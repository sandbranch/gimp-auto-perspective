/* auto-perspective-cli: the core of gimp-auto-perspective on the command
 * line, for the tests (like fusion-cli in gimp-fusion)
 *
 *   auto-perspective-cli lines image.pfm
 *       the detected segments after outlier removal, one per line:
 *       "x1 y1 x2 y2 width weight type outlier" (type 0 ignored,
 *       1 vertical, 2 horizontal; outlier 1 if RANSAC dropped it)
 *   auto-perspective-cli fit [--mode=MODE] [--lines=FILE] [--focal=MM] image.pfm
 *       detects the lines (or reads them from FILE: one segment
 *       "x1 y1 x2 y2" per line, as the guided mode), drops outliers
 *       (detected lines only), fits, and prints one "key value" per line:
 *       status, mode, lines, vertical, horizontal, rotation, shift-v,
 *       shift-h, shear, iterations, cost-before, cost-after, matrix
 *   auto-perspective-cli warp [fit options] [--params=R,V,H,S] [--crop=C]
 *                   [--interp=nearest|linear|cubic] image.pfm -o out.pfm
 *       as fit (or with the given parameters instead of a fit), then
 *       the transformed image cropped as asked (none, largest, aspect;
 *       default largest) into out.pfm, and "crop x y width height"
 *   auto-perspective-cli cost --params=R,V,H,S [--lines=FILE] [--focal=MM] image.pfm
 *       ashift's cost of those parameters for the lines: cost-vertical,
 *       cost-horizontal, cost-all
 *   auto-perspective-cli lsd [--scale=S] image.pfm
 *       LSD alone, with its own defaults (scale 0.8, as LSD 1.6) or the
 *       given scale, on the gray values times 255: "x1 y1 x2 y2 width p
 *       log_nfa" per segment, for the comparison with the reference
 *       implementation (tests/lsd-ref.sh)
 *   auto-perspective-cli version
 *
 * --stats (any command but lsd) adds "seconds" (wall clock) and
 * "vmhwm-kib" (the peak resident memory, from /proc/self/status) at the
 * end.
 *
 * MODE is auto, vertical, horizontal, full or rotation; --focal the
 * focal length in mm (35 mm equivalent), default 28 (ashift's generic
 * lens). Images are PFM (PF: RGB, Pf: gray, float, bottom row first), as
 * the tests of gimp-fusion use them; for the lines RGB is made gray by
 * the mean.
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "perspective.h"
#include "lsd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* the peak resident memory of this process, KiB (0 if unknown) */
static long
vmhwm_kib (void)
{
  FILE *f = fopen ("/proc/self/status", "r");
  char  line[256];
  long  kib = 0;

  if (! f)
    return 0;
  while (fgets (line, sizeof line, f))
    if (! strncmp (line, "VmHWM:", 6))
      kib = atol (line + 6);
  fclose (f);
  return kib;
}

static double
now (void)
{
  struct timespec t;
  clock_gettime (CLOCK_MONOTONIC, &t);
  return t.tv_sec + 1e-9 * t.tv_nsec;
}

static float *
read_pfm (const char *path, int *w, int *h, int *c)
{
  FILE *f = fopen (path, "rb");
  char  kind[3] = { 0 };
  double scale;
  float *raw, *img;

  if (! f)
    return NULL;
  if (fscanf (f, "%2s %d %d %lf", kind, w, h, &scale) != 4 || fgetc (f) == EOF ||
      (strcmp (kind, "PF") && strcmp (kind, "Pf")) || *w <= 0 || *h <= 0 ||
      *w > 65536 || *h > 65536 || scale >= 0)   /* little endian only */
    {
      fclose (f);
      return NULL;
    }
  *c = kind[1] == 'F' ? 3 : 1;
  raw = malloc (sizeof (float) * (size_t) *w * *h * *c);
  img = malloc (sizeof (float) * (size_t) *w * *h * *c);
  if (! raw || ! img || fread (raw, sizeof (float) * *c, (size_t) *w * *h, f) != (size_t) *w * *h)
    {
      free (raw);
      free (img);
      fclose (f);
      return NULL;
    }
  fclose (f);
  for (int y = 0; y < *h; y++)
    memcpy (img + (size_t) y * *w * *c, raw + (size_t) (*h - 1 - y) * *w * *c,
            sizeof (float) * *w * *c);
  free (raw);
  return img;
}

static int
write_pfm (const char *path, const float *img, int w, int h, int c)
{
  FILE *f = fopen (path, "wb");
  int   ok;

  if (! f)
    return 0;
  ok = fprintf (f, "%s\n%d %d\n-1.0\n", c == 3 ? "PF" : "Pf", w, h) > 0;
  for (int y = h - 1; ok && y >= 0; y--)
    ok = fwrite (img + (size_t) y * w * c, sizeof (float) * c, (size_t) w, f) == (size_t) w;
  return fclose (f) == 0 && ok;
}

