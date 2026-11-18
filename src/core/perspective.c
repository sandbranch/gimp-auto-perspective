/* gimp-auto-perspective core: names, classification, detection and the
 * fit driver; see perspective.h
 *
 * The detection and the fit follow darktable's src/iop/ashift.c
 * (line_detect, nmsfit), "Copyright (C) 2016-2026 darktable developers",
 * GPL-3.0-or-later, read at darktable master, commit 131eca7025 of
 * 2026-11-15. The automatic choice of the mode is ours (ashift leaves it
 * to the user).
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "perspective.h"
#include "internal.h"
#include "lsd.h"

#include <float.h>
#include <math.h>
#include <string.h>

/* the Nelder-Mead settings of ashift's fit */
#define NMS_EPSILON    1e-3
#define NMS_SCALE      1.0
#define NMS_ITERATIONS 400

/* the automatic mode: Full instead of Vertical only if it brings the cost
 * of all lines below this share of Vertical's and its largest crop keeps
 * at least this share of the image (see up_fit, tuned on tests/synth.py
 * facades: tests/recovery.py) */
#define AUTO_COST_RATIO 0.5
#define AUTO_MIN_AREA   0.5

/* the automatic mode takes a fit only if the lines it used agree after
 * it: the weighted median of their deviation from vertical (or
 * horizontal) must be below this many degrees, for each kind the mode
 * fits. Measured on 13 real photos (PLAN.md 5.3): 0.2 to 2.0 degrees on
 * buildings, interiors, a street and tree trunks, 7 to 13 on the rocks
 * and hills of a landscape, where a Vertical fit kept 37 % of the image
 * and turned it 7 degrees. */
#define AUTO_MAX_RESIDUAL 4.0

static const char *const mode_names[UP_MODE_COUNT] = {
  "auto", "vertical", "horizontal", "full", "rotation"
};

const char *
up_mode_name (UpMode mode)
{
  return mode >= 0 && mode < UP_MODE_COUNT ? mode_names[mode] : "?";
}

int
up_mode_from_name (const char *name, UpMode *mode)
{
  for (int m = 0; m < UP_MODE_COUNT; m++)
    if (name && ! strcmp (name, mode_names[m]))
      {
        *mode = (UpMode) m;
        return 1;
      }
  return 0;
}

const char *
up_status_name (UpStatus status)
{
  switch (status)
    {
    case UP_OK:               return "ok";
    case UP_NOT_ENOUGH_LINES: return "not-enough-lines";
    case UP_DID_NOT_CONVERGE: return "did-not-converge";
    case UP_INSANE:           return "insane";
    case UP_ERROR:            return "error";
    }
  return "?";
}

UpLineType
up_classify_line (const UpLine *line)
{
  const double dx = line->x2 - line->x1;
  const double dy = line->y2 - line->y1;

  if (! (hypot (dx, dy) > UP_MIN_LINE_LENGTH))
    return UP_LINE_IGNORED;
  /* the angle to the x axis, folded into 0..90 degrees */
  const double a = atan2 (fabs (dy), fabs (dx)) * (180.0 / M_PI);
  if (90.0 - a < UP_MAX_DEVIATION)
    return UP_LINE_VERTICAL;
  if (a < UP_MAX_DEVIATION)
    return UP_LINE_HORIZONTAL;
  return UP_LINE_IGNORED;
}

/* ------------------------------------------------------------ detection */

/* the image averaged down by an integer factor k, as doubles 0..256
 * (ashift's range for LSD) */
static double *
prepare_gray (const float *gray, int w, int h, int k, int *sw, int *sh)
{
  double *out;

  *sw = (w + k - 1) / k;
  *sh = (h + k - 1) / k;
  out = calloc ((size_t) *sw * *sh, sizeof (double));
  if (! out)
    return NULL;
  for (int sy = 0; sy < *sh; sy++)
    for (int sx = 0; sx < *sw; sx++)
      {
        double sum = 0.0;
        int    n = 0;

        for (int y = sy * k; y < sy * k + k && y < h; y++)
          for (int x = sx * k; x < sx * k + k && x < w; x++)
            {
              const float v = gray[(size_t) y * w + x];
              if (isfinite (v))
                {
                  sum += v;
                  n++;
                }
            }
        out[(size_t) sy * *sw + sx] = n ? 256.0 * sum / n : 0.0;
      }
  return out;
}

/* weighted sums for a total least squares line through points */
typedef struct
{
  double w, x, y, xx, xy, yy;
} LineSums;

