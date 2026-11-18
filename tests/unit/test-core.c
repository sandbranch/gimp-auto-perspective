/* unit tests of the core: classification and names, LSD on synthetic
 * edges, the homography against darktable's ashift (tests/ref), the
 * cost, Nelder-Mead, the fit on exact lines, RANSAC, the crop against
 * closed forms, and the warp. The recovery on rendered facades is
 * tests/recovery.py.
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "perspective.h"
#include "internal.h"
#include "lsd.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tests/ref/ashift-homography.c */
void ref_ashift_homography (float *homograph, float angle, float shift_v, float shift_h,
                            float shear, float f_length_kb, float orthocorr, float aspect,
                            int width, int height);

static int failures, passes;

static void __attribute__ ((format (printf, 2, 3)))
check (int ok, const char *fmt, ...)
{
  va_list ap;

  printf ("%s ", ok ? "PASS" : "FAIL");
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  printf ("\n");
  ok ? passes++ : failures++;
}

/* a segment of that length through (x, y) at that angle to the x axis */
static UpLine
seg (double x, double y, double len, double deg)
{
  const double a = deg * M_PI / 180.0;
  UpLine l = { x - 0.5 * len * cos (a), y - 0.5 * len * sin (a),
               x + 0.5 * len * cos (a), y + 0.5 * len * sin (a), 1.0, 1.0, UP_LINE_IGNORED, 0 };
  return l;
}

static double
seg_angle (double x1, double y1, double x2, double y2)
{
  return atan2 (y2 - y1, x2 - x1) * 180.0 / M_PI;
}

/* ------------------------------------------------------------ names and classes */

static void
test_classify (void)
{
  static const struct { double len, deg; UpLineType want; } cases[] = {
    { 100, 90, UP_LINE_VERTICAL },   { 100, -90, UP_LINE_VERTICAL },
    { 100, 61, UP_LINE_VERTICAL },   { 100, 119, UP_LINE_VERTICAL },
    { 100, 0, UP_LINE_HORIZONTAL },  { 100, 180, UP_LINE_HORIZONTAL },
    { 100, 29, UP_LINE_HORIZONTAL }, { 100, -29, UP_LINE_HORIZONTAL },
    { 100, 45, UP_LINE_IGNORED },    { 100, 31, UP_LINE_IGNORED },
    { 100, 59, UP_LINE_IGNORED },    { 4.9, 90, UP_LINE_IGNORED },
    { 0, 0, UP_LINE_IGNORED },
  };
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++)
    {
      UpLine l = seg (50, 50, cases[i].len, cases[i].deg);
      UpLineType t = up_classify_line (&l);
      check (t == cases[i].want, "classify: length %g at %g degrees is %d (want %d)",
             cases[i].len, cases[i].deg, t, cases[i].want);
    }
  UpLine nan_line = { NAN, 0, 10, 10, 1, 1, UP_LINE_IGNORED, 0 };
  check (up_classify_line (&nan_line) == UP_LINE_IGNORED, "classify: NaN end point is ignored");
}

static void
test_names (void)
{
  int ok = 1;
  for (int m = 0; m < UP_MODE_COUNT; m++)
    {
      UpMode back;
      ok &= up_mode_from_name (up_mode_name ((UpMode) m), &back) && back == (UpMode) m;
    }
  check (ok, "mode names: every mode survives name and back");
  UpMode dummy;
  check (! up_mode_from_name ("sideways", &dummy) && ! up_mode_from_name (NULL, &dummy),
         "mode names: unknown and NULL names are refused");
  check (! strcmp (up_status_name (UP_NOT_ENOUGH_LINES), "not-enough-lines"), "status names");
  ok = 1;
  for (int c = 0; c < UP_CROP_COUNT; c++)
    {
      UpCrop back;
      ok &= up_crop_from_name (up_crop_name ((UpCrop) c), &back) && back == (UpCrop) c;
    }
  UpCrop cdummy;
  check (ok && ! up_crop_from_name ("round", &cdummy), "crop names: name and back, unknown refused");
}

/* ------------------------------------------------------------ LSD */

/* a straight edge, anti-aliased (8x8 samples per pixel): dark left of the
 * line through (x0, y0) at that angle to the y axis (degrees), light
 * right of it */
static float *
edge_image (int w, int h, double x0, double y0, double deg)
{
  float       *img = malloc (sizeof (float) * w * h);
  const double a = deg * M_PI / 180.0, nx = cos (a), ny = -sin (a);

  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      {
        int n = 0;
        for (int sy = 0; sy < 8; sy++)
          for (int sx = 0; sx < 8; sx++)
            {
              const double px = x + (sx + 0.5) / 8.0, py = y + (sy + 0.5) / 8.0;
              n += (px - x0) * nx + (py - y0) * ny > 0.0;
            }
        img[y * w + x] = 0.2f + 0.6f * n / 64.0f;
      }
  return img;
}

