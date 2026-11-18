/* gimp-auto-perspective core: the Nelder-Mead simplex, the logit mapping
 * of the fit parameters, and a small random generator
 *
 * Nelder-Mead as in J. A. Nelder and R. Mead, "A simplex method for
 * function minimization", The Computer Journal 7 (1965), with the
 * reflection, expansion, outside and inside contraction and shrink steps
 * of J. C. Lagarias et al., "Convergence properties of the Nelder-Mead
 * simplex method in low dimensions", SIAM J. Optim. 9 (1998), and the
 * coefficients darktable's ashift uses (1, 2, 0.5, shrink 0.5); the
 * initial simplex is the regular one of Spendley, Hext and Himsworth
 * (1962) at the given scale, the stopping rule ashift's (the standard
 * deviation of the values at the vertices). Written for this project, not
 * copied from the Hutt code ashift includes.
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "internal.h"

#include <math.h>
#include <string.h>

#define NM_MAX 8
#define NM_ALPHA 1.0   /* reflection */
#define NM_GAMMA 2.0   /* expansion */
#define NM_BETA  0.5   /* contraction */
#define NM_DELTA 0.5   /* shrink */

static double
eval (double (*f) (const double *, void *), double *x, int n,
      void (*constrain) (double *, int), void *data)
{
  double v;

  if (constrain)
    constrain (x, n);
  v = f (x, data);
  /* NaN counts as the worst value there is */
  return isnan (v) ? HUGE_VAL : v;
}

int
up__simplex (double (*f) (const double *x, void *data), double *x, int n, double eps,
             double scale, int maxiter, void (*constrain) (double *x, int n), void *data)
{
  double v[NM_MAX + 1][NM_MAX], fv[NM_MAX + 1];
  double xc[NM_MAX], xr[NM_MAX], xe[NM_MAX], xk[NM_MAX];
  int    itr;

  if (n < 1 || n > NM_MAX)
    return maxiter + 1;

  /* the regular simplex with one vertex at x */
  {
    const double pn = scale * (sqrt (n + 1.0) - 1.0 + n) / (n * sqrt (2.0));
    const double qn = scale * (sqrt (n + 1.0) - 1.0) / (n * sqrt (2.0));

    for (int j = 0; j < n; j++)
      v[0][j] = x[j];
    for (int i = 1; i <= n; i++)
      for (int j = 0; j < n; j++)
        v[i][j] = x[j] + (i - 1 == j ? pn : qn);
    for (int i = 0; i <= n; i++)
      fv[i] = eval (f, v[i], n, constrain, data);
  }

  for (itr = 1; itr <= maxiter; itr++)
    {
      int    lo = 0, hi = 0, nh;   /* best, worst, second worst */
      double fr, mean = 0.0, s = 0.0;

      for (int i = 1; i <= n; i++)
        {
          if (fv[i] < fv[lo])
            lo = i;
          if (fv[i] > fv[hi])
            hi = i;
        }
      nh = lo;
      for (int i = 0; i <= n; i++)
        if (i != hi && fv[i] >= fv[nh])
          nh = i;

      /* the centroid of all but the worst */
      for (int j = 0; j < n; j++)
        {
          double c = 0.0;
          for (int i = 0; i <= n; i++)
            if (i != hi)
              c += v[i][j];
          xc[j] = c / n;
        }

      for (int j = 0; j < n; j++)
        xr[j] = xc[j] + NM_ALPHA * (xc[j] - v[hi][j]);
      fr = eval (f, xr, n, constrain, data);

      if (fr < fv[lo])
        {
          double fe;

          for (int j = 0; j < n; j++)
            xe[j] = xc[j] + NM_GAMMA * (xr[j] - xc[j]);
          fe = eval (f, xe, n, constrain, data);
          if (fe < fr)
            {
              memcpy (v[hi], xe, sizeof (double) * n);
              fv[hi] = fe;
            }
          else
            {
              memcpy (v[hi], xr, sizeof (double) * n);
              fv[hi] = fr;
            }
        }
      else if (fr < fv[nh])
        {
          memcpy (v[hi], xr, sizeof (double) * n);
          fv[hi] = fr;
        }
      else
        {
          double fk;
          int    outside = fr < fv[hi];

          for (int j = 0; j < n; j++)
            xk[j] = outside ? xc[j] + NM_BETA * (xr[j] - xc[j])
                            : xc[j] + NM_BETA * (v[hi][j] - xc[j]);
          fk = eval (f, xk, n, constrain, data);
          if (outside ? fk <= fr : fk < fv[hi])
            {
              memcpy (v[hi], xk, sizeof (double) * n);
              fv[hi] = fk;
            }
          else
            {
              /* shrink towards the best */
              for (int i = 0; i <= n; i++)
                if (i != lo)
                  {
                    for (int j = 0; j < n; j++)
                      v[i][j] = v[lo][j] + NM_DELTA * (v[i][j] - v[lo][j]);
                    fv[i] = eval (f, v[i], n, constrain, data);
                  }
            }
        }

      /* converged when the values at the vertices hardly differ */
      for (int i = 0; i <= n; i++)
        mean += fv[i];
      mean /= n + 1;
      for (int i = 0; i <= n; i++)
        s += (fv[i] - mean) * (fv[i] - mean) / n;
      if (sqrt (s) < eps)
        break;
    }

  {
    int lo = 0;
    for (int i = 1; i <= n; i++)
      if (fv[i] < fv[lo])
        lo = i;
    memcpy (x, v[lo], sizeof (double) * n);
  }
  return itr;
}

double
up__logit (double x, double min, double max)
{
  const double eps = 1.0e-6;
  double       p = (x - min) / (max - min);

  p = p < eps ? eps : p > 1.0 - eps ? 1.0 - eps : p;
  return 2.0 * atanh (2.0 * p - 1.0);
}

double
up__ilogit (double l, double min, double max)
{
  const double p = 0.5 * (1.0 + tanh (0.5 * l));
  return p * (max - min) + min;
}

uint32_t
up__random (UpRandom *r)
{
  r->s ^= r->s >> 12;
  r->s ^= r->s << 25;
  r->s ^= r->s >> 27;
  return (uint32_t) ((r->s * 0x2545F4914F6CDD1DULL) >> 32);
}
