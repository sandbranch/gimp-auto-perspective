/* LSD, the line segment detector, in C: see lsd.h
 *
 * Ported to C for gimp-auto-perspective (2026) from OpenCV's
 * modules/imgproc/src/lsd.cpp (opencv/opencv, branch 4.x, read at commit
 * ecb072401a of 2026-11-17). What changed against that file: plain C
 * instead of C++ and cv::Mat; double input instead of 8 bit; the Gaussian
 * subsampling is our own, with the sample positions of the LSD paper
 * (OpenCV calls GaussianBlur and resize); the
 * pseudo ordering by gradient norm is a counting sort (the same order as
 * OpenCV's stable sort); atan2 instead of fastAtan2; only the refinement
 * OpenCV calls LSD_REFINE_ADV (the one of the paper and of LSD 1.6, which
 * darktable's ashift uses); end points in our corner based coordinates.
 *
 * About the licence of this code: OpenCV removed lsd.cpp on 2019-03-01
 * (commit 3ba49ccecc, "removed LSD code due original code license
 * conflict": parts of it came from the AGPL licensed LSD 1.6) and restored
 * it unchanged on 2021-10-01 (commit 9b76872708, opencv/opencv pull
 * request 20797, released in 4.5.4) after the author of LSD, Rafael
 * Grompone von Gioi, published the NFA code under the MIT licence
 * (github.com/rafael-grompone-von-gioi/binomial_nfa) and wrote in
 * opencv/opencv_contrib issue 2524 (2021-05-10) that OpenCV should keep
 * its implementation and only update the doubtful parts. OpenCV ships it
 * under the 3-clause BSD licence below. See docs/research.md.
 *
 * The licence of OpenCV's lsd.cpp, which this file keeps:
 *
 *  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
 *
 *  By downloading, copying, installing or using the software you agree to this license.
 *  If you do not agree to this license, do not download, install,
 *  copy or use the software.
 *
 *
 *                           License Agreement
 *                For Open Source Computer Vision Library
 *
 * Copyright (C) 2013, OpenCV Foundation, all rights reserved.
 * Third party copyrights are property of their respective owners.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 *   * The name of the copyright holders may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * This software is provided by the copyright holders and contributors "as is" and
 * any express or implied warranties, including, but not limited to, the implied
 * warranties of merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the Intel Corporation or contributors be liable for any direct,
 * indirect, incidental, special, exemplary, or consequential damages
 * (including, but not limited to, procurement of substitute goods or services;
 * loss of use, data, or profits; or business interruption) however caused
 * and on any theory of liability, whether in contract, strict liability,
 * or tort (including negligence or otherwise) arising in any way out of
 * the use of this software, even if advised of the possibility of such damage.
 *
 * Changes for gimp-auto-perspective: Copyright 2026 David, under the same
 * 3-clause BSD licence.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "lsd.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define M_3_2_PI (1.5 * M_PI)
#define M_2__PI  (2.0 * M_PI)
#ifndef M_LN10
#define M_LN10 2.30258509299404568402
#endif

#define NOTDEF  (-1024.0)   /* the angle of a pixel without a defined gradient */
#define NOTUSED 0
#define USED    1
#define RELATIVE_ERROR_FACTOR 100.0

typedef struct
{
  int    x, y;
  double angle, modgrad;
} RegionPoint;

typedef struct
{
  double x1, y1, x2, y2;  /* the segment */
  double width;
  double x, y;            /* center */
  double theta;
  double dx, dy;          /* unit vector along the segment */
  double prec;            /* angle tolerance */
  double p;               /* probability of a point within prec */
} Rect;

typedef struct
{
  int            w, h;
  double        *angles;
  double        *modgrad;
  unsigned char *used;
  int           *ordered;    /* pixel indices by decreasing gradient norm */
  int            n_ordered;
  RegionPoint   *reg;        /* room for a region of every pixel */
  int            reg_size;
  double         log_nt;
  double         log_eps;
} Lsd;

void
up_lsd_defaults (UpLsdParams *p)
{
  p->scale = 0.99;
  p->sigma_scale = 0.6;
  p->quant = 2.0;
  p->ang_th = 22.5;
  p->log_eps = 0.0;
  p->density_th = 0.7;
  p->n_bins = 1024;
}

