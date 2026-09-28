/* upright-cli: the core of gimp-upright on the command line, for the
 * tests (like fusion-cli in gimp-fusion)
 *
 *   upright-cli fit [--mode=auto|vertical|horizontal|full|rotation]
 *                   [--lines=FILE] image.pfm
 *       detects the lines in the image (or reads them from FILE: one
 *       segment "x1 y1 x2 y2" per line, the guided mode), fits, and prints
 *       one "key value" per line: status, mode, lines, vertical,
 *       horizontal, rotation, shift-v, shift-h, shear, matrix
 *   upright-cli lines image.pfm
 *       prints the detected segments, "x1 y1 x2 y2 width weight type"
 *   upright-cli version
 *
 * Images are PFM (PF: RGB, Pf: gray, float, bottom row first), as the
 * tests of gimp-fusion use them; RGB is made gray by the mean.
 *
 * SKELETON: detection finds nothing and the fit returns the identity.
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "upright.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float *
read_pfm_gray (const char *path, int *w, int *h)
{
  FILE *f = fopen (path, "rb");
  char kind[3] = { 0 };
  double scale;
  int c;
  float *raw, *gray;

  if (! f)
    return NULL;
  if (fscanf (f, "%2s %d %d %lf", kind, w, h, &scale) != 4 || fgetc (f) == EOF ||
      (strcmp (kind, "PF") && strcmp (kind, "Pf")) || *w <= 0 || *h <= 0 ||
      *w > 65536 || *h > 65536 || scale >= 0)   /* little endian only */
    {
      fclose (f);
      return NULL;
    }
  c = kind[1] == 'F' ? 3 : 1;
  raw = malloc (sizeof (float) * (size_t) *w * *h * c);
  gray = malloc (sizeof (float) * (size_t) *w * *h);
  if (! raw || ! gray || fread (raw, sizeof (float) * c, (size_t) *w * *h, f) != (size_t) *w * *h)
    {
      free (raw);
      free (gray);
      fclose (f);
      return NULL;
    }
  fclose (f);
  for (int y = 0; y < *h; y++)
    for (int x = 0; x < *w; x++)
      {
        const float *p = raw + ((size_t) (*h - 1 - y) * *w + x) * c;
        gray[(size_t) y * *w + x] = c == 3 ? (p[0] + p[1] + p[2]) / 3.0f : p[0];
      }
  free (raw);
  return gray;
}

static int
read_lines (const char *path, UpLine **lines, int *count)
{
  FILE *f = fopen (path, "r");
  UpLine l = { 0 };
  int n = 0, cap = 0;

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
      (*lines)[n++] = l;
    }
  fclose (f);
  *count = n;
  return 1;
}

static int
usage (void)
{
  fprintf (stderr, "usage: upright-cli fit [--mode=MODE] [--lines=FILE] image.pfm\n"
                   "       upright-cli lines image.pfm\n"
                   "       upright-cli version\n");
  return 2;
}

int
main (int argc, char **argv)
{
  UpMode mode = UP_MODE_AUTO;
  const char *lines_file = NULL, *image = NULL;
  UpLine *lines = NULL;
  int count = 0, w, h;
  float *gray;

  if (argc < 2)
    return usage ();
  if (! strcmp (argv[1], "version"))
    {
      printf ("upright-cli %s\n", UP_VERSION);
      return 0;
    }
  if (strcmp (argv[1], "fit") && strcmp (argv[1], "lines"))
    return usage ();
  for (int i = 2; i < argc; i++)
    {
      if (! strncmp (argv[i], "--mode=", 7))
        {
          if (! up_mode_from_name (argv[i] + 7, &mode))
            {
              fprintf (stderr, "upright-cli: unknown mode %s\n", argv[i] + 7);
              return 2;
            }
        }
      else if (! strncmp (argv[i], "--lines=", 8))
        lines_file = argv[i] + 8;
      else if (argv[i][0] == '-' || image)
        return usage ();
      else
        image = argv[i];
    }
  if (! image)
    return usage ();
  gray = read_pfm_gray (image, &w, &h);
  if (! gray)
    {
      fprintf (stderr, "upright-cli: cannot read %s (a little endian PFM)\n", image);
      return 1;
    }

  if (lines_file ? ! read_lines (lines_file, &lines, &count)
                 : up_detect_lines (gray, w, h, &lines, &count) != UP_OK)
    {
      fprintf (stderr, "upright-cli: no lines from %s\n", lines_file ? lines_file : image);
      free (gray);
      return 1;
    }

  if (! strcmp (argv[1], "lines"))
    {
      for (int i = 0; i < count; i++)
        printf ("%.3f %.3f %.3f %.3f %.3f %.3f %d\n", lines[i].x1, lines[i].y1,
                lines[i].x2, lines[i].y2, lines[i].width, lines[i].weight, lines[i].type);
    }
  else
    {
      UpParams p;
      UpFitResult r;
      double m[9];
      UpStatus s = up_fit (lines, count, w, h, mode, &p, &r);

      printf ("status %s\nmode %s\nlines %d\nvertical %d\nhorizontal %d\n",
              up_status_name (s), up_mode_name (r.mode_used), count, r.vertical, r.horizontal);
      printf ("rotation %.6f\nshift-v %.6f\nshift-h %.6f\nshear %.6f\n",
              p.rotation, p.shift_v, p.shift_h, p.shear);
      up_homography (&p, w, h, m);
      printf ("matrix");
      for (int i = 0; i < 9; i++)
        printf (" %.9g", m[i]);
      printf ("\n");
    }
  free (lines);
  free (gray);
  return 0;
}