static void
test_lsd (void)
{
  static const struct { double x0, deg; } edges[] = { { 30.0, 0.0 }, { 40.5, 0.0 }, { 50.0, 10.0 },
                                                     { 60.25, -7.0 } };
  const int w = 97, h = 81;

  for (size_t e = 0; e < sizeof edges / sizeof *edges; e++)
    {
      float        *img = edge_image (w, h, edges[e].x0, 0.5 * h, edges[e].deg);
      double       *d = malloc (sizeof (double) * w * h);
      UpLsdSegment *s = NULL;
      int           n = -1, best = -1;

      for (int i = 0; i < w * h; i++)
        d[i] = 256.0 * img[i];
      {
        const int st = up_lsd (d, w, h, NULL, &s, &n);
        check (st == 0 && n >= 1, "lsd: an edge at x %g, %g degrees: %d segment(s)", edges[e].x0,
               edges[e].deg, n);
      }
      for (int i = 0; i < n; i++)
        if (best < 0 || hypot (s[i].x2 - s[i].x1, s[i].y2 - s[i].y1) >
                        hypot (s[best].x2 - s[best].x1, s[best].y2 - s[best].y1))
          best = i;
      if (best >= 0)
        {
          const UpLsdSegment *b = &s[best];
          /* the angle against the y axis, and where the segment crosses
           * the middle row, against the truth */
          double a = 90.0 - seg_angle (b->x1, b->y1, b->x2, b->y2);
          while (a > 90.0)
            a -= 180.0;
          while (a < -90.0)
            a += 180.0;
          const double t = (0.5 * h - b->y1) / (b->y2 - b->y1);
          const double xm = b->x1 + t * (b->x2 - b->x1);
          /* (an edge of angle deg leans right going down: x grows with y) */
          check (fabs (a - edges[e].deg) < 0.1 && fabs (xm - edges[e].x0) < 1.0 &&
                 fabs (b->y2 - b->y1) > 0.8 * h,
                 "lsd: the edge found at x %.3f (truth %g), %.4f degrees (truth %g), %.1f pixels long",
                 xm, edges[e].x0, a, edges[e].deg, hypot (b->x2 - b->x1, b->y2 - b->y1));
        }
      free (s);
      free (d);
      free (img);
    }

  /* flat and noise: nothing, and bad arguments */
  {
    double       *d = malloc (sizeof (double) * w * h);
    UpLsdSegment *s = (UpLsdSegment *) 1;
    int           n = -1;
    unsigned      r = 12345;

    for (int i = 0; i < w * h; i++)
      d[i] = 128.0;
    check (up_lsd (d, w, h, NULL, &s, &n) == 0 && n == 0 && s == NULL, "lsd: a flat image has no segments");
    for (int i = 0; i < w * h; i++)
      {
        r = r * 1103515245u + 12345u;
        d[i] = (r >> 16) % 256;
      }
    {
      const int st = up_lsd (d, w, h, NULL, &s, &n);
      check (st == 0 && n <= 1, "lsd: uniform noise gives (almost) no segments: %d (the NFA test)", n);
    }
    free (s);
    check (up_lsd (NULL, w, h, NULL, &s, &n) == -1 && up_lsd (d, 1, h, NULL, &s, &n) == -1,
           "lsd: bad arguments give -1");
    free (d);
  }
}

static void
test_detect (void)
{
  const int w = 64, h = 48;
  float    *img = malloc (sizeof (float) * w * h);
  UpLine   *lines = (UpLine *) 1;
  int       n = -1, nv = 0, nh = 0;

  /* a vertical and a horizontal edge */
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      img[y * w + x] = (x >= 30) ^ (y >= 20) ? 0.9f : 0.1f;
  {
    const UpStatus st = up_detect_lines (img, w, h, &lines, &n);
    check (st == UP_OK && n >= 2 && lines, "detect: runs, %d lines", n);
  }
  for (int i = 0; i < n; i++)
    {
      if (lines[i].type == UP_LINE_VERTICAL && fabs (lines[i].x1 - 30.0) < 1.0 && fabs (lines[i].x2 - 30.0) < 1.0)
        nv++;
      if (lines[i].type == UP_LINE_HORIZONTAL && fabs (lines[i].y1 - 20.0) < 1.0 && fabs (lines[i].y2 - 20.0) < 1.0)
        nh++;
    }
  /* (each edge flips its contrast in the middle: LSD sees two segments
   * of opposite gradient; merging joins only those of one kind on one
   * line, so one or two) */
  check (nv >= 1 && nh >= 1, "detect: the vertical edge at x 30 (%d) and the horizontal one at y 20 (%d)", nv, nh);
  check (up_detect_lines (NULL, w, h, &lines, &n) == UP_ERROR, "detect: no image gives UP_ERROR");
  free (lines);
  free (img);

  /* a large image is scaled down for detection and the lines scaled back */
  {
    const int W = 3001, H = 1700;
    float    *big = malloc (sizeof (float) * W * H);
    int       found = 0;

    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
        big[(size_t) y * W + x] = x >= 1200 ? 0.8f : 0.2f;
    lines = NULL;
    check (up_detect_lines (big, W, H, &lines, &n) == UP_OK, "detect: a %dx%d image", W, H);
    for (int i = 0; i < n; i++)
      found += lines[i].type == UP_LINE_VERTICAL && fabs (lines[i].x1 - 1200.0) < 2.0 &&
               fabs (lines[i].x2 - 1200.0) < 2.0 && fabs (lines[i].y2 - lines[i].y1) > 0.9 * H;
    check (found == 1, "detect: on the large image the edge at x 1200 is found once, in full coordinates");
    free (lines);
    free (big);
  }
}