/* ------------------------------------------------------------ helpers */

static inline double
dist_sq (double x1, double y1, double x2, double y2)
{
  return (x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1);
}

static inline double
dist (double x1, double y1, double x2, double y2)
{
  return sqrt (dist_sq (x1, y1, x2, y2));
}

static inline double
angle_diff_signed (double a, double b)
{
  double d = a - b;
  while (d <= -M_PI)
    d += M_2__PI;
  while (d > M_PI)
    d -= M_2__PI;
  return d;
}

static inline double
angle_diff (double a, double b)
{
  return fabs (angle_diff_signed (a, b));
}

/* equal up to a relative error of RELATIVE_ERROR_FACTOR machine epsilons */
static inline int
double_equal (double a, double b)
{
  double m;

  if (a == b)
    return 1;
  m = fmax (fabs (a), fabs (b));
  if (m < DBL_MIN)
    m = DBL_MIN;
  return fabs (a - b) / m <= RELATIVE_ERROR_FACTOR * DBL_EPSILON;
}

/* log |Gamma(x)|, Windschitl's approximation (good above 15) */
static inline double
log_gamma_windschitl (double x)
{
  return 0.918938533204673 + (x - 0.5) * log (x) - x
         + 0.5 * x * log (x * sinh (1.0 / x) + 1.0 / (810.0 * pow (x, 6.0)));
}

/* log |Gamma(x)|, Lanczos' approximation */
static inline double
log_gamma_lanczos (double x)
{
  static const double q[7] = { 75122.6331530, 80916.6278952, 36308.2951477,
                               8687.24529705, 1168.92649479, 83.8676043424,
                               2.50662827511 };
  double a = (x + 0.5) * log (x + 5.5) - (x + 5.5);
  double b = 0.0;

  for (int n = 0; n < 7; n++)
    {
      a -= log (x + (double) n);
      b += q[n] * pow (x, (double) n);
    }
  return a + log (b);
}

static inline double
log_gamma (double x)
{
  return x > 15.0 ? log_gamma_windschitl (x) : log_gamma_lanczos (x);
}

/* ------------------------------------------------------------ Gaussian subsampling */

/* one pass of the separable Gaussian sampler: n input samples (stride
 * istride) to m output samples (stride ostride). As the LSD paper places
 * them: output sample i takes the input around i / scale, coordinates at
 * pixel centres, with the 2 half + 1 taps around the nearest input
 * sample and a symmetric boundary */
static void
gauss_pass (const double *in, int n, int istride, double *out, int m, int ostride,
            double scale, double sigma, int half)
{
  for (int i = 0; i < m; i++)
    {
      const double c = i / scale;
      const int    j0 = (int) floor (c + 0.5);
      double       sum = 0.0, wsum = 0.0;

      for (int j = j0 - half; j <= j0 + half; j++)
        {
          const double d = j - c;
          const double g = exp (-0.5 * d * d / (sigma * sigma));
          int          k = j;

          while (k < 0 || k >= n)
            k = k < 0 ? -k - 1 : 2 * n - k - 1;
          sum += g * in[(size_t) k * istride];
          wsum += g;
        }
      out[(size_t) i * ostride] = sum / wsum;
    }
}

/* the image blurred with sigma and scaled by scale (< 1), as the LSD
 * paper does it before detection */
static double *
gaussian_sampler (const double *in, int w, int h, double scale, double sigma, int *nw, int *nh)
{
  const double prec = 3.0;
  const int    half = (int) ceil (sigma * sqrt (2.0 * prec * log (10.0)));
  double      *tmp, *out;

  *nw = (int) ceil (w * scale);
  *nh = (int) ceil (h * scale);
  if (*nw < 2 || *nh < 2)
    return NULL;
  tmp = malloc (sizeof (double) * (size_t) *nw * h);
  out = malloc (sizeof (double) * (size_t) *nw * *nh);
  if (! tmp || ! out)
    {
      free (tmp);
      free (out);
      return NULL;
    }
  for (int y = 0; y < h; y++)
    gauss_pass (in + (size_t) y * w, w, 1, tmp + (size_t) y * *nw, *nw, 1, scale, sigma, half);
  for (int x = 0; x < *nw; x++)
    gauss_pass (tmp + x, h, *nw, out + x, *nh, *nw, scale, sigma, half);
  free (tmp);
  return out;
}

