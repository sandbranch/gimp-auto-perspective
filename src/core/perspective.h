/* gimp-auto-perspective core: line based perspective correction
 *
 * No GIMP and no GEGL in here, so that the core runs on the command line
 * (tools/auto-perspective-cli.c) and in the unit tests as it runs in the
 * plug-in.
 *
 * The method (see PLAN.md and docs/research.md) follows darktable's
 * ashift module (src/iop/ashift.c, GPL-3.0-or-later, after ShiftN by
 * Marcus Hebel): find straight line segments (LSD, lsd.c), keep the
 * nearly vertical and nearly horizontal ones, drop outliers with RANSAC
 * on their vanishing points, and fit rotation, vertical and horizontal
 * lens shift and shear so that, after the transform, those lines are
 * truly vertical and horizontal.
 *
 * Coordinates: pixels, x to the right, y down, (0, 0) the top left
 * corner of the top left pixel (GIMP's image coordinates). Angles in
 * degrees.
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PERSPECTIVE_H
#define PERSPECTIVE_H

#include <stddef.h>

#define UP_VERSION "0.1.0"

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
  int        outlier;         /* set by up_remove_outliers: not used by the fit */
} UpLine;

/* the transform, as in ashift: all zero is the identity */
typedef struct
{
  double rotation;  /* degrees, as ashift's: positive turns the image
                       counter-clockwise on the screen */
  double shift_v;   /* vertical lens shift (converging verticals), unitless */
  double shift_h;   /* horizontal lens shift (converging horizontals), unitless */
  double shear;     /* unitless */
} UpParams;

/* the lens model of ashift: "generic" is a 28 mm lens (35 mm equivalent),
 * lens dependence 0 and aspect 1; "specific" takes them from the user or
 * the EXIF data */
typedef struct
{
  double focal;      /* focal length, mm, 35 mm equivalent (ashift: f_length * crop_factor) */
  double orthocorr;  /* lens dependence, 0..100 (ashift's "lens dependence") */
  double aspect;     /* aspect adjustment, 1 is none */
} UpLens;

#define UP_DEFAULT_FOCAL 28.0   /* ashift DEFAULT_F_LENGTH */

