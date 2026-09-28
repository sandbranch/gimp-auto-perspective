/* gimp-upright core: line based perspective correction ("upright")
 *
 * No GIMP and no GEGL in here, so that the core runs on the command line
 * (tools/upright-cli.c) and in the unit tests as it runs in the plug-in.
 *
 * The method (see PLAN.md and docs/research.md) follows darktable's
 * ashift module (src/iop/ashift.c, GPL-3.0-or-later, after ShiftN by
 * Marcus Hebel): find straight line segments, keep the nearly vertical
 * and nearly horizontal ones, and fit rotation, vertical and horizontal
 * lens shift and shear so that, after the transform, those lines are
 * truly vertical and horizontal.
 *
 * STATUS: skeleton. The types and calls are the planned interface; line
 * detection finds nothing yet, and the fit returns the neutral parameters
 * (the identity transform). up_classify_line is real.
 *
 * Coordinates: pixels, x to the right, y down, (0, 0) the top left
 * corner of the top left pixel. Angles in degrees.
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef UPRIGHT_H
#define UPRIGHT_H

#include <stddef.h>

#define UP_VERSION "0.0.1"

/* what a line segment is for the fit */
typedef enum
{
  UP_LINE_IGNORED    = 0,  /* too short, or too far from vertical and horizontal */
  UP_LINE_VERTICAL   = 1,
  UP_LINE_HORIZONTAL = 2
} UpLineType;

typedef struct
{
  double     x1, y1, x2, y2;  /* end points */
  double     width;           /* of the detected rectangle (LSD); 1 for drawn lines */
  double     weight;          /* how much it counts; ashift: length * width * precision */
  UpLineType type;
} UpLine;

/* the transform, as in ashift: all zero is the identity */
typedef struct
{
  double rotation;  /* degrees; the sign convention is fixed with the real
                       homography (PLAN.md), to match ashift's */
  double shift_v;   /* vertical lens shift (converging verticals), unitless */
  double shift_h;   /* horizontal lens shift (converging horizontals), unitless */
  double shear;     /* unitless */
} UpParams;

/* what to fit (the modes of the plug-in's dialog) */
typedef enum
{
  UP_MODE_AUTO = 0,     /* choose from the lines found: see PLAN.md */
  UP_MODE_VERTICAL,     /* rotation and vertical shift, from vertical lines */
  UP_MODE_HORIZONTAL,   /* rotation and horizontal shift, from horizontal lines */
  UP_MODE_FULL,         /* rotation, both shifts and shear, from all lines */
  UP_MODE_ROTATION,     /* rotation only (levelling), from all lines */
  UP_MODE_COUNT
} UpMode;

typedef enum
{
  UP_OK = 0,
  UP_NOT_ENOUGH_LINES,   /* fewer than UP_MIN_FIT_LINES of the kind needed */
  UP_DID_NOT_CONVERGE,
  UP_INSANE,             /* the result would blow the image up (ashift: area x4) */
  UP_ERROR               /* bad arguments, out of memory */
} UpStatus;

#define UP_MIN_LINE_LENGTH      5.0   /* pixels; ashift MIN_LINE_LENGTH */
#define UP_MAX_DEVIATION       30.0   /* degrees from vertical or horizontal; ashift MAX_TANGENTIAL_DEVIATION */
#define UP_MIN_FIT_LINES        2     /* ashift MINIMUM_FITLINES */

typedef struct
{
  UpStatus status;
  UpMode   mode_used;    /* for UP_MODE_AUTO: what it chose */
  int      vertical;     /* lines of each kind that took part */
  int      horizontal;
  int      iterations;   /* of the Nelder-Mead simplex */
  double   cost_before;  /* the line deviation cost, neutral and fitted */
  double   cost_after;
} UpFitResult;

const char *up_mode_name (UpMode mode);
int         up_mode_from_name (const char *name, UpMode *mode);   /* 1 if known */
const char *up_status_name (UpStatus status);

/* the kind of a segment: vertical, horizontal or ignored (ashift's rule:
 * at least UP_MIN_LINE_LENGTH long and within UP_MAX_DEVIATION degrees) */
UpLineType up_classify_line (const UpLine *line);

/* straight line segments in a gray image (values about 0..1, row after
 * row). Sets *lines (free with free()) and *count. SKELETON: finds none. */
UpStatus up_detect_lines (const float *gray, int width, int height,
                          UpLine **lines, int *count);

/* fits the parameters to the classified lines of an image of that size.
 * SKELETON: returns the neutral parameters. */
UpStatus up_fit (const UpLine *lines, int count, int width, int height,
                 UpMode mode, UpParams *params, UpFitResult *result);

/* the 3x3 matrix (row major, maps input pixel coordinates to output
 * ones, homogeneous) of the parameters for an image of that size, as
 * gimp_item_transform_matrix takes it. SKELETON: only the neutral
 * parameters (the identity) are done; others return UP_ERROR. */
UpStatus up_homography (const UpParams *params, int width, int height, double matrix[9]);

/* 1 if the parameters are all zero: nothing to do */
int up_params_neutral (const UpParams *params);

#endif