/* ------------------------------------------------------------ gradient */

/* gradient angles and norms, and the pixels pseudo ordered by norm */
static int
ll_angle (Lsd *l, const double *img, double threshold, int n_bins)
{
  const int w = l->w, h = l->h;
  double    max_grad = -1.0, bin_coef;
  int      *count, *start;

  for (int x = 0; x < w; x++)
    {
      l->angles[(size_t) (h - 1) * w + x] = NOTDEF;
      l->modgrad[(size_t) (h - 1) * w + x] = 0.0;
    }
  for (int y = 0; y < h; y++)
    {
      l->angles[(size_t) y * w + w - 1] = NOTDEF;
      l->modgrad[(size_t) y * w + w - 1] = 0.0;
    }
  for (int y = 0; y < h - 1; y++)
    {
      const double *row = img + (size_t) y * w, *next = row + w;
      double       *ang = l->angles + (size_t) y * w, *mod = l->modgrad + (size_t) y * w;

      for (int x = 0; x < w - 1; x++)
        {
          const double da = next[x + 1] - row[x];
          const double bc = row[x + 1] - next[x];
          const double gx = da + bc;
          const double gy = da - bc;
          const double norm = sqrt ((gx * gx + gy * gy) / 4.0);

          mod[x] = norm;
          if (norm <= threshold)
            ang[x] = NOTDEF;
          else
            {
              ang[x] = atan2 (gx, -gy);
              if (norm > max_grad)
                max_grad = norm;
            }
        }
    }

  /* counting sort by bin, highest first; within a bin column after
   * column (x outer, y inner), and bins of norm * n_bins / max_grad, as
   * LSD 1.6 orders them (OpenCV goes row after row with n_bins - 1;
   * measured against LSD 1.6's output by tests/lsd-ref.py) */
  bin_coef = max_grad > 0.0 ? (double) n_bins / max_grad : 0.0;
  count = calloc ((size_t) n_bins + 1, sizeof (int));
  start = malloc (sizeof (int) * ((size_t) n_bins + 1));
  if (! count || ! start)
    {
      free (count);
      free (start);
      return -1;
    }
  for (int x = 0; x < w - 1; x++)
    for (int y = 0; y < h - 1; y++)
      {
        int b = (int) (l->modgrad[(size_t) y * w + x] * bin_coef);
        count[b < 0 ? 0 : b >= n_bins ? n_bins - 1 : b]++;
      }
  start[n_bins - 1] = 0;
  for (int b = n_bins - 2; b >= 0; b--)
    start[b] = start[b + 1] + count[b + 1];
  for (int x = 0; x < w - 1; x++)
    for (int y = 0; y < h - 1; y++)
      {
        int b = (int) (l->modgrad[(size_t) y * w + x] * bin_coef);
        b = b < 0 ? 0 : b >= n_bins ? n_bins - 1 : b;
        l->ordered[start[b]++] = y * w + x;
      }
  l->n_ordered = (w - 1) * (h - 1);
  free (count);
  free (start);
  return 0;
}

static inline int
is_aligned (const Lsd *l, int x, int y, double theta, double prec)
{
  double a, n;

  if (x < 0 || y < 0 || x >= l->w || y >= l->h)
    return 0;
  a = l->angles[(size_t) y * l->w + x];
  if (a == NOTDEF)
    return 0;
  /* theta and a are in [-pi, pi] */
  n = fabs (theta - a);
  if (n > M_3_2_PI)
    n = fabs (n - M_2__PI);
  return n <= prec;
}

/* ------------------------------------------------------------ regions */

