/* gimp-auto-perspective core: the crop rectangle and the warp
 *
 * The largest rectangle follows darktable's src/iop/ashift.c
 * (crop_fitness, crop_constraint, do_crop), "Copyright (C) 2016-2026
 * darktable developers", GPL-3.0-or-later, read at darktable master,
 * commit 131eca7025 of 2026-11-15: for a given centre (in input
 * coordinates, so it surely lies inside the transformed image) and
 * aspect angle, the largest rectangle with its corners on the two
 * diagonals through the centre that fits inside the transformed outline;
 * Nelder-Mead searches the centre and, for the free shape, the angle.
 * Changed: double instead of float; a few starting points instead of
 * one, keeping the best (a single start found a local optimum on some of
 * our tests); the result in whole pixels, rounded inwards.
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "perspective.h"
#include "internal.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define NMS_CROP_EPSILON    100.0   /* ashift's, in square pixels */
#define NMS_CROP_SCALE      0.5
#define NMS_CROP_ITERATIONS 100

static const char *const crop_names[UP_CROP_COUNT] = { "none", "largest", "aspect" };

const char *
up_crop_name (UpCrop crop)
{
  return crop >= 0 && crop < UP_CROP_COUNT ? crop_names[crop] : "?";
}

int
up_crop_from_name (const char *name, UpCrop *crop)
{
  for (int c = 0; c < UP_CROP_COUNT; c++)
    if (name && ! strcmp (name, crop_names[c]))
      {
        *crop = (UpCrop) c;
        return 1;
      }
  return 0;
}

typedef struct
{
  double m[9];
  double width, height;
  double edges[4][3];
  double alpha;   /* fixed angle, or NAN when it is searched */
} CropFit;

/* keeps the centre inside the image and the angle in 0..pi/2 (ashift) */
static void
crop_constraint (double *x, int n)
{
  for (int i = 0; i < n; i++)
    x[i] = fabs (x[i]);
  if (n > 0 && x[0] > 1.0)
    x[0] = 1.0 - x[0];
  if (n > 1 && x[1] > 1.0)
    x[1] = 1.0 - x[1];
  if (n > 2 && x[2] > 0.5 * M_PI)
    x[2] = 0.5 * M_PI - x[2];
  /* (the reflections can still leave a value outside for wild steps) */
  for (int i = 0; i < n && i < 2; i++)
    x[i] = x[i] < 0.0 ? 0.0 : x[i] > 1.0 ? 1.0 : x[i];
  if (n > 2)
    x[2] = x[2] < 0.0 ? 0.0 : x[2] > 0.5 * M_PI ? 0.5 * M_PI : x[2];
}

/* the half diagonal^2 of the largest rectangle with that centre (output
 * coordinates) and angle inside the outline */
static double
half_diagonal_sq (const CropFit *c, const double P[3], double alpha)
{
  const double Pa[2][3] = { { P[0] + 10.0 * cos (alpha), P[1] + 10.0 * sin (alpha), 1.0 },
                            { P[0] + 10.0 * cos (alpha), P[1] - 10.0 * sin (alpha), 1.0 } };
  double       D[2][3], d2min = DBL_MAX;

  up__vec3prodn (D[0], P, Pa[0]);
  up__vec3prodn (D[1], P, Pa[1]);
  for (int k = 0; k < 4; k++)
    for (int l = 0; l < 2; l++)
      {
        double I[3];

        up__vec3prodn (I, c->edges[k], D[l]);
        if (fabs (I[0]) < 1e-10 && fabs (I[1]) < 1e-10 && fabs (I[2]) < 1e-10)
          return 0.0;   /* the centre lies on an edge */
        if (I[2] == 0.0)
          continue;     /* parallel */
        I[0] /= I[2];
        I[1] /= I[2];
        d2min = fmin (d2min, (P[0] - I[0]) * (P[0] - I[0]) + (P[1] - I[1]) * (P[1] - I[1]));
      }
  return d2min;
}

