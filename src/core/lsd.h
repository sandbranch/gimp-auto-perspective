/* LSD, the line segment detector of R. Grompone von Gioi, J. Jakubowicz,
 * J.-M. Morel and G. Randall ("LSD: a Line Segment Detector", Image
 * Processing On Line 2 (2012), doi:10.5201/ipol.2012.gjmr-lsd), in C.
 *
 * A port of OpenCV's modules/imgproc/src/lsd.cpp (3-clause BSD licence,
 * kept in lsd.c with the history of that file). No OpenCV and no C++:
 * plain C11 and malloc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef UP_LSD_H
#define UP_LSD_H

typedef struct
{
  double x1, y1, x2, y2;  /* end points; pixels, (0, 0) the top left
                             corner of the top left pixel */
  double width;           /* of the rectangle, pixels */
  double p;               /* angle precision, as a share of pi (LSD's p) */
  double log_nfa;         /* -log10 (number of false alarms) */
} UpLsdSegment;

typedef struct
{
  double scale;        /* the image is scaled by this first (Gaussian), (0, 1] */
  double sigma_scale;  /* sigma of the Gaussian: sigma_scale / scale */
  double quant;        /* bound to the quantisation error of the gradient norm */
  double ang_th;       /* gradient angle tolerance, degrees */
  double log_eps;      /* detection threshold: -log10 (NFA) > log_eps */
  double density_th;   /* minimal density of region points in the rectangle */
  int    n_bins;       /* bins of the pseudo ordering of gradient norms */
} UpLsdParams;

/* the values darktable's ashift uses (scale 0.99, sigma scale 0.6, quant
 * 2, 22.5 degrees, log eps 0, density 0.7, 1024 bins) */
void up_lsd_defaults (UpLsdParams *params);

/* finds the line segments of a gray image (row after row, values about
 * 0..255, as LSD expects them: the gradient threshold depends on that
 * range). Sets *segments (free with free(), NULL if none) and *count.
 * Returns 0, or -1 for bad arguments or out of memory. */
int up_lsd (const double *image, int width, int height, const UpLsdParams *params,
            UpLsdSegment **segments, int *count);

#endif
