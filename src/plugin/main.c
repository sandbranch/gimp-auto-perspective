/* gimp-auto-perspective: automatic line based perspective correction for GIMP 3
 *
 *   Image > Transform > Auto Perspective...      plug-in-auto-perspective
 *
 * Finds the straight lines of buildings and the like in the selected
 * layer, fits rotation, lens shift and shear so that the nearly vertical
 * ones become vertical and the nearly horizontal ones horizontal, and
 * transforms the image with that. The work is done by the core in
 * src/core (see perspective.h); this file reads the layer or the guide
 * path, and applies the result (PLAN.md, section 4): one undo group,
 * every layer, channel and path of the image (or only the layer) through
 * gimp_item_transform_matrix, the selection saved, transformed and given
 * back, then the canvas (or the layer) cropped to the core's rectangle.
 *
 * The dialog shows a preview: the layer transformed as it will be, the
 * lines used (vertical green, horizontal blue, outliers grey) and the
 * crop; below it the settings and, in "Fine tuning", offsets added to
 * the fitted values (also arguments, so scripts can use them).
 *
 * Copyright 2026 David
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include <stdlib.h>
#include <string.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "perspective.h"

#define PROC_AUTO_PERSPECTIVE   "plug-in-auto-perspective"
#define PLUG_IN_BINARY "gimp-auto-perspective"
#define MENU_PATH      "<Image>/Image/Transform"

/* the modes of the dialog: the core's, and "guided" (lines from a path) */
enum { MODE_GUIDED = UP_MODE_COUNT };
/* (the same numbers as UpCrop) */
enum { CROP_NONE = UP_CROP_NONE, CROP_LARGEST = UP_CROP_LARGEST, CROP_ASPECT = UP_CROP_ASPECT };
enum { APPLY_IMAGE, APPLY_LAYER };

typedef struct _AutoPerspective      AutoPerspective;
typedef struct _AutoPerspectiveClass AutoPerspectiveClass;

struct _AutoPerspective
{
  GimpPlugIn parent_instance;
};

struct _AutoPerspectiveClass
{
  GimpPlugInClass parent_class;
};

#define AUTO_PERSPECTIVE_TYPE (auto_perspective_get_type ())
GType auto_perspective_get_type (void) G_GNUC_CONST;

static GList          *auto_perspective_query_procedures (GimpPlugIn *plug_in);
static GimpProcedure  *auto_perspective_create_procedure (GimpPlugIn *plug_in, const gchar *name);
static GimpValueArray *auto_perspective_run (GimpProcedure *procedure, GimpRunMode run_mode,
                                             GimpImage *image, GimpDrawable **drawables,
                                             GimpProcedureConfig *config, gpointer run_data);

