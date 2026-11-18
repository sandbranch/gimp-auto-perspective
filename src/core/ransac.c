/* gimp-auto-perspective core: outlier removal on vanishing points
 *
 * Ported from darktable's src/iop/ashift.c (ransac, _remove_outliers,
 * quickperm, shuffle), "Copyright (C) 2016-2026 darktable developers",
 * GPL-3.0-or-later, read at darktable master, commit 131eca7025 of
 * 2026-11-15. Changed: double instead of float, a seeded random
 * generator of our own instead of rand() (the same result every run),
 * the frame test in our corner based coordinates.
 *
 * ashift's idea: the lines that matter for the fit meet in one vanishing
 * point per direction. Two (random) lines give a candidate vanishing
 * point; the other lines are in if they pass close to it. The candidate
 * with most lines, most weight and least distance wins; epsilon, the
 * hurdle for "close", tunes itself in dry runs first so that about 60 %
 * of the lines would be dropped by a random model.
 *
 * Copyright 2026 David
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "internal.h"

#include <math.h>
#include <string.h>

#define RANSAC_RUNS 400                 /* iterations */
#define RANSAC_EPSILON 2                /* start of epsilon, -log10 */
#define RANSAC_EPSILON_STEP 1           /* step of the epsilon tuning, log10 */
#define RANSAC_ELIMINATION_RATIO 60     /* percent of lines a random model should drop */
#define RANSAC_OPTIMIZATION_STEPS 5
#define RANSAC_OPTIMIZATION_DRY_RUNS 50
#define RANSAC_HURDLE 5                 /* below this, all permutations */
#define RANSAC_SEED 0x5eed5eedcafef00dULL

static inline void
swap_int (int *a, int *b)
{
  const int t = *a;
  *a = *b;
  *b = t;
}

/* the next permutation (Phillips' QuickPerm) */
static int
quickperm (int *a, int *p, int n, int *i)
{
  int j;

  if (*i >= n)
    return 0;
  p[*i]--;
  j = (*i % 2 == 1) ? p[*i] : 0;
  swap_int (&a[j], &a[*i]);
  *i = 1;
  while (p[*i] == 0)
    {
      p[*i] = *i;
      (*i)++;
    }
  return 1;
}

/* Fisher-Yates */
static void
shuffle (int *a, int n, UpRandom *r)
{
  for (int i = 0; i < n; i++)
    swap_int (&a[i + (int) (up__random (r) % (uint32_t) (n - i))], &a[i]);
}

static int
factorial (int n)
{
  return n <= 1 ? 1 : n * factorial (n - 1);
}

