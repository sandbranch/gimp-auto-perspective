/* gimp-auto-perspective core: what the parts of the core share, and what
 * the unit tests reach into; not part of the interface (perspective.h)
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef UP_INTERNAL_H
#define UP_INTERNAL_H

#include <stdint.h>
#include <stdlib.h>

#include "perspective.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* a line ready for the cost: end points and the line through them in
 * homogeneous coordinates (L normalised to x^2 + y^2 = 1) */
typedef struct
{
  double p1[3], p2[3], L[3];
  double weight;
  int    vertical;
} UpPrepLine;

/* model.c */
void   up__mat3mul (double *dst, const double *a, const double *b);   /* dst = a b */
void   up__mat3mulv (double *dst, const double *m, const double *v);
void   up__vec3prodn (double *dst, const double *a, const double *b);
void   up__vec3norm (double *dst, const double *v);
void   up__vec3lnorm (double *dst, const double *v);
/* ashift's homography; ashift_offsets 1: its translation exactly (from
 * the pixel positions 0 and width - 1), 0: from our corners (0 and width) */
void   up__homography (const UpParams *p, const UpLens *lens, int width, int height,
                       int ashift_offsets, double *h);
void   up__line_prepare (const UpLine *line, UpPrepLine *p);
int    up__prepare_lines (const UpLine *lines, int count, int kinds, UpPrepLine **out);
double up__cost_matrix (const UpPrepLine *lines, int count, const double h[9]);

/* simplex.c: Nelder-Mead; minimises f over n (<= 8) variables from x
 * (the result is left in x), with an initial regular simplex of that
 * scale, until the standard deviation of the values at the vertices is
 * below eps or after maxiter iterations; constrain (may be NULL) moves a
 * point back into the allowed region. Returns the iterations used
 * (maxiter + 1 if it did not converge, as Hutt's code that ashift uses). */
int up__simplex (double (*f) (const double *x, void *data), double *x, int n, double eps,
                 double scale, int maxiter, void (*constrain) (double *x, int n), void *data);

/* the logit mapping of ashift: [min, max] onto the real line and back */
double up__logit (double x, double min, double max);
double up__ilogit (double l, double min, double max);

/* a small deterministic random generator (xorshift64*), so that RANSAC
 * gives the same result in the CLI and the plug-in */
typedef struct { uint64_t s; } UpRandom;
uint32_t up__random (UpRandom *r);

#endif
