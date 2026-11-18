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
 * STATUS: the fit and the transform work; the dialog has no preview yet.
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

  gimp_procedure_add_string_return_value (procedure, "status", "Status",
    "What the fit did: ok, not-enough-lines, did-not-converge, insane",
    NULL, G_PARAM_READWRITE);
  gimp_procedure_add_double_return_value (procedure, "rotation", "Rotation",
    "The rotation found, in degrees", -180.0, 180.0, 0.0, G_PARAM_READWRITE);
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

/* ------------------------------------------------------------ run */

static gboolean
run_dialog (GimpProcedure *procedure, GimpProcedureConfig *config)
{
  GtkWidget *dialog;
  gboolean   ok;

  gimp_ui_init (PLUG_IN_BINARY);
  dialog = gimp_procedure_dialog_new (procedure, config, "Auto Perspective");
  /* PLAN.md step 5: the preview and the fine tuning come here */
  gimp_procedure_dialog_fill (GIMP_PROCEDURE_DIALOG (dialog), "mode", "path", "crop",
                              "apply-to", "interpolation", NULL);
  ok = gimp_procedure_dialog_run (GIMP_PROCEDURE_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  return ok;
}

static GimpValueArray *
auto_perspective_run (GimpProcedure *procedure, GimpRunMode run_mode, GimpImage *image,
                      GimpDrawable **drawables, GimpProcedureConfig *config, gpointer run_data)
{
  GimpValueArray       *ret;
  GimpDrawable         *drawable;
  GimpPath             *path = NULL;
  UpLine               *lines = NULL;
  UpParams              params = { 0 };
  UpFitResult           fit = { 0 };
  UpLens                lens;
  UpStatus              status;
  GError               *error = NULL;
  GimpInterpolationType interp;
  gint                  mode, crop, apply_to, count = 0, w, h;
  gdouble               focal, orthocorr;
  gboolean              guided;
  double                m[9];

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

  if (run_mode == GIMP_RUN_INTERACTIVE && ! run_dialog (procedure, config))
    return gimp_procedure_new_return_values (procedure, GIMP_PDB_CANCEL, NULL);

  mode = gimp_procedure_config_get_choice_id (config, "mode");
  crop = gimp_procedure_config_get_choice_id (config, "crop");
  apply_to = gimp_procedure_config_get_choice_id (config, "apply-to");
  g_object_get (config, "path", &path, "interpolation", &interp, "focal-length", &focal,
                "lens-dependence", &orthocorr, NULL);
  guided = mode == MODE_GUIDED;

  up_lens_generic (&lens);
  if (focal <= 0.0)
    focal = exif_focal (image);
  if (focal > 0.0)
    lens.focal = focal;
  lens.orthocorr = orthocorr;

  gimp_progress_init ("Auto Perspective: finding lines");
  w = gimp_drawable_get_width (drawable);
  h = gimp_drawable_get_height (drawable);
  if (guided)
    {
      if (! path)
        {
          g_set_error (&error, GIMP_PLUG_IN_ERROR, 0,
                       "The guided mode needs a path whose strokes lie along the lines.");
          return gimp_procedure_new_return_values (procedure, GIMP_PDB_CALLING_ERROR, error);
        }
      lines = path_lines (path, drawable, &count);
      mode = up_mode_for_lines (lines, count);
    }
  else
    {
      float  *gray = read_gray (drawable, &w, &h);
      UpLine *found = NULL;

      if (! gray || up_detect_lines (gray, w, h, &found, &count) != UP_OK)
        {
          g_free (gray);
          g_clear_object (&path);
          g_set_error (&error, GIMP_PLUG_IN_ERROR, 0, "Auto Perspective: out of memory.");
          return gimp_procedure_new_return_values (procedure, GIMP_PDB_EXECUTION_ERROR, error);
        }
      g_free (gray);
      up_remove_outliers (found, count, w, h);
      /* (the core's lines are malloc'ed, the path's g_new'ed: one kind
       * to free below) */
      lines = g_memdup2 (found, sizeof (UpLine) * (gsize) count);
      free (found);
    }
  g_clear_object (&path);

  gimp_progress_set_text ("Auto Perspective: fitting");
  status = up_fit (lines, count, w, h, (UpMode) mode, &lens, &params, &fit);
  g_free (lines);

  if (status == UP_OK && ! up_params_neutral (&params))
    {
      gimp_progress_set_text ("Auto Perspective: transforming");
      if (up_homography (&params, &lens, w, h, m) != UP_OK ||
          ! apply (image, GIMP_LAYER (drawable), m, w, h, crop, apply_to, interp, &error))
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
               status == UP_NOT_ENOUGH_LINES ? "not enough lines"
               : status == UP_INSANE ? "the result would distort the image too much"
               : status == UP_DID_NOT_CONVERGE ? "the fit did not converge" : "an error",
               fit.vertical, fit.horizontal, UP_MIN_FIT_LINES);
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