/* index_set: the lines of one direction; inout_set: 1 for those kept */
static int
ransac (const UpPrepLine *lines, int *index_set, int *inout_set, int set_count,
        double total_weight, double xmin, double xmax, double ymin, double ymax, UpRandom *rnd)
{
  const size_t set_size = sizeof (int) * (size_t) set_count;
  const int    optiruns = RANSAC_OPTIMIZATION_STEPS * RANSAC_OPTIMIZATION_DRY_RUNS;
  const int    riter = set_count > RANSAC_HURDLE ? RANSAC_RUNS : factorial (set_count);
  double       epsilon = pow (10.0, -RANSAC_EPSILON), epsilon_step = RANSAC_EPSILON_STEP;
  double       best_quality = 0.0;
  int          lines_eliminated = 0, valid_runs = 0, piter = 1;
  int         *best_set, *best_inout, *perm, *inout;

  if (set_count < 3)
    return 1;
  best_set = malloc (set_size);
  best_inout = calloc ((size_t) set_count, sizeof (int));
  perm = malloc (sizeof (int) * ((size_t) set_count + 1));
  inout = calloc ((size_t) set_count, sizeof (int));
  if (! best_set || ! best_inout || ! perm || ! inout)
    {
      free (best_set);
      free (best_inout);
      free (perm);
      free (inout);
      return 0;
    }
  memcpy (best_set, index_set, set_size);
  for (int n = 0; n <= set_count; n++)
    perm[n] = n;

  for (int r = 0; r < optiruns + riter; r++)
    {
      double quality = 0.0, V[3];

      if (set_count > RANSAC_HURDLE || r < optiruns)
        shuffle (index_set, set_count, rnd);
      else
        (void) quickperm (index_set, perm, set_count, &piter);

      /* the model: the intersection of the first two lines */
      up__vec3prodn (V, lines[index_set[0]].L, lines[index_set[1]].L);
      if ((fabs (V[0]) < 1e-10 && fabs (V[1]) < 1e-10 && fabs (V[2]) < 1e-10) ||
          (fabs (V[2]) > 0.0 && V[0] / V[2] >= xmin && V[1] / V[2] >= ymin &&
           V[0] / V[2] <= xmax && V[1] / V[2] <= ymax))
        {
          /* the same line twice, or a vanishing point inside the image:
           * no model that a correction could reach */
          quality = 0.0;
        }
      else
        {
          up__vec3norm (V, V);
          inout[0] = inout[1] = 1;
          for (int n = 2; n < set_count; n++)
            {
              const UpPrepLine *l = &lines[index_set[n]];
              /* not a euclidean distance, but 0 when V lies on L */
              const double d = fabs (V[0] * l->L[0] + V[1] * l->L[1] + V[2] * l->L[2]);

              inout[n] = d < epsilon;
              if (inout[n])
                quality += 0.33 / set_count + 0.33 * l->weight / total_weight
                           + 0.33 * (1.0 - d / epsilon) * set_count * l->weight / total_weight;
              else
                lines_eliminated++;
            }
          valid_runs++;
        }

      if (r < optiruns)
        {
          if (r % RANSAC_OPTIMIZATION_DRY_RUNS == RANSAC_OPTIMIZATION_DRY_RUNS - 1 && valid_runs > 0)
            {
              const double ratio = 100.0 * lines_eliminated / ((double) set_count * valid_runs);

              if (ratio < RANSAC_ELIMINATION_RATIO)
                epsilon = pow (10.0, log10 (epsilon) - epsilon_step);
              else if (ratio > RANSAC_ELIMINATION_RATIO)
                epsilon = pow (10.0, log10 (epsilon) + epsilon_step);
              epsilon_step /= 2.0;
              lines_eliminated = 0;
              valid_runs = 0;
            }
        }
      else if (quality > best_quality)
        {
          memcpy (best_set, index_set, set_size);
          memcpy (best_inout, inout, set_size);
          best_quality = quality;
        }
    }

  memcpy (index_set, best_set, set_size);
  memcpy (inout_set, best_inout, set_size);
  free (inout);
  free (perm);
  free (best_inout);
  free (best_set);
  return 1;
}

void
up_remove_outliers (UpLine *lines, int count, int width, int height)
{
  UpPrepLine *prep;
  int        *set, *inout, *which;
  UpRandom    rnd = { RANSAC_SEED };

  if (! lines || count < 1 || width <= 0 || height <= 0)
    return;
  prep = malloc (sizeof (UpPrepLine) * (size_t) count);
  set = malloc (sizeof (int) * (size_t) count);
  inout = malloc (sizeof (int) * (size_t) count);
  which = malloc (sizeof (int) * (size_t) count);
  if (prep && set && inout && which)
    {
      for (UpLineType t = UP_LINE_VERTICAL; t <= UP_LINE_HORIZONTAL; t++)
        {
          double total = 0.0;
          int    n = 0;

          for (int i = 0; i < count; i++)
            if (lines[i].type == t && ! lines[i].outlier)
              {
                up__line_prepare (&lines[i], &prep[n]);
                which[n] = i;
                set[n] = n;
                inout[n] = 0;
                total += lines[i].weight;
                n++;
              }
          if (n < 3 || ! (total > 0.0))
            continue;
          if (! ransac (prep, set, inout, n, total, 0.0, width, 0.0, height, &rnd))
            continue;
          for (int k = 0; k < n; k++)
            if (! inout[k])
              lines[which[set[k]]].outlier = 1;
        }
    }
  free (prep);
  free (set);
  free (inout);
  free (which);
}