static void
sums_add (LineSums *s, double x, double y, double w)
{
  s->w += w;
  s->x += w * x;
  s->y += w * y;
  s->xx += w * x * x;
  s->xy += w * x * y;
  s->yy += w * y * y;
}

/* the line of the sums: a point on it and its unit direction */
static void
sums_line (const LineSums *s, double *cx, double *cy, double *dx, double *dy)
{
  const double mx = s->x / s->w, my = s->y / s->w;
  const double sxx = s->xx / s->w - mx * mx, syy = s->yy / s->w - my * my;
  const double sxy = s->xy / s->w - mx * my;
  const double a = 0.5 * atan2 (2.0 * sxy, sxx - syy);

  *cx = mx;
  *cy = my;
  *dx = cos (a);
  *dy = sin (a);
}

/* Merges the segments of one kind (vertical or horizontal) that lie on
 * one line: the window heads of a row, the pieces of a long edge broken
 * up by LSD. Not in ashift. Why: a short segment's angle is biased by
 * the corners at its ends (measured on tests/synth.py facades: window
 * heads of about 20 pixels were off by 0.13 degrees on average, all in
 * the same direction, the long roof line by 0.003), while the line
 * through all the segments of a row is as good as a long edge. A segment
 * joins a line if its direction is within MERGE_ANGLE degrees and both
 * its end points are within tol pixels of the line fitted so far; the
 * merged line is the weighted total least squares line through the end
 * points, from the outermost projection to the other, with the summed
 * weight and the mean width. Returns the new count. */
#define MERGE_ANGLE 2.0
#define MERGE_TOLERANCE 1.5   /* pixels of the detection image */

static int
merge_collinear (UpLine *lines, int n, double tol)
{
  const double cos_max = cos (MERGE_ANGLE * M_PI / 180.0);
  int         *order, *taken, m = 0;
  UpLine      *out;

  if (n < 2)
    return n;
  order = malloc (sizeof (int) * (size_t) n);
  taken = calloc ((size_t) n, sizeof (int));
  out = malloc (sizeof (UpLine) * (size_t) n);
  if (! order || ! taken || ! out)
    {
      free (order);
      free (taken);
      free (out);
      return n;   /* (unmerged, which still works) */
    }
  /* longest first, so that a line grows from its most reliable piece */
  for (int i = 0; i < n; i++)
    order[i] = i;
  for (int i = 1; i < n; i++)
    {
      const int    k = order[i];
      const double lk = hypot (lines[k].x2 - lines[k].x1, lines[k].y2 - lines[k].y1);
      int          j = i - 1;

      while (j >= 0 && hypot (lines[order[j]].x2 - lines[order[j]].x1,
                              lines[order[j]].y2 - lines[order[j]].y1) < lk)
        {
          order[j + 1] = order[j];
          j--;
        }
      order[j + 1] = k;
    }

  for (int a = 0; a < n; a++)
    {
      const int i = order[a];
      LineSums  s = { 0 };
      double    cx, cy, dx, dy, wsum, width = 0.0, tmin = HUGE_VAL, tmax = -HUGE_VAL;
      int       members = 1;

      if (taken[i])
        continue;
      taken[i] = 1;
      out[m] = lines[i];
      if (lines[i].type == UP_LINE_IGNORED)
        {
          m++;
          continue;
        }
      sums_add (&s, lines[i].x1, lines[i].y1, 0.5 * lines[i].weight + 1e-12);
      sums_add (&s, lines[i].x2, lines[i].y2, 0.5 * lines[i].weight + 1e-12);
      sums_line (&s, &cx, &cy, &dx, &dy);
      wsum = lines[i].weight;
      width = lines[i].width * lines[i].weight;
      for (int b = a + 1; b < n; b++)
        {
          const int j = order[b];
          double    ex, ey, len, d1, d2;

          if (taken[j] || lines[j].type != lines[i].type)
            continue;
          ex = lines[j].x2 - lines[j].x1;
          ey = lines[j].y2 - lines[j].y1;
          len = hypot (ex, ey);
          if (! (len > 0.0) || fabs (ex * dx + ey * dy) / len < cos_max)
            continue;
          d1 = fabs ((lines[j].x1 - cx) * dy - (lines[j].y1 - cy) * dx);
          d2 = fabs ((lines[j].x2 - cx) * dy - (lines[j].y2 - cy) * dx);
          if (d1 > tol || d2 > tol)
            continue;
          taken[j] = 1;
          members++;
          sums_add (&s, lines[j].x1, lines[j].y1, 0.5 * lines[j].weight + 1e-12);
          sums_add (&s, lines[j].x2, lines[j].y2, 0.5 * lines[j].weight + 1e-12);
          sums_line (&s, &cx, &cy, &dx, &dy);
          wsum += lines[j].weight;
          width += lines[j].width * lines[j].weight;
          /* (remember the members by marking them with the leader) */
          taken[j] = 2 + i;
        }
      if (members > 1)
        {
          /* the extent: the outermost projections of the members' ends */
          for (int b = a; b < n; b++)
            {
              const int j = order[b];
              if (j != i && taken[j] != 2 + i)
                continue;
              for (int e = 0; e < 2; e++)
                {
                  const double px = e ? lines[j].x2 : lines[j].x1;
                  const double py = e ? lines[j].y2 : lines[j].y1;
                  const double t = (px - cx) * dx + (py - cy) * dy;
                  tmin = fmin (tmin, t);
                  tmax = fmax (tmax, t);
                }
            }
          out[m].x1 = cx + tmin * dx;
          out[m].y1 = cy + tmin * dy;
          out[m].x2 = cx + tmax * dx;
          out[m].y2 = cy + tmax * dy;
          out[m].weight = wsum;
          out[m].width = wsum > 0.0 ? width / wsum : lines[i].width;
          out[m].type = up_classify_line (&out[m]);
        }
      m++;
    }
  memcpy (lines, out, sizeof (UpLine) * (size_t) m);
  free (order);
  free (taken);
  free (out);
  return m;
}