static int
centre_out (const CropFit *c, double x, double y, double P[3])
{
  const double Pc[3] = { x * c->width, y * c->height, 1.0 };

  up__mat3mulv (P, c->m, Pc);
  if (! (fabs (P[2]) > 1e-12))
    return 0;
  P[0] /= P[2];
  P[1] /= P[2];
  P[2] = 1.0;
  return 1;
}

/* minus the area of the rectangle (the simplex minimises) */
static double
crop_fitness (const double *x, void *data)
{
  const CropFit *c = data;
  const double   alpha = isnan (c->alpha) ? x[2] : c->alpha;
  double         P[3];

  if (! centre_out (c, x[0], x[1], P))
    return 0.0;
  return -2.0 * half_diagonal_sq (c, P, alpha) * sin (2.0 * alpha);
}

UpStatus
up_crop (const double matrix[9], int width, int height, UpCrop crop, UpRect *rect)
{
  CropFit c;
  double  V[4][3], xmin = DBL_MAX, ymin = DBL_MAX, xmax = -DBL_MAX, ymax = -DBL_MAX;
  double  best[3] = { 0.5, 0.5, 0.0 }, best_f = 0.0, P[3], d, xl, xr, yt, yb;
  int     n;

  if (! matrix || width <= 0 || height <= 0 || crop < 0 || crop >= UP_CROP_COUNT || ! rect)
    return UP_ERROR;
  memset (rect, 0, sizeof *rect);
  memcpy (c.m, matrix, sizeof c.m);
  c.width = width;
  c.height = height;
  {
    const double Vc[4][3] = { { 0, 0, 1 }, { 0, height, 1 }, { width, height, 1 }, { width, 0, 1 } };
    for (int i = 0; i < 4; i++)
      {
        up__mat3mulv (V[i], matrix, Vc[i]);
        if (! (V[i][2] > 1e-12))   /* behind the camera: no sane outline */
          return UP_ERROR;
        V[i][0] /= V[i][2];
        V[i][1] /= V[i][2];
        V[i][2] = 1.0;
        xmin = fmin (xmin, V[i][0]);
        xmax = fmax (xmax, V[i][0]);
        ymin = fmin (ymin, V[i][1]);
        ymax = fmax (ymax, V[i][1]);
      }
  }
  if (! isfinite (xmin + xmax + ymin + ymax))
    return UP_ERROR;
  if (crop == UP_CROP_NONE)
    {
      rect->x = (int) floor (xmin + 1e-6);
      rect->y = (int) floor (ymin + 1e-6);
      rect->width = (int) ceil (xmax - 1e-6) - rect->x;
      rect->height = (int) ceil (ymax - 1e-6) - rect->y;
      return rect->width > 0 && rect->height > 0 ? UP_OK : UP_ERROR;
    }
  for (int i = 0; i < 4; i++)
    up__vec3prodn (c.edges[i], V[i], V[(i + 1) % 4]);

  n = crop == UP_CROP_LARGEST ? 3 : 2;
  c.alpha = crop == UP_CROP_LARGEST ? NAN : atan2 ((double) height, (double) width);
  {
    /* the centre (ashift's only start) and four points around it */
    static const double starts[5][2] = { { 0.5, 0.5 }, { 0.35, 0.35 }, { 0.65, 0.35 },
                                         { 0.35, 0.65 }, { 0.65, 0.65 } };
    for (int s = 0; s < 5; s++)
      {
        double x[3] = { starts[s][0], starts[s][1], atan2 ((double) height, (double) width) };
        double f;

        up__simplex (crop_fitness, x, n, NMS_CROP_EPSILON, NMS_CROP_SCALE, NMS_CROP_ITERATIONS,
                     crop_constraint, &c);
        f = crop_fitness (x, &c);
        if (f < best_f)
          {
            best_f = f;
            memcpy (best, x, sizeof best);
          }
      }
  }
  if (! (best_f < 0.0))
    return UP_ERROR;
  {
    const double alpha = isnan (c.alpha) ? best[2] : c.alpha;

    if (! centre_out (&c, best[0], best[1], P))
      return UP_ERROR;
    d = sqrt (-best_f / (2.0 * sin (2.0 * alpha)));
    xl = P[0] - d * cos (alpha);
    xr = P[0] + d * cos (alpha);
    yt = P[1] - d * sin (alpha);
    yb = P[1] + d * sin (alpha);
  }
  rect->x = (int) ceil (xl - 1e-6);
  rect->y = (int) ceil (yt - 1e-6);
  rect->width = (int) floor (xr + 1e-6) - rect->x;
  rect->height = (int) floor (yb + 1e-6) - rect->y;
  if (rect->width <= 0 || rect->height <= 0)
    {
      memset (rect, 0, sizeof *rect);
      return UP_ERROR;
    }
  return UP_OK;
}