/* ------------------------------------------------------------ the model */

static void
test_homography (void)
{
  const UpParams zero = { 0 };
  UpLens         generic;
  double         m[9], inv[9], mi[9];

  up_lens_generic (&generic);
  check (up_homography (&zero, NULL, 640, 480, m) == UP_OK &&
         m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 1 && m[5] == 0 &&
         m[6] == 0 && m[7] == 0 && m[8] == 1, "homography: neutral parameters give the identity");
  check (up_homography (&zero, NULL, 0, 480, m) == UP_ERROR, "homography: bad size gives UP_ERROR");
  {
    UpParams bad = { NAN, 0, 0, 0 };
    UpLens   badlens = { -1, 0, 1 };
    check (up_homography (&bad, NULL, 64, 48, m) == UP_ERROR &&
           up_homography (&zero, &badlens, 64, 48, m) == UP_ERROR,
           "homography: NaN parameters and a bad lens give UP_ERROR");
  }

  /* each parameter alone, and all together, against ashift's float code */
  {
    static const struct { UpParams p; UpLens lens; int w, h; } cases[] = {
      { { 3.0, 0, 0, 0 }, { 28, 0, 1 }, 640, 480 },
      { { -7.5, 0, 0, 0 }, { 28, 0, 1 }, 333, 517 },
      { { 0, 0.3, 0, 0 }, { 28, 0, 1 }, 640, 480 },
      { { 0, -0.6, 0, 0 }, { 28, 0, 1 }, 480, 640 },
      { { 0, 0, 0.25, 0 }, { 28, 0, 1 }, 640, 480 },
      { { 0, 0, -0.45, 0 }, { 28, 0, 1 }, 401, 401 },
      { { 0, 0, 0, 0.1 }, { 28, 0, 1 }, 640, 480 },
      { { 0, 0, 0, -0.15 }, { 28, 0, 1 }, 640, 480 },
      { { 2.0, 0.2, -0.1, 0.05 }, { 28, 0, 1 }, 6000, 4000 },
      { { -1.0, -0.3, 0.2, -0.02 }, { 50, 40, 1.2 }, 4000, 6000 },
      { { 0.5, 0.4, 0.3, 0.0 }, { 18, 100, 0.9 }, 1024, 683 },
    };
    double worst = 0.0;
    int    ok = 1;

    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++)
      {
        float  ref[9];
        double ours[9], scale = 0.0;

        ref_ashift_homography (ref, cases[i].p.rotation, cases[i].p.shift_v, cases[i].p.shift_h,
                               cases[i].p.shear, cases[i].lens.focal, cases[i].lens.orthocorr,
                               cases[i].lens.aspect, cases[i].w, cases[i].h);
        up__homography (&cases[i].p, &cases[i].lens, cases[i].w, cases[i].h, 1, ours);
        /* compared as maps: the corners and the centre through both, in
         * pixels relative to the image size (the matrices are both
         * normalised with m[8] near 1, but a float matrix has relative
         * errors of 1e-7 per entry) */
        for (int k = 0; k < 9; k++)
          scale = fmax (scale, fabs (ours[k]));
        for (int j = 0; j < 5; j++)
          {
            const double px = j == 4 ? 0.5 * cases[i].w : (j & 1) ? cases[i].w - 1 : 0;
            const double py = j == 4 ? 0.5 * cases[i].h : (j & 2) ? cases[i].h - 1 : 0;
            const double rw = ref[6] * px + ref[7] * py + ref[8];
            const double rx = (ref[0] * px + ref[1] * py + ref[2]) / rw;
            const double ry = (ref[3] * px + ref[4] * py + ref[5]) / rw;
            double ox, oy, e;

            ok &= up_matrix_apply (ours, px, py, &ox, &oy);
            e = hypot (ox - rx, oy - ry) / fmax (cases[i].w, cases[i].h);
            worst = fmax (worst, e);
          }
      }
    check (ok && worst < 1e-6,
           "homography: as ashift's (float) for each parameter alone and together, %zu cases: "
           "largest difference of a mapped corner %.2g of the image size (target 1e-6)",
           sizeof cases / sizeof *cases, worst);
  }

  /* our translation: the transformed image starts at (0, 0); the inverse */
  {
    const UpParams p = { 2.5, 0.3, -0.2, 0.04 };
    double         xmin = 1e9, ymin = 1e9, err = 0.0;

    up_homography (&p, NULL, 640, 480, m);
    for (int j = 0; j < 4; j++)
      {
        double x, y;
        up_matrix_apply (m, (j & 1) ? 640 : 0, (j & 2) ? 480 : 0, &x, &y);
        xmin = fmin (xmin, x);
        ymin = fmin (ymin, y);
      }
    check (fabs (xmin) < 1e-9 && fabs (ymin) < 1e-9,
           "homography: the transformed image's bounding box starts at (0, 0) (%.2g, %.2g)", xmin, ymin);
    check (up_matrix_invert (m, inv), "homography: invertible");
    up__mat3mul (mi, m, inv);
    for (int k = 0; k < 9; k++)
      err = fmax (err, fabs (mi[k] - (k % 4 == 0 ? 1.0 : 0.0)));
    check (err < 1e-12, "homography: matrix times its inverse is the identity within %.2g", err);
    for (int k = 0; k < 9; k++)
      mi[k] = 0.0;
    check (! up_matrix_invert (mi, inv), "matrix: a singular one is refused");
  }

  /* the rotation's sign: positive turns the image counter-clockwise on
   * the screen (the point right of the centre moves up, y down) */
  {
    const UpParams p = { 10.0, 0, 0, 0 };
    double         cx, cy, rx, ry;

    up_homography (&p, NULL, 200, 100, m);
    up_matrix_apply (m, 100, 50, &cx, &cy);
    up_matrix_apply (m, 150, 50, &rx, &ry);
    check (ry < cy - 8.0 && fabs (atan2 (cy - ry, rx - cx) * 180.0 / M_PI - 10.0) < 1e-9,
           "homography: rotation 10 turns counter-clockwise by 10 degrees");
  }
  (void) generic;
}

