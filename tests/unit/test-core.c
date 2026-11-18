/* unit tests of the core (skeleton): what the core already does, and the
 * contract the real implementation must keep (neutral parameters give
 * the identity, bad input gives UP_ERROR, not a crash)
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "perspective.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
               x + 0.5 * len * cos (a), y + 0.5 * len * sin (a), 1.0, 1.0, UP_LINE_IGNORED };
  return l;
}

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
  UpLine nan_line = { NAN, 0, 10, 10, 1, 1, UP_LINE_IGNORED };
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
}

static void
test_fit (void)
{
  UpLine lines[4] = { seg (20, 50, 80, 88), seg (80, 50, 80, 92), seg (50, 20, 80, 1), seg (50, 80, 80, -2) };
  UpParams p = { 1, 2, 3, 4 };
  UpFitResult r;

  for (int i = 0; i < 4; i++)
    lines[i].type = up_classify_line (&lines[i]);

  check (up_fit (NULL, 0, 100, 100, UP_MODE_VERTICAL, &p, &r) == UP_NOT_ENOUGH_LINES &&
         up_params_neutral (&p), "fit: no lines, not enough, neutral parameters");
  check (up_fit (lines, 1, 100, 100, UP_MODE_VERTICAL, &p, &r) == UP_NOT_ENOUGH_LINES,
         "fit: one vertical line is not enough for vertical");
  check (up_fit (lines, 2, 100, 100, UP_MODE_VERTICAL, &p, &r) == UP_OK && r.vertical == 2,
         "fit: two vertical lines are enough for vertical");
  check (up_fit (lines, 2, 100, 100, UP_MODE_HORIZONTAL, &p, &r) == UP_NOT_ENOUGH_LINES,
         "fit: vertical lines do not make a horizontal fit");
  check (up_fit (lines, 4, 100, 100, UP_MODE_AUTO, &p, &r) == UP_OK && r.mode_used == UP_MODE_FULL &&
         r.vertical == 2 && r.horizontal == 2, "fit: auto with both kinds uses full");
  check (up_fit (lines + 2, 2, 100, 100, UP_MODE_AUTO, &p, &r) == UP_OK &&
         r.mode_used == UP_MODE_HORIZONTAL, "fit: auto with horizontal lines only uses horizontal");
  check (up_fit (lines, 1, 100, 100, UP_MODE_ROTATION, &p, &r) == UP_OK,
         "fit: one line is enough for rotation");
  check (up_fit (lines, 4, 0, 100, UP_MODE_FULL, &p, &r) == UP_ERROR &&
         up_fit (lines, 4, 100, 100, UP_MODE_COUNT, &p, &r) == UP_ERROR &&
         up_fit (NULL, 3, 100, 100, UP_MODE_FULL, &p, &r) == UP_ERROR,
         "fit: bad size, mode or lines give UP_ERROR");
  /* SKELETON: the fit returns the identity; the recovery tests of
   * PLAN.md (known rotation, shift and shear) replace this check */
  check (up_fit (lines, 4, 100, 100, UP_MODE_FULL, &p, &r) == UP_OK && up_params_neutral (&p),
         "fit (skeleton): the parameters are neutral");
  printf ("SKIP recovery of known rotation, lens shift and shear: not implemented yet\n");
}

static void
test_homography (void)
{
  const UpParams zero = { 0 }, turned = { 1.5, 0, 0, 0 };
  double m[9];

  check (up_homography (&zero, 640, 480, m) == UP_OK &&
         m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 1 && m[5] == 0 &&
         m[6] == 0 && m[7] == 0 && m[8] == 1, "homography: neutral parameters give the identity");
  check (up_homography (&turned, 640, 480, m) == UP_ERROR,
         "homography (skeleton): other parameters are not done yet");
  check (up_homography (&zero, 0, 480, m) == UP_ERROR, "homography: bad size gives UP_ERROR");
}

static void
test_detect (void)
{
  const int w = 64, h = 48;
  float *img = malloc (sizeof (float) * w * h);
  UpLine *lines = (UpLine *) 1;
  int n = -1;

  /* a vertical and a horizontal edge */
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      img[y * w + x] = (x > 30) ^ (y > 20) ? 0.9f : 0.1f;
  check (up_detect_lines (img, w, h, &lines, &n) == UP_OK && n >= 0 && (n == 0) == (lines == NULL),
         "detect: runs, count and array agree");
  check (up_detect_lines (NULL, w, h, &lines, &n) == UP_ERROR, "detect: no image gives UP_ERROR");
  free (lines);
  free (img);
  printf ("SKIP detection of the two edges: LSD not ported yet\n");
}

int
main (void)
{
  test_classify ();
  test_names ();
  test_fit ();
  test_homography ();
  test_detect ();
  printf ("%d passed, %d failed\n", passes, failures);
  return failures ? 1 : 0;
}
