/* gimp-auto-perspective core: the model (ashift's homography of
 * rotation, lens shifts and shear) and ashift's line deviation cost
 *
 * Ported from darktable's src/iop/ashift.c (_homography, model_fitness,
 * the vec3 helpers), "Copyright (C) 2016-2026 darktable developers",
 * GPL-3.0-or-later, read at darktable master, commit 131eca7025 of
 * 2026-11-15. ashift says of its homography "most of this comes from
 * ShiftN" (ShiftN by Marcus Hebel). Changed: double instead of float; a
 * translation in our corner based coordinates at the end (ashift_offsets
 * 0) or ashift's own (ashift_offsets 1, for the oracle test,
 * tests/ref/ashift-homography.c).
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "perspective.h"
#include "internal.h"

#include <float.h>
#include <math.h>
#include <string.h>

void
up_lens_generic (UpLens *lens)
{
  lens->focal = UP_DEFAULT_FOCAL;
  lens->orthocorr = 0.0;
  lens->aspect = 1.0;
}

/* ------------------------------------------------------------ vectors and matrices */

void
up__mat3mul (double *dst, const double *a, const double *b)
{
  double r[9];

  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      r[3 * i + j] = a[3 * i] * b[j] + a[3 * i + 1] * b[3 + j] + a[3 * i + 2] * b[6 + j];
  memcpy (dst, r, sizeof r);
}

void
up__mat3mulv (double *dst, const double *m, const double *v)
{
  double r[3];

  for (int i = 0; i < 3; i++)
    r[i] = m[3 * i] * v[0] + m[3 * i + 1] * v[1] + m[3 * i + 2] * v[2];
  memcpy (dst, r, sizeof r);
}

/* the normalised cross product (x^2 + y^2 + z^2 = 1) */
void
up__vec3prodn (double *dst, const double *a, const double *b)
{
  const double l1 = a[1] * b[2] - a[2] * b[1];
  const double l2 = a[2] * b[0] - a[0] * b[2];
  const double l3 = a[0] * b[1] - a[1] * b[0];
  const double sq = sqrt (l1 * l1 + l2 * l2 + l3 * l3);
  const double f = sq > 0.0 ? 1.0 / sq : 1.0;

  dst[0] = l1 * f;
  dst[1] = l2 * f;
  dst[2] = l3 * f;
}