/* lines that the given parameters make vertical (and horizontal): exact
 * vertical and horizontal segments in the output, mapped back through
 * the inverse */
static int
lines_for (const UpParams *p, int w, int h, int nv, int nh, UpLine *out)
{
  double m[9], inv[9];
  int    n = 0;

  up_homography (p, NULL, w, h, m);
  up_matrix_invert (m, inv);
  {
    double xs[4], ys[4], x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (int j = 0; j < 4; j++)
      {
        up_matrix_apply (m, (j & 1) ? w : 0, (j & 2) ? h : 0, &xs[j], &ys[j]);
        x0 = fmin (x0, xs[j]);
        x1 = fmax (x1, xs[j]);
        y0 = fmin (y0, ys[j]);
        y1 = fmax (y1, ys[j]);
      }
    for (int i = 0; i < nv + nh; i++)
      {
        const int    v = i < nv;
        const double t = (i < nv ? i + 1.0 : i - nv + 1.0) / ((v ? nv : nh) + 1.0);
        double       ax, ay, bx, by;

        if (v)
          {
            const double x = x0 + (0.25 + 0.5 * t) * (x1 - x0);
            up_matrix_apply (inv, x, y0 + 0.3 * (y1 - y0), &ax, &ay);
            up_matrix_apply (inv, x, y0 + 0.7 * (y1 - y0), &bx, &by);
          }
        else
          {
            const double y = y0 + (0.25 + 0.5 * t) * (y1 - y0);
            up_matrix_apply (inv, x0 + 0.3 * (x1 - x0), y, &ax, &ay);
            up_matrix_apply (inv, x0 + 0.7 * (x1 - x0), y, &bx, &by);
          }
        out[n] = (UpLine) { ax, ay, bx, by, 1.0, 1.0, UP_LINE_IGNORED, 0 };
        out[n].weight = hypot (bx - ax, by - ay);
        out[n].type = up_classify_line (&out[n]);
        n++;
      }
  }
  return n;
}