static void
region_grow (Lsd *l, int sx, int sy, double *reg_angle, double prec)
{
  const int w = l->w, h = l->h;
  double    sumdx, sumdy;
  int       n = 0;

  *reg_angle = l->angles[(size_t) sy * w + sx];
  l->reg[n++] = (RegionPoint) { sx, sy, *reg_angle, l->modgrad[(size_t) sy * w + sx] };
  l->used[(size_t) sy * w + sx] = USED;
  sumdx = cos (*reg_angle);
  sumdy = sin (*reg_angle);

  for (int i = 0; i < n; i++)
    {
      const int x0 = l->reg[i].x, y0 = l->reg[i].y;
      const int xmin = x0 > 0 ? x0 - 1 : 0, xmax = x0 < w - 1 ? x0 + 1 : w - 1;
      const int ymin = y0 > 0 ? y0 - 1 : 0, ymax = y0 < h - 1 ? y0 + 1 : h - 1;

      for (int yy = ymin; yy <= ymax; yy++)
        for (int xx = xmin; xx <= xmax; xx++)
          {
            const size_t k = (size_t) yy * w + xx;

            if (l->used[k] != USED && is_aligned (l, xx, yy, *reg_angle, prec))
              {
                const double a = l->angles[k];

                l->used[k] = USED;
                l->reg[n++] = (RegionPoint) { xx, yy, a, l->modgrad[k] };
                sumdx += cos (a);
                sumdy += sin (a);
                *reg_angle = atan2 (sumdy, sumdx);
              }
          }
    }
  l->reg_size = n;
}

/* the angle of the region: its principal axis of inertia */
static double
get_theta (const Lsd *l, double x, double y, double reg_angle, double prec)
{
  double ixx = 0.0, iyy = 0.0, ixy = 0.0, lambda, theta;

  for (int i = 0; i < l->reg_size; i++)
    {
      const double dx = l->reg[i].x - x, dy = l->reg[i].y - y, wgt = l->reg[i].modgrad;
      ixx += dy * dy * wgt;
      iyy += dx * dx * wgt;
      ixy -= dx * dy * wgt;
    }
  if (double_equal (ixx, 0.0) && double_equal (iyy, 0.0) && double_equal (ixy, 0.0))
    return reg_angle;   /* (OpenCV asserts here; a single point has no axis) */
  lambda = 0.5 * (ixx + iyy - sqrt ((ixx - iyy) * (ixx - iyy) + 4.0 * ixy * ixy));
  theta = fabs (ixx) > fabs (iyy) ? atan2 (lambda - ixx, ixy) : atan2 (ixy, lambda - iyy);
  if (angle_diff (theta, reg_angle) > prec)
    theta += M_PI;
  return theta;
}

static int
region2rect (const Lsd *l, double reg_angle, double prec, double p, Rect *rec)
{
  double x = 0.0, y = 0.0, sum = 0.0, theta, dx, dy;
  double l_min = 0.0, l_max = 0.0, w_min = 0.0, w_max = 0.0;

  for (int i = 0; i < l->reg_size; i++)
    {
      x += l->reg[i].x * l->reg[i].modgrad;
      y += l->reg[i].y * l->reg[i].modgrad;
      sum += l->reg[i].modgrad;
    }
  if (! (sum > 0.0))
    return 0;
  x /= sum;
  y /= sum;
  theta = get_theta (l, x, y, reg_angle, prec);
  dx = cos (theta);
  dy = sin (theta);
  for (int i = 0; i < l->reg_size; i++)
    {
      const double rx = l->reg[i].x - x, ry = l->reg[i].y - y;
      const double ll = rx * dx + ry * dy;
      const double ww = -rx * dy + ry * dx;

      if (ll > l_max)
        l_max = ll;
      else if (ll < l_min)
        l_min = ll;
      if (ww > w_max)
        w_max = ww;
      else if (ww < w_min)
        w_min = ww;
    }
  rec->x1 = x + l_min * dx;
  rec->y1 = y + l_min * dy;
  rec->x2 = x + l_max * dx;
  rec->y2 = y + l_max * dy;
  rec->width = w_max - w_min;
  rec->x = x;
  rec->y = y;
  rec->theta = theta;
  rec->dx = dx;
  rec->dy = dy;
  rec->prec = prec;
  rec->p = p;
  if (rec->width < 1.0)
    rec->width = 1.0;
  return 1;
}

static int
reduce_region_radius (Lsd *l, double reg_angle, double prec, double p, Rect *rec,
                      double density, double density_th)
{
  const double xc = l->reg[0].x, yc = l->reg[0].y;
  double       rad_sq = fmax (dist_sq (xc, yc, rec->x1, rec->y1), dist_sq (xc, yc, rec->x2, rec->y2));

  while (density < density_th)
    {
      rad_sq *= 0.75 * 0.75;
      for (int i = 0; i < l->reg_size; i++)
        if (dist_sq (xc, yc, l->reg[i].x, l->reg[i].y) > rad_sq)
          {
            l->used[(size_t) l->reg[i].y * l->w + l->reg[i].x] = NOTUSED;
            l->reg[i] = l->reg[l->reg_size - 1];
            l->reg_size--;
            i--;
          }
      if (l->reg_size < 2)
        return 0;
      if (! region2rect (l, reg_angle, prec, p, rec))
        return 0;
      density = l->reg_size / (dist (rec->x1, rec->y1, rec->x2, rec->y2) * rec->width);
    }
  return 1;
}