void
up__vec3norm (double *dst, const double *v)
{
  const double sq = sqrt (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  const double f = sq > 0.0 ? 1.0 / sq : 1.0;

  dst[0] = v[0] * f;
  dst[1] = v[1] * f;
  dst[2] = v[2] * f;
}

/* normalised so that x^2 + y^2 = 1: for lines */
void
up__vec3lnorm (double *dst, const double *v)
{
  const double sq = hypot (v[0], v[1]);
  const double f = sq > 0.0 ? 1.0 / sq : 1.0;

  dst[0] = v[0] * f;
  dst[1] = v[1] * f;
  dst[2] = v[2] * f;
}

int
up_matrix_invert (const double m[9], double inv[9])
{
  const double c00 = m[4] * m[8] - m[5] * m[7];
  const double c01 = m[5] * m[6] - m[3] * m[8];
  const double c02 = m[3] * m[7] - m[4] * m[6];
  const double det = m[0] * c00 + m[1] * c01 + m[2] * c02;
  double       r[9];

  if (! isfinite (det) || fabs (det) < 1e-300)
    return 0;
  r[0] = c00 / det;
  r[1] = (m[2] * m[7] - m[1] * m[8]) / det;
  r[2] = (m[1] * m[5] - m[2] * m[4]) / det;
  r[3] = c01 / det;
  r[4] = (m[0] * m[8] - m[2] * m[6]) / det;
  r[5] = (m[2] * m[3] - m[0] * m[5]) / det;
  r[6] = c02 / det;
  r[7] = (m[1] * m[6] - m[0] * m[7]) / det;
  r[8] = (m[0] * m[4] - m[1] * m[3]) / det;
  memcpy (inv, r, sizeof r);
  return 1;
}

int
up_matrix_apply (const double m[9], double x, double y, double *ox, double *oy)
{
  const double w = m[6] * x + m[7] * y + m[8];

  if (! (fabs (w) > 1e-12))
    return 0;
  *ox = (m[0] * x + m[1] * y + m[2]) / w;
  *oy = (m[3] * x + m[4] * y + m[5]) / w;
  return 1;
}

/* ------------------------------------------------------------ the homography */

static double
clampd (double v, double lo, double hi)
{
  return v < lo ? lo : v > hi ? hi : v;
}

void
up__homography (const UpParams *p, const UpLens *lens, int width, int height,
                int ashift_offsets, double *h)
{
  const double u = width, v = height;
  const double phi = p->rotation * M_PI / 180.0;
  const double cosi = cos (phi), sini = sin (phi);
  const double ascale = sqrt (lens->aspect);

  /* most of this comes from ShiftN (as ashift says) */
  const double f_global = lens->focal;
  const double horifac = 1.0 - lens->orthocorr / 100.0;
  const double exppa_v = exp (p->shift_v);
  const double fdb_v = f_global / (14.4 + (v / u - 1) * 7.2);
  const double rad_v = fdb_v * (exppa_v - 1.0) / (exppa_v + 1.0);
  const double alpha_v = clampd (atan (rad_v), -1.5, 1.5);
  const double rt_v = sin (0.5 * alpha_v);
  const double r_v = fmax (0.1, 2.0 * (horifac - 1.0) * rt_v * rt_v + 1.0);

  const double vertifac = 1.0 - lens->orthocorr / 100.0;
  const double exppa_h = exp (p->shift_h);
  const double fdb_h = f_global / (14.4 + (u / v - 1) * 7.2);
  const double rad_h = fdb_h * (exppa_h - 1.0) / (exppa_h + 1.0);
  const double alpha_h = clampd (atan (rad_h), -1.5, 1.5);
  const double rt_h = sin (0.5 * alpha_h);
  const double r_h = fmax (0.1, 2.0 * (vertifac - 1.0) * rt_h * rt_h + 1.0);

  double m[9], w[9];

  /* step 1: swap x and y (ShiftN works in (y : x : 1)) */
  memset (m, 0, sizeof m);
  m[1] = m[3] = m[8] = 1.0;

  /* step 2: rotation about the image centre */
  memset (w, 0, sizeof w);
  w[0] = cosi;
  w[1] = -sini;
  w[3] = sini;
  w[4] = cosi;
  w[2] = -0.5 * v * cosi + 0.5 * u * sini + 0.5 * v;
  w[5] = -0.5 * v * sini - 0.5 * u * cosi + 0.5 * u;
  w[8] = 1.0;
  up__mat3mul (m, w, m);

  /* step 3: shear */
  memset (w, 0, sizeof w);
  w[0] = 1.0;
  w[1] = p->shear;
  w[4] = 1.0;
  w[3] = p->shear;
  w[8] = 1.0;
  up__mat3mul (m, w, m);

  /* step 4: vertical lens shift */
  memset (w, 0, sizeof w);
  w[0] = exppa_v;
  w[3] = 0.5 * ((exppa_v - 1.0) * u) / v;
  w[4] = 2.0 * exppa_v / (exppa_v + 1.0);
  w[5] = -0.5 * ((exppa_v - 1.0) * u) / (exppa_v + 1.0);
  w[6] = (exppa_v - 1.0) / v;
  w[8] = 1.0;
  up__mat3mul (m, w, m);

  /* step 5: horizontal compression */
  memset (w, 0, sizeof w);
  w[0] = 1.0;
  w[4] = r_v;
  w[5] = 0.5 * u * (1.0 - r_v);
  w[8] = 1.0;
  up__mat3mul (m, w, m);

  /* step 6: swap x and y back */
  memset (w, 0, sizeof w);
  w[1] = w[3] = w[8] = 1.0;
  up__mat3mul (m, w, m);

  /* step 7: horizontal lens shift, now in (x : y : 1) */
  memset (w, 0, sizeof w);
  w[0] = exppa_h;
  w[3] = 0.5 * ((exppa_h - 1.0) * v) / u;
  w[4] = 2.0 * exppa_h / (exppa_h + 1.0);
  w[5] = -0.5 * ((exppa_h - 1.0) * v) / (exppa_h + 1.0);
  w[6] = (exppa_h - 1.0) / u;
  w[8] = 1.0;
  up__mat3mul (m, w, m);

  /* step 8: vertical compression */
  memset (w, 0, sizeof w);
  w[0] = 1.0;
  w[4] = r_h;
  w[5] = 0.5 * v * (1.0 - r_h);
  w[8] = 1.0;
  up__mat3mul (m, w, m);

  /* step 9: aspect */
  memset (w, 0, sizeof w);
  w[0] = ascale;
  w[4] = 1.0 / ascale;
  w[8] = 1.0;
  up__mat3mul (m, w, m);

  /* step 10: translate so that no output coordinate is negative: ashift
   * visits the pixel positions 0 and width - 1; we the corners of the
   * image, 0 and width */
  {
    const double last_x = ashift_offsets ? width - 1 : width;
    const double last_y = ashift_offsets ? height - 1 : height;
    double       umin = DBL_MAX, vmin = DBL_MAX;

    for (int j = 0; j < 2; j++)
      for (int i = 0; i < 2; i++)
        {
          const double pi[3] = { i ? last_x : 0.0, j ? last_y : 0.0, 1.0 };
          double       po[3];

          up__mat3mulv (po, m, pi);
          umin = fmin (umin, po[0] / po[2]);
          vmin = fmin (vmin, po[1] / po[2]);
        }
    memset (w, 0, sizeof w);
    w[0] = w[4] = w[8] = 1.0;
    w[2] = -umin;
    w[5] = -vmin;
    up__mat3mul (m, w, m);
  }
  memcpy (h, m, sizeof m);
}

int
up_params_neutral (const UpParams *p)
{
  return p->rotation == 0.0 && p->shift_v == 0.0 && p->shift_h == 0.0 && p->shear == 0.0;
}

static int
lens_ok (const UpLens *l)
{
  return isfinite (l->focal) && l->focal > 0.0 && isfinite (l->orthocorr) &&
         l->orthocorr >= 0.0 && l->orthocorr <= 100.0 && isfinite (l->aspect) && l->aspect > 0.0;
}

static int
params_ok (const UpParams *p)
{
  return isfinite (p->rotation) && isfinite (p->shift_v) && isfinite (p->shift_h) &&
         isfinite (p->shear);
}

UpStatus
up_homography (const UpParams *params, const UpLens *lens, int width, int height, double matrix[9])
{
  static const double identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  UpLens generic;

  if (! params || width <= 0 || height <= 0 || ! matrix)
    return UP_ERROR;
  if (! lens)
    {
      up_lens_generic (&generic);
      lens = &generic;
    }
  memcpy (matrix, identity, sizeof identity);
  if (! lens_ok (lens) || ! params_ok (params))
    return UP_ERROR;
  /* neutral parameters and lens: exactly the identity, not one with
   * rounding errors */
  if (up_params_neutral (params) && lens->orthocorr == 0.0 && lens->aspect == 1.0)
    return UP_OK;
  up__homography (params, lens, width, height, 0, matrix);
  for (int i = 0; i < 9; i++)
    if (! isfinite (matrix[i]))
      {
        memcpy (matrix, identity, sizeof identity);
        return UP_ERROR;
      }
  return UP_OK;
}

/* ------------------------------------------------------------ the cost */

void
up__line_prepare (const UpLine *line, UpPrepLine *p)
{
  p->p1[0] = line->x1;
  p->p1[1] = line->y1;
  p->p1[2] = 1.0;
  p->p2[0] = line->x2;
  p->p2[1] = line->y2;
  p->p2[2] = 1.0;
  up__vec3prodn (p->L, p->p1, p->p2);
  up__vec3lnorm (p->L, p->L);
  p->weight = line->weight;
  p->vertical = line->type == UP_LINE_VERTICAL;
}

/* model_fitness of ashift, for a matrix: transform both end points of
 * every line, take the line through them, and its dot product s with the
 * axis it should be perpendicular to; per direction the weighted mean of
 * s^2 times the share of lines in that direction; then
 * sqrt (1 - (1 - v) (1 - h)) * 1e6 */
double
up__cost_matrix (const UpPrepLine *lines, int count, const double h[9])
{
  double sumsq_v = 0.0, sumsq_h = 0.0, weight_v = 0.0, weight_h = 0.0, v, hh;
  int    count_v = 0, count_h = 0;

  for (int n = 0; n < count; n++)
    {
      double P1[3], P2[3], L[3], s;

      up__mat3mulv (P1, h, lines[n].p1);
      up__mat3mulv (P2, h, lines[n].p2);
      up__vec3prodn (L, P1, P2);
      up__vec3lnorm (L, L);
      /* (vertical lines against the y axis, horizontal ones the x axis) */
      s = lines[n].vertical ? L[1] : L[0];
      if (lines[n].vertical)
        {
          sumsq_v += s * s * lines[n].weight;
          weight_v += lines[n].weight;
          count_v++;
        }
      else
        {
          sumsq_h += s * s * lines[n].weight;
          weight_h += lines[n].weight;
          count_h++;
        }
    }
  v = weight_v > 0.0 && count > 0 ? sumsq_v / weight_v * count_v / count : 0.0;
  hh = weight_h > 0.0 && count > 0 ? sumsq_h / weight_h * count_h / count : 0.0;
  return sqrt (1.0 - (1.0 - v) * (1.0 - hh)) * 1.0e6;
}

/* the lines of the kinds asked for (1 vertical, 2 horizontal) that take
 * part (no outliers), prepared; returns how many, or -1 for out of memory */
int
up__prepare_lines (const UpLine *lines, int count, int kinds, UpPrepLine **out)
{
  int n = 0;

  *out = NULL;
  for (int i = 0; i < count; i++)
    n += ! lines[i].outlier && (((kinds & 1) && lines[i].type == UP_LINE_VERTICAL) ||
                                ((kinds & 2) && lines[i].type == UP_LINE_HORIZONTAL));
  if (n == 0)
    return 0;
  *out = malloc (sizeof (UpPrepLine) * (size_t) n);
  if (! *out)
    return -1;
  n = 0;
  for (int i = 0; i < count; i++)
    if (! lines[i].outlier && (((kinds & 1) && lines[i].type == UP_LINE_VERTICAL) ||
                               ((kinds & 2) && lines[i].type == UP_LINE_HORIZONTAL)))
      up__line_prepare (&lines[i], &(*out)[n++]);
  return n;
}

double
up_cost (const UpLine *lines, int count, int width, int height, const UpLens *lens,
         const UpParams *params, int kinds)
{
  UpLens      generic;
  UpPrepLine *prep;
  double      h[9], cost;
  int         n;

  if (count < 0 || (count > 0 && ! lines) || width <= 0 || height <= 0 || ! params ||
      kinds < 1 || kinds > 3)
    return -1.0;
  if (! lens)
    {
      up_lens_generic (&generic);
      lens = &generic;
    }
  if (! lens_ok (lens) || ! params_ok (params))
    return -1.0;
  n = up__prepare_lines (lines, count, kinds, &prep);
  if (n < 0)
    return -1.0;
  up__homography (params, lens, width, height, 0, h);
  cost = up__cost_matrix (prep, n, h);
  free (prep);
  return cost;
}