static void
test_cost (void)
{
  UpLine         lines[8];
  const UpParams zero = { 0 };

  lines[0] = seg (20, 50, 80, 90);
  lines[1] = seg (70, 50, 80, 90);
  lines[2] = seg (50, 20, 80, 0);
  for (int i = 0; i < 3; i++)
    lines[i].type = up_classify_line (&lines[i]);
  check (up_cost (lines, 3, 100, 100, NULL, &zero, 3) == 0.0, "cost: zero for perfect lines");
  /* one vertical line tilted by 1 degree: s = sin (1 degree), cost
   * sqrt (s^2) * 1e6 */
  lines[0] = seg (50, 50, 80, 91);
  lines[0].type = up_classify_line (&lines[0]);
  {
    const double c = up_cost (lines, 1, 100, 100, NULL, &zero, 1);
    check (fabs (c - 1e6 * sin (M_PI / 180.0)) < 1e-6,
           "cost: a vertical line 1 degree off costs 1e6 sin (1 degree) = %.6f (got %.6f)",
           1e6 * sin (M_PI / 180.0), c);
  }
  /* two kinds: v and h each weighted by their share */
  lines[1] = seg (50, 50, 80, 2.0);
  lines[1].type = up_classify_line (&lines[1]);
  {
    const double s1 = sin (M_PI / 180.0), s2 = sin (2.0 * M_PI / 180.0);
    const double v = s1 * s1 * 0.5, h = s2 * s2 * 0.5;
    const double want = sqrt (1.0 - (1.0 - v) * (1.0 - h)) * 1e6;
    const double c = up_cost (lines, 2, 100, 100, NULL, &zero, 3);
    check (fabs (c - want) < 1e-6, "cost: one vertical 1 degree and one horizontal 2 degrees off: %.6f (got %.6f)",
           want, c);
  }
  lines[1].outlier = 1;
  check (fabs (up_cost (lines, 2, 100, 100, NULL, &zero, 3) - 1e6 * sin (M_PI / 180.0)) < 1e-6,
         "cost: outliers do not count");
  check (up_cost (lines, 2, 0, 100, NULL, &zero, 3) < 0.0 && up_cost (lines, 2, 100, 100, NULL, &zero, 4) < 0.0,
         "cost: bad arguments give a negative value");

  /* the parameters that made the lines give cost 0 */
  {
    const UpParams p = { 1.5, 0.2, -0.15, 0.03 };
    int            n = lines_for (&p, 300, 200, 4, 4, lines);
    check (up_cost (lines, n, 300, 200, NULL, &p, 3) < 1e-3 && up_cost (lines, n, 300, 200, NULL, &zero, 3) > 1000.0,
           "cost: zero (%.2g) for the parameters that straighten the lines, large (%.0f) without",
           up_cost (lines, n, 300, 200, NULL, &p, 3), up_cost (lines, n, 300, 200, NULL, &zero, 3));
  }
}

static double
rosenbrock (const double *x, void *data)
{
  (void) data;
  return 100.0 * (x[1] - x[0] * x[0]) * (x[1] - x[0] * x[0]) + (1.0 - x[0]) * (1.0 - x[0]);
}

static double
bowl4 (const double *x, void *data)
{
  const double *c = data;
  double        s = 0.0;
  for (int i = 0; i < 4; i++)
    s += (i + 1) * (x[i] - c[i]) * (x[i] - c[i]);
  return s;
}

static void
test_simplex (void)
{
  double x[4] = { -1.2, 1.0 };
  int    it = up__simplex (rosenbrock, x, 2, 1e-12, 1.0, 2000, NULL, NULL);

  check (it <= 2000 && fabs (x[0] - 1.0) < 1e-3 && fabs (x[1] - 1.0) < 1e-3,
         "simplex: Rosenbrock from (-1.2, 1) to (%.6f, %.6f) in %d iterations", x[0], x[1], it);
  {
    double c[4] = { 0.3, -2.0, 1.5, 4.0 }, y[4] = { 0 }, e = 0.0;
    it = up__simplex (bowl4, y, 4, 1e-14, 1.0, 5000, NULL, c);
    for (int i = 0; i < 4; i++)
      e = fmax (e, fabs (y[i] - c[i]));
    check (it <= 5000 && e < 1e-4, "simplex: a 4 dimensional bowl to within %.2g in %d iterations", e, it);
  }
  x[0] = -1.2;
  x[1] = 1.0;
  check (up__simplex (rosenbrock, x, 2, 1e-30, 1.0, 10, NULL, NULL) == 11,
         "simplex: reports maxiter + 1 when it does not converge");
  {
    int ok = 1;
    for (double v = -9.99; v < 10.0; v += 0.37)
      ok &= fabs (up__ilogit (up__logit (v, -10, 10), -10, 10) - v) < 1e-9;
    check (ok, "logit: ilogit (logit (x)) = x inside the range");
    check (isfinite (up__logit (10.0, -10, 10)) && isfinite (up__logit (-10.0, -10, 10)) &&
           isfinite (up__logit (50.0, -10, 10)),
           "logit: finite at and beyond the bounds");
    check (up__ilogit (1e6, -10, 10) <= 10.0 && up__ilogit (-1e6, -10, 10) >= -10.0 &&
           up__ilogit (0.0, -10, 10) == 0.0,
           "logit: ilogit stays inside the range, 0 maps to the middle");
  }
}

/* ------------------------------------------------------------ the fit */