static int
refine (Lsd *l, double reg_angle, double prec, double p, Rect *rec, double density_th)
{
  double density = l->reg_size / (dist (rec->x1, rec->y1, rec->x2, rec->y2) * rec->width);
  double xc, yc, ang_c, sum = 0.0, s_sum = 0.0, mean, tau;
  int    n = 0, sx, sy;

  if (density >= density_th)
    return 1;

  /* a smaller angle tolerance, from the angles near the seed */
  xc = l->reg[0].x;
  yc = l->reg[0].y;
  ang_c = l->reg[0].angle;
  for (int i = 0; i < l->reg_size; i++)
    {
      l->used[(size_t) l->reg[i].y * l->w + l->reg[i].x] = NOTUSED;
      if (dist (xc, yc, l->reg[i].x, l->reg[i].y) < rec->width)
        {
          const double d = angle_diff_signed (l->reg[i].angle, ang_c);
          sum += d;
          s_sum += d * d;
          n++;
        }
    }
  if (n == 0)
    return 0;
  mean = sum / n;
  tau = 2.0 * sqrt ((s_sum - 2.0 * mean * sum) / n + mean * mean);   /* twice the std */

  sx = l->reg[0].x;
  sy = l->reg[0].y;
  region_grow (l, sx, sy, &reg_angle, tau);
  if (l->reg_size < 2)
    return 0;
  if (! region2rect (l, reg_angle, prec, p, rec))
    return 0;
  density = l->reg_size / (dist (rec->x1, rec->y1, rec->x2, rec->y2) * rec->width);
  if (density < density_th)
    return reduce_region_radius (l, reg_angle, prec, p, rec, density, density_th);
  return 1;
}

/* ------------------------------------------------------------ NFA */

/* -log10 (NFA) of n points of which k are aligned with probability p */
static double
nfa (double log_nt, int n, int k, double p)
{
  double p_term, log1term, term, bin_tail, tolerance = 0.1;

  if (n == 0 || k == 0)
    return -log_nt;
  if (n == k)
    return -log_nt - (double) n * log10 (p);
  p_term = p / (1.0 - p);
  log1term = log_gamma ((double) n + 1.0) - log_gamma ((double) k + 1.0)
             - log_gamma ((double) (n - k) + 1.0)
             + (double) k * log (p) + (double) (n - k) * log (1.0 - p);
  term = exp (log1term);
  if (double_equal (term, 0.0))
    return k > n * p ? -log1term / M_LN10 - log_nt : -log_nt;
  bin_tail = term;
  for (int i = k + 1; i <= n; i++)
    {
      const double bin_term = (double) (n - i + 1) / (double) i;
      const double mult_term = bin_term * p_term;

      term *= mult_term;
      bin_tail += term;
      if (bin_term < 1.0)
        {
          const double err = term * ((1.0 - pow (mult_term, (double) (n - i + 1))) / (1.0 - mult_term) - 1.0);
          if (err < tolerance * fabs (-log10 (bin_tail) - log_nt) * bin_tail)
            break;
        }
    }
  return -log10 (bin_tail) - log_nt;
}

static inline int
smaller_y_over_x (double ax, double ay, double bx, double by)
{
  return ay == by ? ax < bx : ay < by;
}

static inline double
get_slope (double x1, double y1, double x2, double y2)
{
  return (int) ceil (y2) != (int) ceil (y1) ? (x2 - x1) / (y2 - y1) : 0.0;
}