/* ------------------------------------------------------------ warp */

static inline float
pix (const float *in, int w, int h, int c, int x, int y, int k)
{
  x = x < 0 ? 0 : x >= w ? w - 1 : x;
  y = y < 0 ? 0 : y >= h ? h - 1 : y;
  return in[((size_t) y * w + x) * c + k];
}

static inline double
catmull_rom (double t, double a, double b, double c, double d)
{
  return b + 0.5 * t * (c - a + t * (2.0 * a - 5.0 * b + 4.0 * c - d + t * (3.0 * (b - c) + d - a)));
}

UpStatus
up_warp (const float *in, int width, int height, int channels, const double matrix[9],
         const UpRect *rect, UpInterp interp, float *out, float *coverage)
{
  double inv[9];

  if (! in || width <= 0 || height <= 0 || channels < 1 || ! matrix || ! rect ||
      rect->width <= 0 || rect->height <= 0 || ! out || interp < UP_INTERP_NEAREST ||
      interp > UP_INTERP_CUBIC || ! up_matrix_invert (matrix, inv))
    return UP_ERROR;

  for (int j = 0; j < rect->height; j++)
    for (int i = 0; i < rect->width; i++)
      {
        const size_t o = (size_t) j * rect->width + i;
        double       x, y;
        int          inside = up_matrix_apply (inv, rect->x + i + 0.5, rect->y + j + 0.5, &x, &y) &&
                              x >= 0.0 && y >= 0.0 && x < width && y < height;

        if (coverage)
          coverage[o] = inside ? 1.0f : 0.0f;
        if (! inside)
          {
            for (int k = 0; k < channels; k++)
              out[o * channels + k] = 0.0f;
            continue;
          }
        /* pixel centres are at +0.5 */
        x -= 0.5;
        y -= 0.5;
        {
          const int    x0 = (int) floor (x), y0 = (int) floor (y);
          const double fx = x - x0, fy = y - y0;

          for (int k = 0; k < channels; k++)
            {
              double v;

              if (interp == UP_INTERP_NEAREST)
                v = pix (in, width, height, channels, (int) floor (x + 0.5), (int) floor (y + 0.5), k);
              else if (interp == UP_INTERP_LINEAR)
                v = (1 - fy) * ((1 - fx) * pix (in, width, height, channels, x0, y0, k) +
                                fx * pix (in, width, height, channels, x0 + 1, y0, k)) +
                    fy * ((1 - fx) * pix (in, width, height, channels, x0, y0 + 1, k) +
                          fx * pix (in, width, height, channels, x0 + 1, y0 + 1, k));
              else
                {
                  double r[4];
                  for (int m = 0; m < 4; m++)
                    r[m] = catmull_rom (fx, pix (in, width, height, channels, x0 - 1, y0 - 1 + m, k),
                                        pix (in, width, height, channels, x0, y0 - 1 + m, k),
                                        pix (in, width, height, channels, x0 + 1, y0 - 1 + m, k),
                                        pix (in, width, height, channels, x0 + 2, y0 - 1 + m, k));
                  v = catmull_rom (fy, r[0], r[1], r[2], r[3]);
                }
              out[o * channels + k] = (float) v;
            }
        }
      }
  return UP_OK;
}