UpStatus
up_detect_lines (const float *gray, int width, int height, UpLine **lines, int *count)
{
  UpLsdParams   lp;
  UpLsdSegment *seg = NULL;
  UpLine       *out = NULL;
  double       *img;
  int           nseg = 0, n = 0, sw, sh, k;

  if (! gray || width <= 0 || height <= 0 || ! lines || ! count)
    return UP_ERROR;
  *lines = NULL;
  *count = 0;
  if (width < 3 || height < 3)
    return UP_OK;
  k = (int) ceil ((double) (width > height ? width : height) / UP_DETECT_MAX_SIDE);
  if (k < 1)
    k = 1;
  img = prepare_gray (gray, width, height, k, &sw, &sh);
  if (! img)
    return UP_ERROR;
  up_lsd_defaults (&lp);
  if (up_lsd (img, sw, sh, &lp, &seg, &nseg))
    {
      free (img);
      return UP_ERROR;
    }
  free (img);
  if (nseg > 0)
    {
      out = malloc (sizeof (UpLine) * (size_t) nseg);
      if (! out)
        {
          free (seg);
          return UP_ERROR;
        }
    }
  for (int i = 0; i < nseg; i++)
    {
      const UpLsdSegment *s = &seg[i];
      UpLine             *l = &out[n];

      /* segments along the border are artefacts (ashift drops those
       * within 2 pixels of it and parallel to it; its coordinates are
       * pixel centres, ours corners, hence the half pixel) */
      if ((fabs (s->x1 - s->x2) < 1 && fmax (s->x1, s->x2) < 2.5) ||
          (fabs (s->x1 - s->x2) < 1 && fmin (s->x1, s->x2) > sw - 2.5) ||
          (fabs (s->y1 - s->y2) < 1 && fmax (s->y1, s->y2) < 2.5) ||
          (fabs (s->y1 - s->y2) < 1 && fmin (s->y1, s->y2) > sh - 2.5))
        continue;
      l->x1 = s->x1 * k;
      l->y1 = s->y1 * k;
      l->x2 = s->x2 * k;
      l->y2 = s->y2 * k;
      l->width = s->width * k;
      l->weight = hypot (l->x2 - l->x1, l->y2 - l->y1) * l->width * s->p;
      l->type = up_classify_line (l);
      l->outlier = 0;
      n++;
    }
  free (seg);
  n = merge_collinear (out, n, MERGE_TOLERANCE * k);
  if (n == 0)
    {
      free (out);
      out = NULL;
    }
  *lines = out;
  *count = n;
  return UP_OK;
}

/* ------------------------------------------------------------ the fit */

enum
{
  FIT_ROTATION = 1 << 0,
  FIT_SHIFT_V  = 1 << 1,
  FIT_SHIFT_H  = 1 << 2,
  FIT_SHEAR    = 1 << 3
};