G_DEFINE_TYPE (AutoPerspective, auto_perspective, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (AUTO_PERSPECTIVE_TYPE)

/* no translations yet */
static gboolean
auto_perspective_set_i18n (GimpPlugIn *plug_in, const gchar *procedure_name, gchar **gettext_domain,
                           gchar **catalog_dir)
{
  return FALSE;
}

static void
auto_perspective_class_init (AutoPerspectiveClass *klass)
{
  GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

  plug_in_class->set_i18n = auto_perspective_set_i18n;
  plug_in_class->query_procedures = auto_perspective_query_procedures;
  plug_in_class->create_procedure = auto_perspective_create_procedure;
}

static void
auto_perspective_init (AutoPerspective *self)
{
}

static GList *
auto_perspective_query_procedures (GimpPlugIn *plug_in)
{
  return g_list_append (NULL, g_strdup (PROC_AUTO_PERSPECTIVE));
}

static GimpProcedure *
auto_perspective_create_procedure (GimpPlugIn *plug_in, const gchar *name)
{
  GimpProcedure *procedure;

  if (strcmp (name, PROC_AUTO_PERSPECTIVE))
    return NULL;

  procedure = gimp_image_procedure_new (plug_in, name, GIMP_PDB_PROC_TYPE_PLUGIN,
                                        auto_perspective_run, NULL, NULL);
  /* (indexed images: no; transforming them would resample the palette
   * indices) */
  gimp_procedure_set_image_types (procedure, "RGB*, GRAY*");
  gimp_procedure_set_sensitivity_mask (procedure, GIMP_PROCEDURE_SENSITIVE_DRAWABLE);
  gimp_procedure_set_menu_label (procedure, "_Auto Perspective...");
  gimp_procedure_add_menu_path (procedure, MENU_PATH);
  gimp_procedure_set_attribution (procedure, "David", "David, GPL-3.0-or-later", "2026");
  gimp_procedure_set_documentation (procedure,
    "Straighten converging and tilted lines automatically (perspective correction)",
    "Finds the straight lines in the layer, keeps the nearly vertical and nearly "
    "horizontal ones, and fits a rotation, a vertical and a horizontal lens shift and a "
    "shear that make them truly vertical and horizontal, as darktable's rotate and "
    "perspective module (after ShiftN) does; then transforms the image (or only the "
    "layer) and crops it. The guided mode uses the strokes of a path as the lines "
    "instead. With too few lines nothing is changed and the status says so.",
    name);

  gimp_procedure_add_choice_argument (procedure, "mode", "_Mode",
    "What to straighten",
    gimp_choice_new_with_values (
      "auto", UP_MODE_AUTO, "Automatic",
        "Vertical, or Full when that straightens all lines clearly better and keeps enough of the image",
      "vertical", UP_MODE_VERTICAL, "Vertical", "Rotation and vertical lens shift: converging verticals",
      "horizontal", UP_MODE_HORIZONTAL, "Horizontal", "Rotation and horizontal lens shift",
      "full", UP_MODE_FULL, "Full", "Rotation, both lens shifts and shear",
      "rotation", UP_MODE_ROTATION, "Rotation only", "Level the image, no perspective change",
      "guided", MODE_GUIDED, "Guided by a path",
        "The strokes of the path are the lines (from their first to their last anchor; a "
        "closed stroke of 4 anchors gives its 4 sides)",
      NULL),
    "auto", G_PARAM_READWRITE);
  gimp_procedure_add_path_argument (procedure, "path", "_Guide path",
    "For the guided mode: a path whose strokes lie along lines that should be "
    "vertical or horizontal",
    TRUE, G_PARAM_READWRITE);
  gimp_procedure_add_choice_argument (procedure, "crop", "_Crop",
    "What to keep of the transformed image",
    gimp_choice_new_with_values (
      "none", CROP_NONE, "Nothing: enlarge to show all", NULL,
      "largest", CROP_LARGEST, "The largest rectangle", "Inside the transformed image, no transparent corners",
      "aspect", CROP_ASPECT, "The largest rectangle of the same shape", "Keeps the aspect ratio",
      NULL),
    "largest", G_PARAM_READWRITE);
  gimp_procedure_add_choice_argument (procedure, "apply-to", "_Apply to",
    "Transform the whole image or only the layer",
    gimp_choice_new_with_values (
      "image", APPLY_IMAGE, "The whole image", "All layers, channels and paths; the canvas follows the crop",
      "layer", APPLY_LAYER, "The layer", "Only the layer the lines were found in",
      NULL),
    "image", G_PARAM_READWRITE);
  gimp_procedure_add_enum_argument (procedure, "interpolation", "_Interpolation",
    "How the pixels are resampled",
    GIMP_TYPE_INTERPOLATION_TYPE, GIMP_INTERPOLATION_CUBIC, G_PARAM_READWRITE);
  gimp_procedure_add_double_argument (procedure, "focal-length", "_Focal length",
    "Of the lens, mm, 35 mm equivalent; 0: from the image's EXIF data, else 28 mm "
    "(ashift's generic lens). Only changes the result with a lens dependence above 0",
    0.0, 2000.0, 0.0, G_PARAM_READWRITE);
  gimp_procedure_add_double_argument (procedure, "lens-dependence", "_Lens dependence",
    "ashift's lens dependence (orthocorr), 0..100: how much the correction also "
    "compresses across the shift; 0 is ashift's generic lens",
    0.0, 100.0, 0.0, G_PARAM_READWRITE);
  gimp_procedure_add_double_argument (procedure, "adjust-rotation", "_Rotation",
    "Fine tuning: degrees added to the fitted rotation",
    -10.0, 10.0, 0.0, G_PARAM_READWRITE);
  gimp_procedure_add_double_argument (procedure, "adjust-shift-v", "_Vertical shift",
    "Fine tuning: added to the fitted vertical lens shift",
    -1.0, 1.0, 0.0, G_PARAM_READWRITE);
  gimp_procedure_add_double_argument (procedure, "adjust-shift-h", "_Horizontal shift",
    "Fine tuning: added to the fitted horizontal lens shift",
    -1.0, 1.0, 0.0, G_PARAM_READWRITE);
  gimp_procedure_add_double_argument (procedure, "adjust-shear", "_Shear",
    "Fine tuning: added to the fitted shear",
    -0.5, 0.5, 0.0, G_PARAM_READWRITE);

  gimp_procedure_add_string_return_value (procedure, "status", "Status",
    "What the fit did: ok, not-enough-lines, did-not-converge, insane",
    NULL, G_PARAM_READWRITE);
  gimp_procedure_add_double_return_value (procedure, "rotation", "Rotation",
    "The rotation applied (found plus fine tuning), in degrees", -190.0, 190.0, 0.0, G_PARAM_READWRITE);
  gimp_procedure_add_double_return_value (procedure, "shift-v", "Vertical lens shift",
    "The vertical lens shift found", -10.0, 10.0, 0.0, G_PARAM_READWRITE);
  gimp_procedure_add_double_return_value (procedure, "shift-h", "Horizontal lens shift",
    "The horizontal lens shift found", -10.0, 10.0, 0.0, G_PARAM_READWRITE);
  gimp_procedure_add_double_return_value (procedure, "shear", "Shear",
    "The shear found", -10.0, 10.0, 0.0, G_PARAM_READWRITE);
  gimp_procedure_add_string_return_value (procedure, "mode-used", "Mode used",
    "The mode that was fitted (for auto and guided: the one chosen)",
    NULL, G_PARAM_READWRITE);
  return procedure;
}

/* ------------------------------------------------------------ input */

/* the drawable as gray values, "Y' float" (perceptual, as LSD expects) */
static float *
read_gray (GimpDrawable *drawable, int *w, int *h)
{
  GeglBuffer *buffer = gimp_drawable_get_buffer (drawable);
  float      *gray;

  *w = gimp_drawable_get_width (drawable);
  *h = gimp_drawable_get_height (drawable);
  gray = g_try_malloc (sizeof (float) * (gsize) *w * *h);
  if (gray)
    gegl_buffer_get (buffer, GEGL_RECTANGLE (0, 0, *w, *h), 1.0, babl_format ("Y' float"),
                     gray, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
  g_object_unref (buffer);
  return gray;
}

/* the strokes of a path as segments, in the drawable's coordinates: from
 * the first to the last anchor of each stroke (a Bezier stroke is a list
 * of control, anchor, control triples); a closed stroke of 4 anchors
 * gives its 4 sides (ashift's "draw rectangle") */
static UpLine *
path_lines (GimpPath *path, GimpDrawable *drawable, int *count)
{
  gsize    n_strokes = 0;
  gint    *strokes = gimp_path_get_strokes (path, &n_strokes);
  UpLine  *lines = g_new0 (UpLine, MAX (4 * n_strokes, 1));
  gint     ox, oy;

  gimp_drawable_get_offsets (drawable, &ox, &oy);
  *count = 0;
  for (gsize i = 0; i < n_strokes; i++)
    {
      gsize     n = 0;
      gdouble  *pts = NULL;
      gboolean  closed = FALSE;
      gint      anchors;

      gimp_path_stroke_get_points (path, strokes[i], &n, &pts, &closed);
      anchors = (gint) (n / 6);
      for (gint k = 0; k < (closed && anchors == 4 ? 4 : anchors >= 2 ? 1 : 0); k++)
        {
          const gint a = closed && anchors == 4 ? k : 0;
          const gint b = closed && anchors == 4 ? (k + 1) % 4 : anchors - 1;
          UpLine    *l = &lines[*count];

          l->x1 = pts[6 * a + 2] - ox;
          l->y1 = pts[6 * a + 3] - oy;
          l->x2 = pts[6 * b + 2] - ox;
          l->y2 = pts[6 * b + 3] - oy;
          l->width = 1.0;
          l->weight = 1.0;
          l->type = up_classify_line (l);
          l->outlier = 0;
          (*count)++;
        }
      g_free (pts);
    }
  g_free (strokes);
  return lines;
}

/* the focal length, 35 mm equivalent, from the EXIF data; 0 if none */
static double
exif_focal (GimpImage *image)
{
  GimpMetadata *md = gimp_image_get_metadata (image);
  GError       *error = NULL;
  glong         f = 0;

  if (! md)
    return 0.0;
  if (gexiv2_metadata_try_has_tag (GEXIV2_METADATA (md), "Exif.Photo.FocalLengthIn35mmFilm", NULL))
    f = gexiv2_metadata_try_get_tag_long (GEXIV2_METADATA (md), "Exif.Photo.FocalLengthIn35mmFilm", &error);
  g_clear_error (&error);
  return f > 0 && f < 5000 ? (double) f : 0.0;
}

/* ------------------------------------------------------------ applying */

static void
mat3mul (double *dst, const double *a, const double *b)
{
  double r[9];

  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      r[3 * i + j] = a[3 * i] * b[j] + a[3 * i + 1] * b[3 + j] + a[3 * i + 2] * b[6 + j];
  memcpy (dst, r, sizeof r);
}

static void
transform_item (GimpItem *item, const double *m)
{
  gimp_item_transform_matrix (item, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
}

/* transforms the image (or the layer) with the matrix m of the layer (in
 * the layer's own coordinates, w x h) and crops as asked; one undo group */
static gboolean
apply (GimpImage *image, GimpLayer *layer, const double m[9], int w, int h, int crop, int apply_to,
       GimpInterpolationType interp, GError **error)
{
  GimpChannel *saved = NULL;
  double       t[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, ti[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, hm[9];
  UpRect       r;
  gint         ox, oy;

  if (up_crop (m, w, h, (UpCrop) crop, &r) != UP_OK)
    {
      g_set_error (error, GIMP_PLUG_IN_ERROR, 0, "Auto Perspective: no sensible crop for this result.");
      return FALSE;
    }
  /* the matrix in image coordinates: from the image to the layer's,
   * the layer's matrix, back */
  gimp_drawable_get_offsets (GIMP_DRAWABLE (layer), &ox, &oy);
  t[2] = ox;
  t[5] = oy;
  ti[2] = -ox;
  ti[5] = -oy;
  mat3mul (hm, t, m);
  mat3mul (hm, hm, ti);
  r.x += ox;
  r.y += oy;

  gimp_image_undo_group_start (image);
  gimp_context_push ();
  gimp_context_set_interpolation (interp);
  gimp_context_set_transform_direction (GIMP_TRANSFORM_FORWARD);
  gimp_context_set_transform_resize (GIMP_TRANSFORM_RESIZE_ADJUST);

  /* a selection would make the transform cut a floating selection: keep
   * it as a channel, transform that with the rest, and give it back */
  if (! gimp_selection_is_empty (image))
    {
      saved = gimp_selection_save (image);
      gimp_selection_none (image);
    }

  if (apply_to == APPLY_IMAGE)
    {
      GimpLayer   **layers = gimp_image_get_layers (image);
      GimpChannel **channels = gimp_image_get_channels (image);
      GimpPath    **paths = gimp_image_get_paths (image);
      gint          ux = G_MAXINT, uy = G_MAXINT;

      for (gint i = 0; layers && layers[i]; i++)
        transform_item (GIMP_ITEM (layers[i]), hm);
      for (gint i = 0; channels && channels[i]; i++)
        transform_item (GIMP_ITEM (channels[i]), hm);
      for (gint i = 0; paths && paths[i]; i++)
        transform_item (GIMP_ITEM (paths[i]), hm);

      if (crop == CROP_NONE)
        /* the canvas: the transformed image's bounding box */
        gimp_image_resize (image, r.width, r.height, -r.x, -r.y);
      else
        {
          /* the canvas around all layers first, so that the crop
           * rectangle lies inside it, then the crop (which crops the
           * layers too); the first moves the image origin to the
           * layers' top left corner */
          for (gint i = 0; layers && layers[i]; i++)
            {
              gint lx, ly;
              gimp_drawable_get_offsets (GIMP_DRAWABLE (layers[i]), &lx, &ly);
              ux = MIN (ux, lx);
              uy = MIN (uy, ly);
            }
          if (ux == G_MAXINT)
            ux = uy = 0;
          gimp_image_resize_to_layers (image);
          gimp_image_crop (image, r.width, r.height, r.x - ux, r.y - uy);
        }
      g_free (layers);
      g_free (channels);
      g_free (paths);
    }
  else
    {
      transform_item (GIMP_ITEM (layer), hm);
      if (saved)
        transform_item (GIMP_ITEM (saved), hm);
      if (crop != CROP_NONE)
        {
          gint lx, ly;
          gimp_drawable_get_offsets (GIMP_DRAWABLE (layer), &lx, &ly);
          gimp_layer_resize (layer, r.width, r.height, lx - r.x, ly - r.y);
        }
    }

  if (saved)
    {
      gimp_image_select_item (image, GIMP_CHANNEL_OP_REPLACE, GIMP_ITEM (saved));
      gimp_image_remove_channel (image, saved);
    }
  gimp_context_pop ();
  gimp_image_undo_group_end (image);
  return TRUE;
}

/* ------------------------------------------------------------ settings */

typedef struct
{
  gint                  mode, crop, apply_to;
  GimpInterpolationType interp;
  GimpPath             *path;      /* (owned) */
  UpLens                lens;
  UpParams              adjust;
} Settings;

static void
settings_read (GimpProcedureConfig *config, GimpImage *image, Settings *st)
{
  gdouble focal, orthocorr;

  st->mode = gimp_procedure_config_get_choice_id (config, "mode");
  st->crop = gimp_procedure_config_get_choice_id (config, "crop");
  st->apply_to = gimp_procedure_config_get_choice_id (config, "apply-to");
  g_object_get (config, "path", &st->path, "interpolation", &st->interp,
                "focal-length", &focal, "lens-dependence", &orthocorr,
                "adjust-rotation", &st->adjust.rotation, "adjust-shift-v", &st->adjust.shift_v,
                "adjust-shift-h", &st->adjust.shift_h, "adjust-shear", &st->adjust.shear, NULL);
  up_lens_generic (&st->lens);
  if (focal <= 0.0)
    focal = exif_focal (image);
  if (focal > 0.0)
    st->lens.focal = focal;
  st->lens.orthocorr = orthocorr;
}

/* the detected lines of the drawable, outliers marked (g_free them);
 * FALSE for out of memory */
static gboolean
detect (GimpDrawable *drawable, UpLine **lines, gint *count)
{
  gint    w, h;
  float  *gray = read_gray (drawable, &w, &h);
  UpLine *found = NULL;

  *lines = NULL;
  *count = 0;
  if (! gray || up_detect_lines (gray, w, h, &found, count) != UP_OK)
    {
      g_free (gray);
      return FALSE;
    }
  g_free (gray);
  up_remove_outliers (found, *count, w, h);
  /* (the core's lines are malloc'ed, the path's g_new'ed: one kind) */
  *lines = g_memdup2 (found, sizeof (UpLine) * (gsize) MAX (*count, 1));
  free (found);
  return TRUE;
}

/* the parameters to apply: the fit (if it succeeded) plus the fine
 * tuning; *apply is FALSE when they are neutral */
static UpStatus
compute (const UpLine *lines, gint count, gint w, gint h, gint mode, const Settings *st,
         UpParams *params, UpFitResult *fit, gboolean *apply)
{
  UpStatus status = up_fit (lines, count, w, h, (UpMode) mode, &st->lens, params, fit);

  params->rotation += st->adjust.rotation;
  params->shift_v += st->adjust.shift_v;
  params->shift_h += st->adjust.shift_h;
  params->shear += st->adjust.shear;
  *apply = ! up_params_neutral (params) && (status == UP_OK || ! up_params_neutral (&st->adjust));
  if (! *apply && status != UP_OK)
    memset (params, 0, sizeof *params);
  return status;
}

static const gchar *
status_text (UpStatus status)
{
  switch (status)
    {
    case UP_OK:               return "ok";
    case UP_NOT_ENOUGH_LINES: return "not enough lines";
    case UP_INSANE:           return "the result would distort the image too much";
    case UP_DID_NOT_CONVERGE: return "the fit did not converge";
    default:                  return "an error";
    }
}

/* ------------------------------------------------------------ preview */

#define PREVIEW_SIZE 400   /* the longer side of the preview copy */

typedef struct
{
  GimpProcedureConfig *config;
  GimpImage           *image;
  GimpDrawable        *drawable;
  gint                 w, h;          /* the layer */
  UpLine              *detected;      /* lazily, once */
  gint                 ndetected;
  gboolean             detected_done;
  float               *small;         /* the layer, scaled by s, R'G'B' float */
  gint                 sw, sh;
  gdouble              s;
  GtkWidget           *area, *label;
  guint                idle;
  /* the current result */
  UpLine              *lines;         /* those shown: detected or drawn */
  gint                 count;
  UpParams             params;
  gboolean             apply;
  double               ms[9];         /* the matrix in preview coordinates */
  UpRect               box;           /* the transformed preview copy */
  UpRect               crop;          /* the crop, full size output coordinates */
  gboolean             have_crop;
  cairo_surface_t     *surface;
} Preview;

static void
preview_compute (Preview *p)
{
  Settings    st;
  UpFitResult fit = { 0 };
  UpStatus    status;
  double      m[9], sm[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, smi[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  gint        mode;
  gchar      *text;

  settings_read (p->config, p->image, &st);
  g_clear_pointer (&p->lines, g_free);
  p->count = 0;
  mode = st.mode;
  if (mode == MODE_GUIDED)
    {
      if (st.path)
        p->lines = path_lines (st.path, p->drawable, &p->count);
      mode = up_mode_for_lines (p->lines, p->count);
    }
  else
    {
      if (! p->detected_done)
        {
          detect (p->drawable, &p->detected, &p->ndetected);
          p->detected_done = TRUE;
        }
      if (p->detected)
        p->lines = g_memdup2 (p->detected, sizeof (UpLine) * (gsize) MAX (p->ndetected, 1));
      p->count = p->ndetected;
    }
  g_clear_object (&st.path);

  status = compute (p->lines, p->count, p->w, p->h, mode, &st, &p->params, &fit, &p->apply);
  if (up_homography (&p->params, &st.lens, p->w, p->h, m) != UP_OK)
    {
      memset (&p->params, 0, sizeof p->params);
      p->apply = FALSE;
      up_homography (&p->params, &st.lens, p->w, p->h, m);
    }
  p->have_crop = up_crop (m, p->w, p->h, (UpCrop) st.crop, &p->crop) == UP_OK;

  /* the same matrix for the preview copy: S m S^-1 */
  sm[0] = sm[4] = p->s;
  smi[0] = smi[4] = 1.0 / p->s;
  up_matrix_invert (sm, smi);
  {
    double t[9];
    /* (written out: t = sm * m * smi) */
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
        t[3 * i + j] = sm[3 * i] * m[j] + sm[3 * i + 1] * m[3 + j] + sm[3 * i + 2] * m[6 + j];
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
        p->ms[3 * i + j] = t[3 * i] * smi[j] + t[3 * i + 1] * smi[3 + j] + t[3 * i + 2] * smi[6 + j];
  }

  g_clear_pointer (&p->surface, cairo_surface_destroy);
  if (p->small && up_crop (p->ms, p->sw, p->sh, UP_CROP_NONE, &p->box) == UP_OK &&
      p->box.width > 0 && p->box.height > 0 && p->box.width <= 8 * PREVIEW_SIZE &&
      p->box.height <= 8 * PREVIEW_SIZE)
    {
      float *out = g_try_malloc (sizeof (float) * 3 * (gsize) p->box.width * p->box.height);
      float *cov = g_try_malloc (sizeof (float) * (gsize) p->box.width * p->box.height);

      if (out && cov &&
          up_warp (p->small, p->sw, p->sh, 3, p->ms, &p->box, UP_INTERP_LINEAR, out, cov) == UP_OK)
        {
          cairo_surface_t *surf = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, p->box.width,
                                                              p->box.height);
          guchar          *data = cairo_image_surface_get_data (surf);
          const gint       stride = cairo_image_surface_get_stride (surf);

          cairo_surface_flush (surf);
          for (gint y = 0; y < p->box.height; y++)
            for (gint x = 0; x < p->box.width; x++)
              {
                const gsize k = (gsize) y * p->box.width + x;
                guint32    *px = (guint32 *) (data + (gsize) y * stride) + x;
                const float a = cov[k];
                guint32     v[3];

                for (gint c = 0; c < 3; c++)
                  v[c] = (guint32) (CLAMP (out[3 * k + c], 0.0f, 1.0f) * a * 255.0f + 0.5f);
                /* premultiplied ARGB, native endian */
                *px = ((guint32) (a * 255.0f + 0.5f) << 24) | (v[0] << 16) | (v[1] << 8) | v[2];
              }
          cairo_surface_mark_dirty (surf);
          p->surface = surf;
        }
      g_free (out);
      g_free (cov);
    }

  {
    gint used_v = 0, used_h = 0, out = 0;
    for (gint i = 0; i < p->count; i++)
      {
        used_v += p->lines[i].type == UP_LINE_VERTICAL && ! p->lines[i].outlier;
        used_h += p->lines[i].type == UP_LINE_HORIZONTAL && ! p->lines[i].outlier;
        out += p->lines[i].type != UP_LINE_IGNORED && p->lines[i].outlier;
      }
    if (st.mode == MODE_GUIDED && p->count == 0)
      text = g_strdup ("Guided: choose a path whose strokes lie along lines that should be "
                       "vertical or horizontal.");
    else if (status == UP_OK || p->apply)
      text = g_strdup_printf ("%s: %d vertical and %d horizontal lines (%d dropped as outliers). "
                              "Rotation %.2f\302\260, vertical shift %.3f, horizontal shift %.3f, "
                              "shear %.3f%s",
                              up_mode_name (fit.mode_used), used_v, used_h, out, p->params.rotation,
                              p->params.shift_v, p->params.shift_h, p->params.shear,
                              status == UP_OK ? "" : " (fine tuning only: the fit failed)");
    else
      text = g_strdup_printf ("Nothing to do: %s (%d vertical and %d horizontal lines; at least %d "
                              "of a kind are needed).", status_text (status), used_v, used_h,
                              UP_MIN_FIT_LINES);
    gtk_label_set_text (GTK_LABEL (p->label), text);
    g_free (text);
  }
  gtk_widget_queue_draw (p->area);
}

static gboolean
preview_idle (gpointer data)
{
  Preview *p = data;

  p->idle = 0;
  preview_compute (p);
  return G_SOURCE_REMOVE;
}

static void
preview_queue (Preview *p)
{
  if (! p->idle)
    p->idle = g_idle_add (preview_idle, p);
}

static gboolean
preview_draw (GtkWidget *widget, cairo_t *cr, Preview *p)
{
  const gint aw = gtk_widget_get_allocated_width (widget);
  const gint ah = gtk_widget_get_allocated_height (widget);
  gdouble    k;

  cairo_set_source_rgb (cr, 0.25, 0.25, 0.25);
  cairo_paint (cr);
  if (! p->surface || p->box.width <= 0 || p->box.height <= 0)
    return TRUE;

  /* the transformed copy fitted into the widget */
  k = MIN ((aw - 8.0) / p->box.width, (ah - 8.0) / p->box.height);
  if (k <= 0.0)
    return TRUE;
  cairo_save (cr);
  cairo_translate (cr, 0.5 * (aw - k * p->box.width), 0.5 * (ah - k * p->box.height));
  cairo_scale (cr, k, k);
  cairo_translate (cr, -p->box.x, -p->box.y);
  cairo_set_source_surface (cr, p->surface, p->box.x, p->box.y);
  cairo_paint (cr);

  /* outside the crop darker, the crop's outline */
  if (p->have_crop)
    {
      const gdouble cx = p->crop.x * p->s, cy = p->crop.y * p->s;
      const gdouble cw = p->crop.width * p->s, ch = p->crop.height * p->s;

      cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
      cairo_rectangle (cr, p->box.x, p->box.y, p->box.width, p->box.height);
      cairo_rectangle (cr, cx, cy, cw, ch);
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.45);
      cairo_fill (cr);
      cairo_rectangle (cr, cx, cy, cw, ch);
      cairo_set_source_rgb (cr, 1.0, 0.85, 0.2);
      cairo_set_line_width (cr, 1.5 / k);
      cairo_stroke (cr);
    }

  /* the lines, through the same transform: vertical green, horizontal
   * blue, outliers grey (as ashift draws them) */
  cairo_set_line_width (cr, 1.5 / k);
  for (gint i = 0; i < p->count; i++)
    {
      const UpLine *l = &p->lines[i];
      double        x1, y1, x2, y2;

      if (l->type == UP_LINE_IGNORED)
        continue;
      if (! up_matrix_apply (p->ms, l->x1 * p->s, l->y1 * p->s, &x1, &y1) ||
          ! up_matrix_apply (p->ms, l->x2 * p->s, l->y2 * p->s, &x2, &y2))
        continue;
      if (l->outlier)
        cairo_set_source_rgba (cr, 0.7, 0.7, 0.7, 0.8);
      else if (l->type == UP_LINE_VERTICAL)
        cairo_set_source_rgb (cr, 0.15, 0.85, 0.25);
      else
        cairo_set_source_rgb (cr, 0.2, 0.55, 1.0);
      cairo_move_to (cr, x1, y1);
      cairo_line_to (cr, x2, y2);
      cairo_stroke (cr);
    }
  cairo_restore (cr);
  return TRUE;
}

static gboolean
run_dialog (GimpProcedure *procedure, GimpProcedureConfig *config, GimpImage *image,
            GimpDrawable *drawable)
{
  GtkWidget  *dialog, *content;
  Preview     p = { 0 };
  GeglBuffer *buffer;
  gboolean    ok;

  gimp_ui_init (PLUG_IN_BINARY);
  /* the fine tuning belongs to one image: start from the fit */
  g_object_set (config, "adjust-rotation", 0.0, "adjust-shift-v", 0.0, "adjust-shift-h", 0.0,
                "adjust-shear", 0.0, NULL);
  dialog = gimp_procedure_dialog_new (procedure, config, "Auto Perspective");

  p.config = config;
  p.image = image;
  p.drawable = drawable;
  p.w = gimp_drawable_get_width (drawable);
  p.h = gimp_drawable_get_height (drawable);
  p.s = MIN (1.0, (gdouble) PREVIEW_SIZE / MAX (p.w, p.h));
  p.sw = MAX (1, (gint) (p.w * p.s));
  p.sh = MAX (1, (gint) (p.h * p.s));
  p.small = g_try_malloc (sizeof (float) * 3 * (gsize) p.sw * p.sh);
  if (p.small)
    {
      buffer = gimp_drawable_get_buffer (drawable);
      gegl_buffer_get (buffer, GEGL_RECTANGLE (0, 0, p.sw, p.sh), p.s, babl_format ("R'G'B' float"),
                       p.small, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_CLAMP);
      g_object_unref (buffer);
    }

  /* the preview and the status line on top (fill packs below them) */
  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  p.area = gtk_drawing_area_new ();
  gtk_widget_set_size_request (p.area, 480, 360);
  gtk_widget_set_margin_bottom (p.area, 6);
  g_signal_connect (p.area, "draw", G_CALLBACK (preview_draw), &p);
  gtk_box_pack_start (GTK_BOX (content), p.area, TRUE, TRUE, 0);
  gtk_widget_show (p.area);
  p.label = gtk_label_new ("Finding lines...");
  gtk_label_set_line_wrap (GTK_LABEL (p.label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (p.label), 0.0);
  gtk_label_set_max_width_chars (GTK_LABEL (p.label), 60);
  gtk_widget_set_margin_bottom (p.label, 6);
  gtk_box_pack_start (GTK_BOX (content), p.label, FALSE, FALSE, 0);
  gtk_widget_show (p.label);

  gimp_procedure_dialog_get_label (GIMP_PROCEDURE_DIALOG (dialog), "fine-title", "Fine tuning",
                                   FALSE, FALSE);
  gimp_procedure_dialog_fill_box (GIMP_PROCEDURE_DIALOG (dialog), "fine-box",
                                  "adjust-rotation", "adjust-shift-v", "adjust-shift-h",
                                  "adjust-shear", "focal-length", "lens-dependence", NULL);
  gimp_procedure_dialog_fill_expander (GIMP_PROCEDURE_DIALOG (dialog), "fine-expander",
                                       "fine-title", FALSE, "fine-box");
  gimp_procedure_dialog_fill (GIMP_PROCEDURE_DIALOG (dialog), "mode", "path", "crop",
                              "apply-to", "interpolation", "fine-expander", NULL);

  g_signal_connect_swapped (config, "notify", G_CALLBACK (preview_queue), &p);
  preview_queue (&p);

  ok = gimp_procedure_dialog_run (GIMP_PROCEDURE_DIALOG (dialog));

  g_signal_handlers_disconnect_by_data (config, &p);
  if (p.idle)
    g_source_remove (p.idle);
  gtk_widget_destroy (dialog);
  g_clear_pointer (&p.surface, cairo_surface_destroy);
  g_free (p.lines);
  g_free (p.detected);
  g_free (p.small);
  return ok;
}

/* ------------------------------------------------------------ run */

static GimpValueArray *
auto_perspective_run (GimpProcedure *procedure, GimpRunMode run_mode, GimpImage *image,
                      GimpDrawable **drawables, GimpProcedureConfig *config, gpointer run_data)
{
  GimpValueArray *ret;
  GimpDrawable   *drawable;
  UpLine         *lines = NULL;
  UpParams        params = { 0 };
  UpFitResult     fit = { 0 };
  UpStatus        status;
  Settings        st;
  GError         *error = NULL;
  gint            mode, count = 0, w, h;
  gboolean        apply_it;
  double          m[9];

  if (gimp_core_object_array_get_length ((GObject **) drawables) != 1 ||
      ! GIMP_IS_LAYER (drawables[0]))
    {
      g_set_error (&error, GIMP_PLUG_IN_ERROR, 0, "Auto Perspective works on one layer: select one.");
      return gimp_procedure_new_return_values (procedure, GIMP_PDB_CALLING_ERROR, error);
    }
  drawable = drawables[0];
  if (gimp_drawable_is_indexed (drawable))
    {
      g_set_error (&error, GIMP_PLUG_IN_ERROR, 0,
                   "Auto Perspective works on RGB and gray images; convert indexed images first.");
      return gimp_procedure_new_return_values (procedure, GIMP_PDB_CALLING_ERROR, error);
    }

  if (run_mode == GIMP_RUN_INTERACTIVE && ! run_dialog (procedure, config, image, drawable))
    return gimp_procedure_new_return_values (procedure, GIMP_PDB_CANCEL, NULL);

  settings_read (config, image, &st);
  mode = st.mode;
  w = gimp_drawable_get_width (drawable);
  h = gimp_drawable_get_height (drawable);

  gimp_progress_init ("Auto Perspective: finding lines");
  if (mode == MODE_GUIDED)
    {
      if (! st.path)
        {
          g_set_error (&error, GIMP_PLUG_IN_ERROR, 0,
                       "The guided mode needs a path whose strokes lie along the lines.");
          return gimp_procedure_new_return_values (procedure, GIMP_PDB_CALLING_ERROR, error);
        }
      lines = path_lines (st.path, drawable, &count);
      mode = up_mode_for_lines (lines, count);
    }
  else if (! detect (drawable, &lines, &count))
    {
      g_clear_object (&st.path);
      g_set_error (&error, GIMP_PLUG_IN_ERROR, 0, "Auto Perspective: out of memory.");
      return gimp_procedure_new_return_values (procedure, GIMP_PDB_EXECUTION_ERROR, error);
    }
  g_clear_object (&st.path);

  gimp_progress_set_text ("Auto Perspective: fitting");
  status = compute (lines, count, w, h, mode, &st, &params, &fit, &apply_it);
  g_free (lines);

  if (apply_it)
    {
      gimp_progress_set_text ("Auto Perspective: transforming");
      if (up_homography (&params, &st.lens, w, h, m) != UP_OK ||
          ! apply (image, GIMP_LAYER (drawable), m, w, h, st.crop, st.apply_to, st.interp, &error))
        {
          gimp_progress_end ();
          if (! error)
            g_set_error (&error, GIMP_PLUG_IN_ERROR, 0, "Auto Perspective: the transform failed.");
          return gimp_procedure_new_return_values (procedure, GIMP_PDB_EXECUTION_ERROR, error);
        }
    }
  else if (status != UP_OK && run_mode == GIMP_RUN_INTERACTIVE)
    g_message ("Auto Perspective changed nothing: %s (%d vertical and %d horizontal lines "
               "found; at least %d of a kind are needed).",
               status_text (status), fit.vertical, fit.horizontal, UP_MIN_FIT_LINES);
  gimp_progress_end ();
  if (run_mode != GIMP_RUN_NONINTERACTIVE)
    gimp_displays_flush ();

  ret = gimp_procedure_new_return_values (procedure, GIMP_PDB_SUCCESS, NULL);
  GIMP_VALUES_SET_STRING (ret, 1, up_status_name (status));
  GIMP_VALUES_SET_DOUBLE (ret, 2, params.rotation);
  GIMP_VALUES_SET_DOUBLE (ret, 3, params.shift_v);
  GIMP_VALUES_SET_DOUBLE (ret, 4, params.shift_h);
  GIMP_VALUES_SET_DOUBLE (ret, 5, params.shear);
  GIMP_VALUES_SET_STRING (ret, 6, up_mode_name (fit.mode_used));
  return ret;
}