static void
test_fit (void)
{
  UpLine      lines[16] = { seg (20, 50, 80, 88), seg (80, 50, 80, 92), seg (50, 20, 80, 1), seg (50, 80, 80, -2) };
  UpParams    p = { 1, 2, 3, 4 };
  UpFitResult r;

  for (int i = 0; i < 4; i++)
    lines[i].type = up_classify_line (&lines[i]);

  check (up_fit (NULL, 0, 100, 100, UP_MODE_VERTICAL, NULL, &p, &r) == UP_NOT_ENOUGH_LINES &&
         up_params_neutral (&p), "fit: no lines, not enough, neutral parameters");
  check (up_fit (lines, 1, 100, 100, UP_MODE_VERTICAL, NULL, &p, &r) == UP_NOT_ENOUGH_LINES,
         "fit: one vertical line is not enough for vertical");
  check (up_fit (lines, 2, 100, 100, UP_MODE_VERTICAL, NULL, &p, &r) == UP_OK && r.vertical == 2,
         "fit: two vertical lines are enough for vertical");
  check (up_fit (lines, 2, 100, 100, UP_MODE_HORIZONTAL, NULL, &p, &r) == UP_NOT_ENOUGH_LINES,
         "fit: vertical lines do not make a horizontal fit");
  check (up_fit (lines, 4, 100, 100, UP_MODE_FULL, NULL, &p, &r) == UP_OK && r.vertical == 2 &&
         r.horizontal == 2, "fit: full with two of each");
  check (up_fit (lines + 2, 2, 100, 100, UP_MODE_AUTO, NULL, &p, &r) == UP_OK &&
         r.mode_used == UP_MODE_HORIZONTAL, "fit: auto with horizontal lines only uses horizontal");
  check (up_fit (lines, 1, 100, 100, UP_MODE_ROTATION, NULL, &p, &r) == UP_OK,
         "fit: one line is enough for rotation");
  check (up_fit (lines, 1, 100, 100, UP_MODE_AUTO, NULL, &p, &r) == UP_OK && r.mode_used == UP_MODE_ROTATION,
         "fit: auto with a single line levels (rotation)");
  check (up_fit (lines, 4, 0, 100, UP_MODE_FULL, NULL, &p, &r) == UP_ERROR &&
         up_fit (lines, 4, 100, 100, UP_MODE_COUNT, NULL, &p, &r) == UP_ERROR &&
         up_fit (NULL, 3, 100, 100, UP_MODE_FULL, NULL, &p, &r) == UP_ERROR,
         "fit: bad size, mode or lines give UP_ERROR");
  check (up_mode_for_lines (lines, 4) == UP_MODE_FULL && up_mode_for_lines (lines, 2) == UP_MODE_VERTICAL &&
         up_mode_for_lines (lines + 2, 2) == UP_MODE_HORIZONTAL && up_mode_for_lines (lines, 1) == UP_MODE_ROTATION &&
         up_mode_for_lines (lines + 1, 2) == UP_MODE_ROTATION && up_mode_for_lines (NULL, 0) == UP_MODE_ROTATION,
         "mode for drawn lines: full, vertical, horizontal, rotation");
  lines[0].outlier = 1;
  check (up_fit (lines, 2, 100, 100, UP_MODE_VERTICAL, NULL, &p, &r) == UP_NOT_ENOUGH_LINES,
         "fit: outliers do not count");

  /* recovery from exact lines: the lines the parameters straighten */
  {
    static const struct { UpParams p; UpMode mode; } cases[] = {
      { { 2.0, 0, 0, 0 }, UP_MODE_ROTATION },
      { { -4.0, 0, 0, 0 }, UP_MODE_ROTATION },
      { { 1.5, 0.25, 0, 0 }, UP_MODE_VERTICAL },
      { { -2.5, -0.4, 0, 0 }, UP_MODE_VERTICAL },
      { { 0.7, 0, 0.3, 0 }, UP_MODE_HORIZONTAL },
      { { 1.0, 0.2, -0.3, 0.05 }, UP_MODE_FULL },
      { { -3.0, -0.35, 0.25, -0.1 }, UP_MODE_FULL },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++)
      {
        const UpParams *t = &cases[i].p;
        const int       n = lines_for (t, 600, 400, 5, 5, lines);
        UpParams        f;
        UpStatus        s = up_fit (lines, n, 600, 400, cases[i].mode, NULL, &f, &r);

        check (s == UP_OK && fabs (f.rotation - t->rotation) < 0.01 && fabs (f.shift_v - t->shift_v) < 1e-3 &&
               fabs (f.shift_h - t->shift_h) < 1e-3 && fabs (f.shear - t->shear) < 1e-3 && r.cost_after < 50.0,
               "fit %s on exact lines: %s, rotation %.4f (%g), shift-v %.5f (%g), shift-h %.5f (%g), "
               "shear %.5f (%g), cost %.1f to %.2g, %d iterations",
               up_mode_name (cases[i].mode), up_status_name (s), f.rotation, t->rotation, f.shift_v,
               t->shift_v, f.shift_h, t->shift_h, f.shear, t->shear, r.cost_before, r.cost_after,
               r.iterations);
      }
  }

  /* the fit is deterministic: the same lines give the same bits */
  {
    const UpParams t = { 1.0, 0.2, -0.3, 0.05 };
    const int      n = lines_for (&t, 600, 400, 5, 5, lines);
    UpParams       a, b;
    up_fit (lines, n, 600, 400, UP_MODE_AUTO, NULL, &a, &r);
    up_fit (lines, n, 600, 400, UP_MODE_AUTO, NULL, &b, &r);
    check (! memcmp (&a, &b, sizeof a), "fit: twice the same lines, the same result to the bit");
  }
}