typedef struct
{
  const UpPrepLine *lines;
  int               count;
  int               width, height;
  const UpLens     *lens;
  int               which;   /* FIT_* */
} FitData;

static const double ranges[4] = { UP_ROTATION_RANGE, UP_SHIFT_RANGE, UP_SHIFT_RANGE, UP_SHEAR_RANGE };

static void
unpack (const FitData *d, const double *x, UpParams *p)
{
  double *v[4] = { &p->rotation, &p->shift_v, &p->shift_h, &p->shear };
  int     k = 0;

  memset (p, 0, sizeof *p);
  for (int i = 0; i < 4; i++)
    if (d->which & (1 << i))
      {
        *v[i] = up__ilogit (x[k], -ranges[i], ranges[i]);
        k++;
      }
}

static double
fitness (const double *x, void *data)
{
  const FitData *d = data;
  UpParams       p;
  double         h[9];

  unpack (d, x, &p);
  up__homography (&p, d->lens, d->width, d->height, 0, h);
  return up__cost_matrix (d->lines, d->count, h);
}

/* the area of the bounding box of the transformed image, as a multiple
 * of the image's */
static double
area_growth (const double h[9], int w, int ht)
{
  double xm = DBL_MAX, xM = -DBL_MAX, ym = DBL_MAX, yM = -DBL_MAX;

  for (int j = 0; j < 2; j++)
    for (int i = 0; i < 2; i++)
      {
        double x, y;
        if (! up_matrix_apply (h, i ? w : 0, j ? ht : 0, &x, &y))
          return HUGE_VAL;
        xm = fmin (xm, x);
        xM = fmax (xM, x);
        ym = fmin (ym, y);
        yM = fmax (yM, y);
      }
  return (xM - xm) * (yM - ym) / ((double) w * ht);
}

static void
mode_setup (UpMode mode, int *which, int *kinds)
{
  switch (mode)
    {
    case UP_MODE_VERTICAL:   *which = FIT_ROTATION | FIT_SHIFT_V; *kinds = 1; break;
    case UP_MODE_HORIZONTAL: *which = FIT_ROTATION | FIT_SHIFT_H; *kinds = 2; break;
    case UP_MODE_FULL:       *which = FIT_ROTATION | FIT_SHIFT_V | FIT_SHIFT_H | FIT_SHEAR; *kinds = 3; break;
    case UP_MODE_ROTATION:
    default:                 *which = FIT_ROTATION; *kinds = 3; break;
    }
}

/* one fit of a definite mode (not auto) */
static UpStatus
fit_mode (const UpLine *lines, int count, int width, int height, UpMode mode,
          const UpLens *lens, int nv, int nh, UpParams *params, UpFitResult *r)
{
  UpPrepLine *prep;
  FitData     d;
  UpParams    zero = { 0 };
  double      x[4], h[9];
  int         kinds, n, iter, np = 0;

  memset (params, 0, sizeof *params);
  r->mode_used = mode;
  r->iterations = 0;
  r->cost_before = r->cost_after = 0.0;
  if ((mode == UP_MODE_VERTICAL && nv < UP_MIN_FIT_LINES) ||
      (mode == UP_MODE_HORIZONTAL && nh < UP_MIN_FIT_LINES) ||
      (mode == UP_MODE_FULL && (nv < UP_MIN_FIT_LINES || nh < UP_MIN_FIT_LINES)) ||
      (mode == UP_MODE_ROTATION && nv + nh < 1))
    return UP_NOT_ENOUGH_LINES;

  mode_setup (mode, &d.which, &kinds);
  n = up__prepare_lines (lines, count, kinds, &prep);
  if (n < 0)
    return UP_ERROR;
  d.lines = prep;
  d.count = n;
  d.width = width;
  d.height = height;
  d.lens = lens;
  for (int i = 0; i < 4; i++)
    if (d.which & (1 << i))
      x[np++] = up__logit (0.0, -ranges[i], ranges[i]);

  up__homography (&zero, lens, width, height, 0, h);
  r->cost_before = up__cost_matrix (prep, n, h);
  iter = up__simplex (fitness, x, np, NMS_EPSILON, NMS_SCALE, NMS_ITERATIONS, NULL, &d);
  r->iterations = iter;
  if (iter >= NMS_ITERATIONS)
    {
      free (prep);
      return UP_DID_NOT_CONVERGE;
    }
  unpack (&d, x, params);
  up__homography (params, lens, width, height, 0, h);
  r->cost_after = up__cost_matrix (prep, n, h);
  free (prep);

  /* ashift's sanity check: more than 4 times the area is insane */
  if (! (area_growth (h, width, height) <= 4.0))
    {
      memset (params, 0, sizeof *params);
      return UP_INSANE;
    }
  return UP_OK;
}