static float *
to_gray (const float *img, int w, int h, int c)
{
  float *g = malloc (sizeof (float) * (size_t) w * h);

  if (g)
    for (size_t i = 0; i < (size_t) w * h; i++)
      g[i] = c == 3 ? (img[3 * i] + img[3 * i + 1] + img[3 * i + 2]) / 3.0f : img[i];
  return g;
}

static int
read_lines (const char *path, UpLine **lines, int *count)
{
  FILE  *f = fopen (path, "r");
  UpLine l = { 0 };
  int    n = 0, cap = 0;

  *lines = NULL;
  if (! f)
    return 0;
  while (fscanf (f, "%lf %lf %lf %lf", &l.x1, &l.y1, &l.x2, &l.y2) == 4)
    {
      if (n == cap)
        {
          UpLine *more = realloc (*lines, sizeof (UpLine) * (cap = cap ? 2 * cap : 16));
          if (! more)
            {
              fclose (f);
              return 0;
            }
          *lines = more;
        }
      l.width = 1.0;
      l.weight = 1.0;
      l.type = up_classify_line (&l);
      l.outlier = 0;
      (*lines)[n++] = l;
    }
  fclose (f);
  *count = n;
  return 1;
}

static int
usage (void)
{
  fprintf (stderr,
    "usage: auto-perspective-cli lines image.pfm\n"
    "       auto-perspective-cli fit [--mode=MODE] [--lines=FILE] [--focal=MM] image.pfm\n"
    "       auto-perspective-cli warp [fit options] [--params=R,V,H,S] [--crop=none|largest|aspect]\n"
    "                                 [--interp=nearest|linear|cubic] image.pfm -o out.pfm\n"
    "       auto-perspective-cli cost --params=R,V,H,S [--lines=FILE] [--focal=MM] image.pfm\n"
    "       auto-perspective-cli lsd [--scale=S] image.pfm\n"
    "       auto-perspective-cli version\n");
  return 2;
}

/* LSD as the reference runs it (scale 0.8 unless given), raw segments */
static int
run_lsd (int argc, char **argv)
{
  UpLsdParams   lp;
  UpLsdSegment *seg = NULL;
  const char   *image = NULL;
  double       *d;
  float        *img, *gray;
  int           w, h, c, n = 0;

  up_lsd_defaults (&lp);
  lp.scale = 0.8;
  for (int i = 2; i < argc; i++)
    if (! strncmp (argv[i], "--scale=", 8))
      lp.scale = atof (argv[i] + 8);
    else if (argv[i][0] == '-' || image)
      return usage ();
    else
      image = argv[i];
  if (! image)
    return usage ();
  img = read_pfm (image, &w, &h, &c);
  gray = img ? to_gray (img, w, h, c) : NULL;
  d = gray ? malloc (sizeof (double) * (size_t) w * h) : NULL;
  if (! d)
    {
      fprintf (stderr, "auto-perspective-cli: cannot read %s (a little endian PFM)\n", image);
      free (gray);
      free (img);
      return 1;
    }
  for (size_t i = 0; i < (size_t) w * h; i++)
    d[i] = 255.0 * gray[i];
  if (up_lsd (d, w, h, &lp, &seg, &n))
    {
      fprintf (stderr, "auto-perspective-cli: LSD failed\n");
      n = -1;
    }
  for (int i = 0; i < n; i++)
    printf ("%.6f %.6f %.6f %.6f %.6f %.6f %.6f\n", seg[i].x1, seg[i].y1, seg[i].x2, seg[i].y2,
            seg[i].width, seg[i].p, seg[i].log_nfa);
  free (seg);
  free (d);
  free (gray);
  free (img);
  return n < 0;
}

static void
print_matrix (const double m[9])
{
  printf ("matrix");
  for (int i = 0; i < 9; i++)
    printf (" %.17g", m[i]);
  printf ("\n");
}