static void
test_ransac (void)
{
  UpLine lines[16];
  int    n = 0, out = 0, wrong = 0;

  /* six verticals meeting far below the image, and one off line */
  for (int i = 0; i < 6; i++)
    {
      const double x = 40.0 + 50.0 * i;
      /* the vanishing point at (170, 3000) */
      lines[n] = (UpLine) { x, 20.0, x + (170.0 - x) * 160.0 / 2980.0, 180.0, 2.0, 100.0, UP_LINE_VERTICAL, 0 };
      n++;
    }
  lines[n++] = (UpLine) { 100.0, 20.0, 130.0, 180.0, 2.0, 100.0, UP_LINE_VERTICAL, 0 };
  up_remove_outliers (lines, n, 340, 200);
  for (int i = 0; i < n; i++)
    {
      out += lines[i].outlier;
      wrong += (i == 6) != lines[i].outlier;
    }
  check (wrong == 0, "ransac: the one line off the vanishing point is the outlier (%d marked)", out);
}

/* ------------------------------------------------------------ crop and warp */

/* the largest axis aligned rectangle inside a w x h rectangle turned by
 * a (the well known closed form) */
static double
largest_in_rotated (double w, double h, double a)
{
  const double sa = fabs (sin (a)), ca = fabs (cos (a));
  const double lng = fmax (w, h), sht = fmin (w, h);

  if (sht <= 2.0 * sa * ca * lng || fabs (sa - ca) < 1e-10)
    {
      const double x = 0.5 * sht;
      return w >= h ? (x / sa) * (x / ca) : (x / ca) * (x / sa);
    }
  {
    const double c2 = ca * ca - sa * sa;
    return ((w * ca - h * sa) / c2) * ((h * ca - w * sa) / c2);
  }
}

static int
inside_outline (const double m[9], int w, int h, double x, double y)
{
  double vx[4], vy[4];
  int    pos = 0, neg = 0;

  up_matrix_apply (m, 0, 0, &vx[0], &vy[0]);
  up_matrix_apply (m, w, 0, &vx[1], &vy[1]);
  up_matrix_apply (m, w, h, &vx[2], &vy[2]);
  up_matrix_apply (m, 0, h, &vx[3], &vy[3]);
  for (int i = 0; i < 4; i++)
    {
      const int    j = (i + 1) % 4;
      const double c = (vx[j] - vx[i]) * (y - vy[i]) - (vy[j] - vy[i]) * (x - vx[i]);
      pos += c > -1e-6;
      neg += c < 1e-6;
    }
  return pos == 4 || neg == 4;
}

static int
rect_inside (const double m[9], int w, int h, const UpRect *r)
{
  return inside_outline (m, w, h, r->x, r->y) && inside_outline (m, w, h, r->x + r->width, r->y) &&
         inside_outline (m, w, h, r->x, r->y + r->height) &&
         inside_outline (m, w, h, r->x + r->width, r->y + r->height);
}