static double
rect_nfa (const Lsd *l, const Rect *rec)
{
  const double hw = rec->width / 2.0, dyhw = rec->dy * hw, dxhw = rec->dx * hw;
  double       vx[4] = { rec->x1 - dyhw, rec->x2 - dyhw, rec->x2 + dyhw, rec->x1 + dyhw };
  double       vy[4] = { rec->y1 + dxhw, rec->y2 + dxhw, rec->y2 - dxhw, rec->y1 - dxhw };
  double       ox[4], oy[4], fl, sl, fr, sr;
  int          offset = 0, total = 0, aligned = 0;

  /* the vertex of smallest y (then x) first, the others in order */
  for (int i = 1; i < 4; i++)
    if (smaller_y_over_x (vx[i], vy[i], vx[offset], vy[offset]))
      offset = i;
  for (int i = 0; i < 4; i++)
    {
      ox[i] = vx[(i + offset) % 4];
      oy[i] = vy[(i + offset) % 4];
    }
  fl = get_slope (ox[0], oy[0], ox[1], oy[1]);
  sl = get_slope (ox[1], oy[1], ox[2], oy[2]);
  fr = get_slope (ox[0], oy[0], ox[3], oy[3]);
  sr = get_slope (ox[3], oy[3], ox[2], oy[2]);

  for (int y = (int) ceil (oy[0]); y <= (int) ceil (oy[2]); y++)
    {
      double left, right;

      if (y < 0 || y >= l->h)
        continue;
      left = y <= (int) ceil (oy[1]) ? ox[0] + (y - oy[0]) * fl : ox[1] + (y - oy[1]) * sl;
      right = y < (int) ceil (oy[3]) ? ox[0] + (y - oy[0]) * fr : ox[3] + (y - oy[3]) * sr;
      for (int x = (int) ceil (left); x <= (int) right; x++)
        {
          if (x < 0 || x >= l->w)
            continue;
          total++;
          aligned += is_aligned (l, x, y, rec->theta, rec->prec);
        }
    }
  return nfa (l->log_nt, total, aligned, rec->p);
}

/* tries variations of the rectangle to improve its NFA */
static double
rect_improve (const Lsd *l, Rect *rec)
{
  const double delta = 0.5, delta_2 = delta / 2.0;
  double       log_nfa = rect_nfa (l, rec), n;
  Rect         r;

  if (log_nfa > l->log_eps)
    return log_nfa;

  /* finer precision */
  r = *rec;
  for (int i = 0; i < 5; i++)
    {
      r.p /= 2.0;
      r.prec = r.p * M_PI;
      if ((n = rect_nfa (l, &r)) > log_nfa)
        {
          log_nfa = n;
          *rec = r;
        }
    }
  if (log_nfa > l->log_eps)
    return log_nfa;

  /* narrower */
  r = *rec;
  for (int i = 0; i < 5; i++)
    if (r.width - delta >= 0.5)
      {
        r.width -= delta;
        if ((n = rect_nfa (l, &r)) > log_nfa)
          {
            *rec = r;
            log_nfa = n;
          }
      }
  if (log_nfa > l->log_eps)
    return log_nfa;

  /* one side in */
  r = *rec;
  for (int i = 0; i < 5; i++)
    if (r.width - delta >= 0.5)
      {
        r.x1 += -r.dy * delta_2;
        r.y1 += r.dx * delta_2;
        r.x2 += -r.dy * delta_2;
        r.y2 += r.dx * delta_2;
        r.width -= delta;
        if ((n = rect_nfa (l, &r)) > log_nfa)
          {
            *rec = r;
            log_nfa = n;
          }
      }
  if (log_nfa > l->log_eps)
    return log_nfa;

  /* the other side in */
  r = *rec;
  for (int i = 0; i < 5; i++)
    if (r.width - delta >= 0.5)
      {
        r.x1 -= -r.dy * delta_2;
        r.y1 -= r.dx * delta_2;
        r.x2 -= -r.dy * delta_2;
        r.y2 -= r.dx * delta_2;
        r.width -= delta;
        if ((n = rect_nfa (l, &r)) > log_nfa)
          {
            *rec = r;
            log_nfa = n;
          }
      }
  if (log_nfa > l->log_eps)
    return log_nfa;

  /* finer precision again */
  r = *rec;
  for (int i = 0; i < 5; i++)
    if (r.width - delta >= 0.5)
      {
        r.p /= 2.0;
        r.prec = r.p * M_PI;
        if ((n = rect_nfa (l, &r)) > log_nfa)
          {
            *rec = r;
            log_nfa = n;
          }
      }
  return log_nfa;
}