int
main (int argc, char **argv)
{
  UpMode      mode = UP_MODE_AUTO;
  UpCrop      crop = UP_CROP_LARGEST;
  UpInterp    interp = UP_INTERP_LINEAR;
  UpLens      lens;
  UpParams    given = { 0 };
  const char *lines_file = NULL, *image = NULL, *output = NULL, *cmd;
  UpLine     *lines = NULL;
  int         count = 0, w, h, c, have_params = 0, status = 0, stats = 0;
  float      *img, *gray;
  double      t0 = now ();

  up_lens_generic (&lens);
  if (argc < 2)
    return usage ();
  cmd = argv[1];
  if (! strcmp (cmd, "version"))
    {
      printf ("auto-perspective-cli %s\n", UP_VERSION);
      return 0;
    }
  if (! strcmp (cmd, "lsd"))
    return run_lsd (argc, argv);
  if (strcmp (cmd, "fit") && strcmp (cmd, "lines") && strcmp (cmd, "warp") && strcmp (cmd, "cost"))
    return usage ();
  for (int i = 2; i < argc; i++)
    {
      const char *a = argv[i];

      if (! strncmp (a, "--mode=", 7))
        {
          if (! up_mode_from_name (a + 7, &mode))
            {
              fprintf (stderr, "auto-perspective-cli: unknown mode %s\n", a + 7);
              return 2;
            }
        }
      else if (! strncmp (a, "--crop=", 7))
        {
          if (! up_crop_from_name (a + 7, &crop))
            {
              fprintf (stderr, "auto-perspective-cli: unknown crop %s\n", a + 7);
              return 2;
            }
        }
      else if (! strncmp (a, "--interp=", 9))
        {
          if (! strcmp (a + 9, "nearest"))
            interp = UP_INTERP_NEAREST;
          else if (! strcmp (a + 9, "linear"))
            interp = UP_INTERP_LINEAR;
          else if (! strcmp (a + 9, "cubic"))
            interp = UP_INTERP_CUBIC;
          else
            return usage ();
        }
      else if (! strncmp (a, "--lines=", 8))
        lines_file = a + 8;
      else if (! strncmp (a, "--focal=", 8))
        {
          lens.focal = atof (a + 8);
          if (! (lens.focal > 0.0))
            return usage ();
        }
      else if (! strncmp (a, "--params=", 9))
        {
          if (sscanf (a + 9, "%lf,%lf,%lf,%lf", &given.rotation, &given.shift_v, &given.shift_h,
                      &given.shear) != 4)
            return usage ();
          have_params = 1;
        }
      else if (! strcmp (a, "-o") && i + 1 < argc)
        output = argv[++i];
      else if (! strcmp (a, "--stats"))
        stats = 1;
      else if (a[0] == '-' || image)
        return usage ();
      else
        image = a;
    }
  if (! image || (! strcmp (cmd, "warp") && ! output) || (! strcmp (cmd, "cost") && ! have_params))
    return usage ();
  img = read_pfm (image, &w, &h, &c);
  gray = img ? to_gray (img, w, h, c) : NULL;
  if (! gray)
    {
      fprintf (stderr, "auto-perspective-cli: cannot read %s (a little endian PFM)\n", image);
      free (img);
      return 1;
    }

  if (lines_file ? ! read_lines (lines_file, &lines, &count)
                 : up_detect_lines (gray, w, h, &lines, &count) != UP_OK)
    {
      fprintf (stderr, "auto-perspective-cli: no lines from %s\n", lines_file ? lines_file : image);
      free (gray);
      free (img);
      return 1;
    }
  if (! lines_file)
    up_remove_outliers (lines, count, w, h);

  if (! strcmp (cmd, "lines"))
    {
      for (int i = 0; i < count; i++)
        printf ("%.3f %.3f %.3f %.3f %.3f %.3f %d %d\n", lines[i].x1, lines[i].y1,
                lines[i].x2, lines[i].y2, lines[i].width, lines[i].weight, lines[i].type,
                lines[i].outlier);
    }
  else if (! strcmp (cmd, "cost"))
    {
      printf ("cost-vertical %.9g\ncost-horizontal %.9g\ncost-all %.9g\n",
              up_cost (lines, count, w, h, &lens, &given, 1),
              up_cost (lines, count, w, h, &lens, &given, 2),
              up_cost (lines, count, w, h, &lens, &given, 3));
    }
  else
    {
      UpParams    p = given;
      UpFitResult r = { 0 };
      UpStatus    s = UP_OK;
      double      m[9];

      if (! have_params)
        {
          s = up_fit (lines, count, w, h, mode, &lens, &p, &r);
          printf ("status %s\nmode %s\nlines %d\nvertical %d\nhorizontal %d\n",
                  up_status_name (s), up_mode_name (r.mode_used), count, r.vertical, r.horizontal);
          printf ("iterations %d\ncost-before %.9g\ncost-after %.9g\n",
                  r.iterations, r.cost_before, r.cost_after);
        }
      printf ("rotation %.9f\nshift-v %.9f\nshift-h %.9f\nshear %.9f\n",
              p.rotation, p.shift_v, p.shift_h, p.shear);
      if (up_homography (&p, &lens, w, h, m) != UP_OK)
        {
          fprintf (stderr, "auto-perspective-cli: bad parameters\n");
          status = 1;
        }
      print_matrix (m);
      if (status == 0 && ! strcmp (cmd, "warp"))
        {
          UpRect rect;
          float *out;

          if (up_crop (m, w, h, crop, &rect) != UP_OK)
            {
              fprintf (stderr, "auto-perspective-cli: the crop failed\n");
              status = 1;
            }
          else if (! (out = malloc (sizeof (float) * (size_t) rect.width * rect.height * c)) ||
                   up_warp (img, w, h, c, m, &rect, interp, out, NULL) != UP_OK ||
                   ! write_pfm (output, out, rect.width, rect.height, c))
            {
              fprintf (stderr, "auto-perspective-cli: cannot warp into %s\n", output);
              free (out);
              status = 1;
            }
          else
            {
              printf ("crop %d %d %d %d\n", rect.x, rect.y, rect.width, rect.height);
              free (out);
            }
        }
    }
  free (lines);
  free (gray);
  free (img);
  if (stats)
    printf ("seconds %.3f\nvmhwm-kib %ld\n", now () - t0, vmhwm_kib ());
  return status;
}