static void
test_crop (void)
{
  static const struct { int w, h; double deg; } rots[] = { { 600, 400, 3.0 }, { 600, 400, 10.0 },
                                                          { 400, 600, -6.0 }, { 500, 500, 45.0 },
                                                          { 800, 200, 20.0 } };
  double m[9];
  UpRect r;

  for (size_t i = 0; i < sizeof rots / sizeof *rots; i++)
    {
      const UpParams p = { rots[i].deg, 0, 0, 0 };
      const int      w = rots[i].w, h = rots[i].h;
      const double   a = rots[i].deg * M_PI / 180.0;
      const double   want = largest_in_rotated (w, h, a);

      up_homography (&p, NULL, w, h, m);
      UpStatus st = up_crop (m, w, h, UP_CROP_LARGEST, &r);
      check (st == UP_OK && rect_inside (m, w, h, &r) &&
             r.width * (double) r.height > 0.97 * want,
             "crop largest: %dx%d turned %g degrees: %dx%d = %.0f, inside, the closed form %.0f (%.2f %%)",
             w, h, rots[i].deg, r.width, r.height, (double) r.width * r.height, want,
             100.0 * r.width * r.height / want);
      {
        /* the same shape: centred, scale s from the closed form */
        const double sa = fabs (sin (a)), ca = fabs (cos (a));
        const double s = fmin (0.5 * w / (0.5 * w * ca + 0.5 * h * sa), 0.5 * h / (0.5 * w * sa + 0.5 * h * ca));
        const double want_a = s * s * w * h;
        st = up_crop (m, w, h, UP_CROP_ASPECT, &r);
        check (st == UP_OK && rect_inside (m, w, h, &r) &&
               r.width * (double) r.height > 0.97 * want_a &&
               fabs ((double) r.width / r.height - (double) w / h) < 0.02 * w / h,
               "crop aspect: %dx%d turned %g degrees: %dx%d, inside, the closed form %.0f (%.2f %%)",
               w, h, rots[i].deg, r.width, r.height, want_a, 100.0 * r.width * r.height / want_a);
      }
    }
  {
    const UpParams p = { 2.0, 0.3, -0.2, 0.03 };
    double         x1 = -1e9, y1 = -1e9;

    up_homography (&p, NULL, 640, 480, m);
    for (int j = 0; j < 4; j++)
      {
        double x, y;
        up_matrix_apply (m, (j & 1) ? 640 : 0, (j & 2) ? 480 : 0, &x, &y);
        x1 = fmax (x1, x);
        y1 = fmax (y1, y);
      }
    UpStatus st = up_crop (m, 640, 480, UP_CROP_NONE, &r);
    check (st == UP_OK && r.x == 0 && r.y == 0 &&
           r.width == (int) ceil (x1 - 1e-6) && r.height == (int) ceil (y1 - 1e-6),
           "crop none: the bounding box, %dx%d at (%d, %d)", r.width, r.height, r.x, r.y);
    st = up_crop (m, 640, 480, UP_CROP_LARGEST, &r);
    check (st == UP_OK && rect_inside (m, 640, 480, &r) &&
           r.width * r.height > 0.5 * 640 * 480,
           "crop largest: a full perspective fit: %dx%d inside the outline", r.width, r.height);
  }
  {
    const double id[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    const UpStatus st = up_crop (id, 320, 240, UP_CROP_LARGEST, &r);
    check (st == UP_OK && r.x == 0 && r.y == 0 &&
           r.width >= 318 && r.height >= 238 && r.width <= 320 && r.height <= 240,
           "crop largest: the identity keeps (almost) everything: %dx%d at (%d, %d)", r.width,
           r.height, r.x, r.y);
    check (up_crop (id, 0, 240, UP_CROP_LARGEST, &r) == UP_ERROR &&
           up_crop (NULL, 320, 240, UP_CROP_LARGEST, &r) == UP_ERROR,
           "crop: bad arguments give UP_ERROR");
  }
}

static void
test_warp (void)
{
  const int    w = 37, h = 23, c = 2;
  float       *in = malloc (sizeof (float) * w * h * c), *out = malloc (sizeof (float) * w * h * c);
  float       *cov = malloc (sizeof (float) * w * h);
  const double id[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  const double shift[9] = { 1, 0, 5, 0, 1, -3, 0, 0, 1 };
  const UpRect all = { 0, 0, w, h };
  int          ok;

  for (int i = 0; i < w * h * c; i++)
    in[i] = (float) ((i * 7919) % 1000) / 1000.0f;
  for (int interp = UP_INTERP_NEAREST; interp <= UP_INTERP_CUBIC; interp++)
    {
      ok = up_warp (in, w, h, c, id, &all, (UpInterp) interp, out, cov) == UP_OK;
      for (int i = 0; ok && i < w * h * c; i++)
        ok = fabsf (out[i] - in[i]) < 1e-6f;
      for (int i = 0; ok && i < w * h; i++)
        ok = cov[i] == 1.0f;
      check (ok, "warp: the identity gives the input back (interpolation %d)", interp);
    }
  {
    /* a shift by (5, -3): output (x, y) is input (x - 5, y + 3) */
    const UpRect part = { 5, -3, w, h };
    ok = up_warp (in, w, h, c, shift, &part, UP_INTERP_CUBIC, out, cov) == UP_OK;
    for (int i = 0; ok && i < w * h * c; i++)
      ok = fabsf (out[i] - in[i]) < 1e-6f;
    check (ok, "warp: a whole pixel translation with the matching rectangle gives the input back");
    {
      const UpRect off = { 0, 0, w, h };
      ok = up_warp (in, w, h, c, shift, &off, UP_INTERP_LINEAR, out, cov) == UP_OK && cov[0] == 0.0f &&
           out[0] == 0.0f && cov[(size_t) 0 * w + 6] == 1.0f && fabsf (out[1 * c] - 0.0f) < 1e-9f;
      check (ok, "warp: outside the input is 0 with coverage 0");
    }
  }
  check (up_warp (in, w, h, c, id, NULL, UP_INTERP_LINEAR, out, cov) == UP_ERROR,
         "warp: bad arguments give UP_ERROR");
  free (in);
  free (out);
  free (cov);
}

int
main (void)
{
  test_classify ();
  test_names ();
  test_lsd ();
  test_detect ();
  test_homography ();
  test_cost ();
  test_simplex ();
  test_fit ();
  test_ransac ();
  test_crop ();
  test_warp ();
  printf ("%d passed, %d failed\n", passes, failures);
  return failures ? 1 : 0;
}