/* ------------------------------------------------------------ driver */

int
up_lsd (const double *image, int width, int height, const UpLsdParams *params,
        UpLsdSegment **segments, int *count)
{
  UpLsdParams   def;
  const double *img = image;
  double       *scaled = NULL;
  Lsd           l = { 0 };
  UpLsdSegment *out = NULL;
  int           n_out = 0, cap = 0, status = -1;
  double        prec, p, rho, scale;
  size_t        min_reg_size, npix;

  if (! image || width < 2 || height < 2 || ! segments || ! count ||
      (size_t) width * height > (size_t) INT32_MAX)
    return -1;
  *segments = NULL;
  *count = 0;
  if (! params)
    {
      up_lsd_defaults (&def);
      params = &def;
    }
  if (! (params->scale > 0.0 && params->scale <= 1.0) || ! (params->sigma_scale > 0.0) ||
      ! (params->quant >= 0.0) || ! (params->ang_th > 0.0 && params->ang_th < 180.0) ||
      ! (params->density_th >= 0.0 && params->density_th < 1.0) || params->n_bins <= 0)
    return -1;

  scale = params->scale;
  prec = M_PI * params->ang_th / 180.0;
  p = params->ang_th / 180.0;
  rho = params->quant / sin (prec);

  l.w = width;
  l.h = height;
  if (scale != 1.0)
    {
      scaled = gaussian_sampler (image, width, height, scale, params->sigma_scale / scale, &l.w, &l.h);
      if (! scaled)
        {
          /* too small to scale: nothing to find */
          if (l.w < 2 || l.h < 2)
            status = 0;
          goto out;
        }
      img = scaled;
    }
  npix = (size_t) l.w * l.h;
  l.angles = malloc (sizeof (double) * npix);
  l.modgrad = malloc (sizeof (double) * npix);
  l.used = calloc (npix, 1);
  l.ordered = malloc (sizeof (int) * npix);
  l.reg = malloc (sizeof (RegionPoint) * npix);
  if (! l.angles || ! l.modgrad || ! l.used || ! l.ordered || ! l.reg)
    goto out;
  if (ll_angle (&l, img, rho, params->n_bins))
    goto out;

  l.log_nt = 5.0 * (log10 ((double) l.w) + log10 ((double) l.h)) / 2.0 + log10 (11.0);
  l.log_eps = params->log_eps;
  min_reg_size = (size_t) (-l.log_nt / log10 (p));

  for (int i = 0; i < l.n_ordered; i++)
    {
      const int k = l.ordered[i];
      double    reg_angle, log_nfa;
      Rect      rec;

      if (l.used[k] != NOTUSED || l.angles[k] == NOTDEF)
        continue;
      region_grow (&l, k % l.w, k / l.w, &reg_angle, prec);
      if ((size_t) l.reg_size < min_reg_size)
        continue;
      if (! region2rect (&l, reg_angle, prec, p, &rec))
        continue;
      if (! refine (&l, reg_angle, prec, p, &rec, params->density_th))
        continue;
      log_nfa = rect_improve (&l, &rec);
      if (log_nfa <= l.log_eps)
        continue;

      if (n_out == cap)
        {
          UpLsdSegment *more = realloc (out, sizeof (UpLsdSegment) * (cap = cap ? 2 * cap : 64));
          if (! more)
            goto out;
          out = more;
        }
      /* the gradient of pixel (x, y) is that of the 2x2 block from it, so
       * it lies at (x + 0.5, y + 0.5) with pixel centres at whole
       * numbers; scaled pixel x is input position x / scale (see
       * gauss_pass); plus half a pixel for our corner based coordinates */
      out[n_out++] = (UpLsdSegment) {
        (rec.x1 + 0.5) / scale + 0.5, (rec.y1 + 0.5) / scale + 0.5,
        (rec.x2 + 0.5) / scale + 0.5, (rec.y2 + 0.5) / scale + 0.5,
        rec.width / scale, rec.p, log_nfa
      };
    }
  status = 0;

out:
  free (scaled);
  free (l.angles);
  free (l.modgrad);
  free (l.used);
  free (l.ordered);
  free (l.reg);
  if (status == 0 && n_out > 0)
    {
      *segments = out;
      *count = n_out;
    }
  else
    free (out);
  return status;
}