/* the share of the image the largest crop of a fit keeps */
static double
crop_share (const UpParams *p, const UpLens *lens, int w, int h)
{
  double m[9];
  UpRect r;

  if (up_homography (p, lens, w, h, m) != UP_OK || up_crop (m, w, h, UP_CROP_LARGEST, &r) != UP_OK)
    return 0.0;
  return (double) r.width * r.height / ((double) w * h);
}

/* the weighted median deviation (degrees) of the lines of a kind (1
 * vertical, 2 horizontal) that are no outliers, after a fit; 0 without
 * such lines */
static double
residual (const UpLine *lines, int count, int width, int height, const UpLens *lens,
          const UpParams *p, int kind)
{
  double  m[9], *dev, *wgt, total = 0.0, acc = 0.0, med = 0.0;
  int     n = 0;

  if (up_homography (p, lens, width, height, m) != UP_OK)
    return HUGE_VAL;
  dev = malloc (sizeof (double) * 2 * (size_t) (count > 0 ? count : 1));
  if (! dev)
    return HUGE_VAL;
  wgt = dev + (count > 0 ? count : 1);
  for (int i = 0; i < count; i++)
    {
      const UpLine *l = &lines[i];
      double        x1, y1, x2, y2, dx, dy, d;
      int           j;

      if (l->outlier || (int) l->type != kind)
        continue;
      if (! up_matrix_apply (m, l->x1, l->y1, &x1, &y1) || ! up_matrix_apply (m, l->x2, l->y2, &x2, &y2))
        {
          free (dev);
          return HUGE_VAL;
        }
      dx = fabs (x2 - x1);
      dy = fabs (y2 - y1);
      d = kind == UP_LINE_VERTICAL ? atan2 (dx, dy) : atan2 (dy, dx);
      /* insertion by deviation (the counts are a few hundred) */
      for (j = n; j > 0 && dev[j - 1] > d; j--)
        {
          dev[j] = dev[j - 1];
          wgt[j] = wgt[j - 1];
        }
      dev[j] = d;
      wgt[j] = l->weight;
      total += l->weight;
      n++;
    }
  for (int i = 0; i < n; i++)
    {
      acc += wgt[i];
      med = dev[i];
      if (acc >= 0.5 * total)
        break;
    }
  free (dev);
  return med * 180.0 / M_PI;
}

/* whether the lines a fit of the automatic mode used agree after it
 * (AUTO_MAX_RESIDUAL) */
static int
lines_agree (const UpLine *lines, int count, int width, int height, const UpLens *lens,
             const UpParams *p, UpMode mode)
{
  int which, kinds;

  mode_setup (mode, &which, &kinds);
  for (int kind = 1; kind <= 2; kind++)
    if ((kinds & kind) && ! (residual (lines, count, width, height, lens, p, kind) < AUTO_MAX_RESIDUAL))
      return 0;
  return 1;
}

UpMode
up_mode_for_lines (const UpLine *lines, int count)
{
  int nv = 0, nh = 0;

  for (int i = 0; lines && i < count; i++)
    if (! lines[i].outlier)
      {
        nv += lines[i].type == UP_LINE_VERTICAL;
        nh += lines[i].type == UP_LINE_HORIZONTAL;
      }
  if (nv >= UP_MIN_FIT_LINES && nh >= UP_MIN_FIT_LINES)
    return UP_MODE_FULL;
  if (nv >= UP_MIN_FIT_LINES)
    return UP_MODE_VERTICAL;
  if (nh >= UP_MIN_FIT_LINES)
    return UP_MODE_HORIZONTAL;
  return UP_MODE_ROTATION;
}

