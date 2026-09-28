/* gimp-upright core (skeleton): see upright.h
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "upright.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

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

UpStatus
up_detect_lines (const float *gray, int width, int height, UpLine **lines, int *count)
{
  if (! gray || width <= 0 || height <= 0 || ! lines || ! count)
    return UP_ERROR;
  /* SKELETON: the LSD port comes here (PLAN.md, step 1) */
  *lines = NULL;
  *count = 0;
  return UP_OK;
}

/* which kinds of lines a mode needs */
static void
mode_needs (UpMode mode, int *vertical, int *horizontal)
{
  *vertical = mode == UP_MODE_VERTICAL || mode == UP_MODE_FULL;
  *horizontal = mode == UP_MODE_HORIZONTAL || mode == UP_MODE_FULL;
}

UpStatus
up_fit (const UpLine *lines, int count, int width, int height, UpMode mode,
        UpParams *params, UpFitResult *result)
{
  UpFitResult r = { 0 };
  int nv = 0, nh = 0, need_v, need_h;

  if (count < 0 || (count > 0 && ! lines) || width <= 0 || height <= 0 ||
      mode < 0 || mode >= UP_MODE_COUNT || ! params)
    return UP_ERROR;
  memset (params, 0, sizeof *params);

  for (int i = 0; i < count; i++)
    {
      nv += lines[i].type == UP_LINE_VERTICAL;
      nh += lines[i].type == UP_LINE_HORIZONTAL;
    }
  r.vertical = nv;
  r.horizontal = nh;

  /* SKELETON: auto becomes "full" when both kinds are there, else the
   * one there is; the real rule is an open question (PLAN.md) */
  if (mode == UP_MODE_AUTO)
    mode = nv >= UP_MIN_FIT_LINES && nh >= UP_MIN_FIT_LINES ? UP_MODE_FULL
         : nh > nv ? UP_MODE_HORIZONTAL : UP_MODE_VERTICAL;
  r.mode_used = mode;

  mode_needs (mode, &need_v, &need_h);
  if ((need_v && nv < UP_MIN_FIT_LINES) || (need_h && nh < UP_MIN_FIT_LINES) ||
      (mode == UP_MODE_ROTATION && nv + nh < 1))
    r.status = UP_NOT_ENOUGH_LINES;
  else
    r.status = UP_OK;   /* SKELETON: the Nelder-Mead fit comes here (step 2) */

  if (result)
    *result = r;
  return r.status;
}

int
up_params_neutral (const UpParams *p)
{
  return p->rotation == 0.0 && p->shift_v == 0.0 && p->shift_h == 0.0 && p->shear == 0.0;
}

UpStatus
up_homography (const UpParams *params, int width, int height, double matrix[9])
{
  static const double identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

  if (! params || width <= 0 || height <= 0 || ! matrix)
    return UP_ERROR;
  memcpy (matrix, identity, sizeof identity);
  /* SKELETON: ashift's _homography (rotation, shear, lens shifts) comes
   * here (step 2); until then only the identity */
  return up_params_neutral (params) ? UP_OK : UP_ERROR;
}