/* what to fit (the modes of the plug-in's dialog) */
typedef enum
{
  UP_MODE_AUTO = 0,     /* choose from the lines found: see up_fit */
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
#define UP_DETECT_MAX_SIDE   1500     /* detection works on a copy at most this large */

/* the ranges the fit searches (ashift maps each through a logit onto
 * its range): those darktable's ashift fits with today (its "soft"
 * ranges, ROTATION_RANGE_SOFT and so on, set in gui_init and
 * reload_defaults at commit 131eca7025). The narrower +-10, +-1, +-0.2
 * gave the same results on tests/recovery.py, but a Full fit there
 * already needs a rotation of 9.3 degrees. */
#define UP_ROTATION_RANGE     180.0
#define UP_SHIFT_RANGE          2.0
#define UP_SHEAR_RANGE          0.5

typedef struct
{
  UpStatus status;
  UpMode   mode_used;    /* for UP_MODE_AUTO: what it chose */
  int      vertical;     /* lines of each kind that took part */
  int      horizontal;
  int      iterations;   /* of the Nelder-Mead simplex */
  double   cost_before;  /* ashift's line deviation cost, neutral and fitted, */
  double   cost_after;   /* over the lines of the mode used */
} UpFitResult;

const char *up_mode_name (UpMode mode);
int         up_mode_from_name (const char *name, UpMode *mode);   /* 1 if known */
const char *up_status_name (UpStatus status);

/* the generic lens (28 mm, lens dependence 0, aspect 1) */
void up_lens_generic (UpLens *lens);

/* the kind of a segment: vertical, horizontal or ignored (ashift's rule:
 * at least UP_MIN_LINE_LENGTH long and within UP_MAX_DEVIATION degrees) */
UpLineType up_classify_line (const UpLine *line);

/* straight line segments in a gray image (values about 0..1, perceptual,
 * row after row), with LSD as ashift runs it; images larger than
 * UP_DETECT_MAX_SIDE are averaged down first and the segments scaled back.
 * Segments along the border are dropped; each has its weight and type,
 * none is an outlier yet. Sets *lines (free with free(), NULL if none)
 * and *count. */
UpStatus up_detect_lines (const float *gray, int width, int height,
                          UpLine **lines, int *count);

/* marks the vertical and the horizontal lines that do not meet in a
 * common vanishing point (outside the image) as outliers: ashift's
 * RANSAC, with a fixed seed so that the result is the same every time.
 * For detected lines; drawn lines are taken as they are. */
void up_remove_outliers (UpLine *lines, int count, int width, int height);

/* fits the parameters to the lines (the vertical and horizontal ones that
 * are no outliers) of an image of that size; lens NULL is the generic
 * one. On UP_OK *params holds the fit; otherwise the neutral parameters.
 * UP_MODE_AUTO: Vertical, unless Full straightens all lines clearly
 * better and keeps enough of the image (see perspective.c); Horizontal
 * with too few vertical lines; Rotation with too few of both. */
UpStatus up_fit (const UpLine *lines, int count, int width, int height, UpMode mode,
                 const UpLens *lens, UpParams *params, UpFitResult *result);

/* the mode drawn lines call for (PLAN.md 3.5): Full with 2 or more of
 * each kind, Vertical or Horizontal with 2 or more of one kind,
 * Rotation with fewer (it then needs one line at least) */
UpMode up_mode_for_lines (const UpLine *lines, int count);

/* ashift's cost of the parameters for these lines (0 when every used
 * vertical line is vertical and every used horizontal one horizontal):
 * vertical and/or horizontal lines as the flags say (1 vertical,
 * 2 horizontal, 3 both); a negative value for bad arguments */
double up_cost (const UpLine *lines, int count, int width, int height, const UpLens *lens,
                const UpParams *params, int kinds);

/* the 3x3 matrix (row major, maps input pixel coordinates to output
 * ones, homogeneous) of the parameters for an image of that size, as
 * gimp_item_transform_matrix takes it: ashift's homography, translated
 * so that the transformed image's bounding box starts at (0, 0). lens
 * NULL is the generic one. */
UpStatus up_homography (const UpParams *params, const UpLens *lens, int width, int height,
                        double matrix[9]);

/* 1 if the parameters are all zero: nothing to do */
int up_params_neutral (const UpParams *params);

/* matrix helpers: the inverse (returns 0 if singular), and a point
 * through a matrix (returns 0 if it goes to infinity) */
int up_matrix_invert (const double m[9], double inverse[9]);
int up_matrix_apply (const double m[9], double x, double y, double *ox, double *oy);

/* ------------------------------------------------------------ crop and warp */

typedef enum
{
  UP_CROP_NONE = 0,   /* the bounding box of the transformed image */
  UP_CROP_LARGEST,    /* the largest rectangle inside it (ashift: "largest area") */
  UP_CROP_ASPECT,     /* the largest one of the image's shape (ashift: "original format") */
  UP_CROP_COUNT
} UpCrop;

typedef struct
{
  int x, y, width, height;   /* whole pixels, in the output coordinates of a matrix */
} UpRect;

const char *up_crop_name (UpCrop crop);
int         up_crop_from_name (const char *name, UpCrop *crop);   /* 1 if known */

/* the rectangle to keep of an image of that size transformed by the
 * matrix (from up_homography), in its output coordinates: for the
 * largest ones ashift's method (a Nelder-Mead search over the centre
 * and, for UP_CROP_LARGEST, the shape), rounded inwards to whole pixels;
 * for UP_CROP_NONE the bounding box rounded outwards */
UpStatus up_crop (const double matrix[9], int width, int height, UpCrop crop, UpRect *rect);

typedef enum
{
  UP_INTERP_NEAREST = 0,
  UP_INTERP_LINEAR,
  UP_INTERP_CUBIC      /* Catmull-Rom */
} UpInterp;

/* the image (width x height, channels floats per pixel) transformed by
 * the matrix, the part of the output that rect covers: out holds
 * rect->width x rect->height x channels floats. Output pixels whose
 * centre falls outside the input are 0, and *coverage (if not NULL,
 * rect->width x rect->height floats) is 1 inside, 0 outside. */
UpStatus up_warp (const float *in, int width, int height, int channels, const double matrix[9],
                  const UpRect *rect, UpInterp interp, float *out, float *coverage);

#endif