UpStatus
up_fit (const UpLine *lines, int count, int width, int height, UpMode mode,
        const UpLens *lens, UpParams *params, UpFitResult *result)
{
  UpFitResult r = { 0 };
  UpLens      generic;
  int         nv = 0, nh = 0;

  if (count < 0 || (count > 0 && ! lines) || width <= 0 || height <= 0 ||
      mode < 0 || mode >= UP_MODE_COUNT || ! params)
    return UP_ERROR;
  memset (params, 0, sizeof *params);
  if (! lens)
    {
      up_lens_generic (&generic);
      lens = &generic;
    }
  if (! (isfinite (lens->focal) && lens->focal > 0.0 && lens->aspect > 0.0 &&
         lens->orthocorr >= 0.0 && lens->orthocorr <= 100.0))
    return UP_ERROR;

  for (int i = 0; i < count; i++)
    if (! lines[i].outlier)
      {
        nv += lines[i].type == UP_LINE_VERTICAL;
        nh += lines[i].type == UP_LINE_HORIZONTAL;
      }

  if (mode != UP_MODE_AUTO)
    r.status = fit_mode (lines, count, width, height, mode, lens, nv, nh, params, &r);
  else
    {
      /* Vertical, unless Full is clearly better on all lines and keeps
       * enough of the image; Horizontal without enough vertical lines;
       * Rotation only with too few of both. A fit whose lines do not
       * agree afterwards (lines_agree) is passed over for the next one;
       * if none is left, "not enough lines". */
      int done = 0;

      r.status = UP_NOT_ENOUGH_LINES;
      r.mode_used = UP_MODE_VERTICAL;
      if (nv >= UP_MIN_FIT_LINES)
        {
          UpParams    pv;
          UpFitResult rv = { 0 };
          UpStatus    sv = fit_mode (lines, count, width, height, UP_MODE_VERTICAL, lens, nv, nh, &pv, &rv);
          int         v_ok = sv == UP_OK && lines_agree (lines, count, width, height, lens, &pv, UP_MODE_VERTICAL);

          if (nh >= UP_MIN_FIT_LINES)
            {
              UpParams    pf;
              UpFitResult rf = { 0 };
              UpStatus    sf = fit_mode (lines, count, width, height, UP_MODE_FULL, lens, nv, nh, &pf, &rf);

              if (sf == UP_OK && lines_agree (lines, count, width, height, lens, &pf, UP_MODE_FULL))
                {
                  const double cv = v_ok ? up_cost (lines, count, width, height, lens, &pv, 3) : HUGE_VAL;
                  const double cf = up_cost (lines, count, width, height, lens, &pf, 3);

                  if (cf >= 0.0 && cf < AUTO_COST_RATIO * cv &&
                      crop_share (&pf, lens, width, height) >= AUTO_MIN_AREA)
                    {
                      *params = pf;
                      r = rf;
                      r.status = sf;
                      done = 1;
                    }
                }
            }
          if (! done && v_ok)
            {
              *params = pv;
              r = rv;
              r.status = sv;
              done = 1;
            }
          else if (! done && sv != UP_OK)
            {
              /* (a Vertical fit that failed outright reports why) */
              r = rv;
              r.status = sv;
            }
        }
      if (! done && nv < UP_MIN_FIT_LINES && nh >= UP_MIN_FIT_LINES)
        {
          UpParams    ph;
          UpFitResult rh = { 0 };
          UpStatus    sh = fit_mode (lines, count, width, height, UP_MODE_HORIZONTAL, lens, nv, nh, &ph, &rh);

          r = rh;
          r.status = sh;
          if (sh == UP_OK && lines_agree (lines, count, width, height, lens, &ph, UP_MODE_HORIZONTAL))
            {
              *params = ph;
              done = 1;
            }
          else if (sh == UP_OK)
            r.status = UP_NOT_ENOUGH_LINES;
        }
      if (! done && nv < UP_MIN_FIT_LINES && nh < UP_MIN_FIT_LINES)
        {
          UpParams    pr;
          UpFitResult rr = { 0 };
          UpStatus    sr = fit_mode (lines, count, width, height, UP_MODE_ROTATION, lens, nv, nh, &pr, &rr);

          r = rr;
          r.status = sr;
          if (sr == UP_OK && lines_agree (lines, count, width, height, lens, &pr, UP_MODE_ROTATION))
            {
              *params = pr;
              done = 1;
            }
          else if (sr == UP_OK)
            r.status = UP_NOT_ENOUGH_LINES;
        }
      if (! done && r.status == UP_OK)
        r.status = UP_NOT_ENOUGH_LINES;
    }

  if (r.status != UP_OK)
    memset (params, 0, sizeof *params);
  {
    /* the lines that took part */
    int which, kinds;
    mode_setup (r.mode_used, &which, &kinds);
    r.vertical = (kinds & 1) ? nv : 0;
    r.horizontal = (kinds & 2) ? nh : 0;
  }
  if (result)
    *result = r;
  return r.status;
}
