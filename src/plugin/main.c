/* gimp-auto-perspective: automatic line based perspective correction for GIMP 3
 *
 *   Image > Transform > Auto Perspective...      plug-in-auto-perspective
 *
 * Finds the straight lines of buildings and the like in the selected
 * layer, fits rotation, lens shift and shear so that the nearly vertical
 * ones become vertical and the nearly horizontal ones horizontal, and
 * transforms the image with that. The work is done by the core in
 * src/core (see perspective.h).
 *
 * STATUS: skeleton. The procedure, its arguments and its dialog are
 * registered; it reads the layer (or the guide lines of a path) and calls
 * the core, whose fit returns the identity for now, so the image stays as
 * it is. See PLAN.md.
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
enum { CROP_NONE, CROP_LARGEST, CROP_ASPECT };
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
  gimp_procedure_set_image_types (procedure, "*");
  gimp_procedure_set_sensitivity_mask (procedure, GIMP_PROCEDURE_SENSITIVE_DRAWABLE);
  gimp_procedure_set_menu_label (procedure, "_Auto Perspective...");
  gimp_procedure_add_menu_path (procedure, MENU_PATH);
  gimp_procedure_set_attribution (procedure, "David", "David, GPL-3.0-or-later", "2026");
  gimp_procedure_set_documentation (procedure,
    "Straighten converging and tilted lines automatically (perspective correction)",
    "Finds the straight lines in the layer, keeps the nearly vertical and nearly "
    "horizontal ones, and fits a rotation, a vertical and a horizontal lens shift and a "
    "shear that make them truly vertical and horizontal, as darktable's rotate and "
    "perspective module (after ShiftN) does. The guided mode uses the strokes of a path "
    "as the lines instead. SKELETON: the fit is not implemented yet, so the image is "
    "left as it is.",
    name);

  gimp_procedure_add_choice_argument (procedure, "mode", "_Mode",
    "What to straighten",
    gimp_choice_new_with_values (
      "auto", UP_MODE_AUTO, "Automatic", "From the lines found: both directions if there are enough of each",
      "vertical", UP_MODE_VERTICAL, "Vertical", "Rotation and vertical lens shift: converging verticals",
      "horizontal", UP_MODE_HORIZONTAL, "Horizontal", "Rotation and horizontal lens shift",
      "full", UP_MODE_FULL, "Full", "Rotation, both lens shifts and shear",
      "rotation", UP_MODE_ROTATION, "Rotation only", "Level the image, no perspective change",
      "guided", MODE_GUIDED, "Guided by a path",
        "The strokes of the path are the lines (two or more, from their first to last anchor)",
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
  return procedure;
}

/* ------------------------------------------------------------ input */

/* the drawable as gray values, "Y float" (linear light) */
static float *
read_gray (GimpDrawable *drawable, int *w, int *h)
{
  GeglBuffer *buffer = gimp_drawable_get_buffer (drawable);
  float *gray;

  *w = gimp_drawable_get_width (drawable);
  *h = gimp_drawable_get_height (drawable);
  gray = g_try_malloc (sizeof (float) * (gsize) *w * *h);
  if (gray)
    gegl_buffer_get (buffer, GEGL_RECTANGLE (0, 0, *w, *h), 1.0, babl_format ("Y float"),
                     gray, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
  g_object_unref (buffer);
  return gray;
}

/* the strokes of a path as segments, in the drawable's coordinates: from
 * the first to the last anchor of each stroke (a Bezier stroke is a list
 * of control, anchor, control triples) */
static UpLine *
path_lines (GimpPath *path, GimpDrawable *drawable, int *count)
{
  gsize    n_strokes = 0;
  gint    *strokes = gimp_path_get_strokes (path, &n_strokes);
  UpLine  *lines = g_new0 (UpLine, MAX (n_strokes, 1));
  gint     ox, oy;

  gimp_drawable_get_offsets (drawable, &ox, &oy);
  *count = 0;
  for (gsize i = 0; i < n_strokes; i++)
    {
      gsize     n = 0;
      gdouble  *pts = NULL;
      gboolean  closed = FALSE;

      gimp_path_stroke_get_points (path, strokes[i], &n, &pts, &closed);
      if (n >= 6)
        {
          UpLine *l = &lines[*count];
          l->x1 = pts[2] - ox;
          l->y1 = pts[3] - oy;
          l->x2 = pts[n - 4] - ox;
          l->y2 = pts[n - 3] - oy;
          l->width = 1.0;
          l->weight = 1.0;
          l->type = up_classify_line (l);
          (*count)++;
        }
      g_free (pts);
    }
  g_free (strokes);
  return lines;
}

/* ------------------------------------------------------------ run */

static gboolean
run_dialog (GimpProcedure *procedure, GimpProcedureConfig *config)
{
  GtkWidget *dialog;
  gboolean   ok;

  gimp_ui_init (PLUG_IN_BINARY);
  dialog = gimp_procedure_dialog_new (procedure, config, "Auto Perspective");
  gimp_procedure_dialog_get_label (GIMP_PROCEDURE_DIALOG (dialog), "note",
    "Skeleton: the fit is not implemented yet, the image stays as it is.", FALSE, FALSE);
  /* SKELETON: the preview (PLAN.md) comes above the settings */
  gimp_procedure_dialog_fill (GIMP_PROCEDURE_DIALOG (dialog), "mode", "path", "crop",
                              "apply-to", "interpolation", "note", NULL);
  ok = gimp_procedure_dialog_run (GIMP_PROCEDURE_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  return ok;
}

static GimpValueArray *
auto_perspective_run (GimpProcedure *procedure, GimpRunMode run_mode, GimpImage *image,
             GimpDrawable **drawables, GimpProcedureConfig *config, gpointer run_data)
{
  GimpValueArray *ret;
  GimpDrawable   *drawable;
  GimpPath       *path = NULL;
  UpLine         *lines = NULL;
  UpParams        params = { 0 };
  UpFitResult     fit = { 0 };
  UpStatus        status;
  GError         *error = NULL;
  gint            mode, count = 0, w, h;
  gboolean        guided;

  if (gimp_core_object_array_get_length ((GObject **) drawables) != 1)
    {
      g_set_error (&error, GIMP_PLUG_IN_ERROR, 0, "Auto Perspective works on one layer: select one.");
      return gimp_procedure_new_return_values (procedure, GIMP_PDB_CALLING_ERROR, error);
    }
  drawable = drawables[0];

  if (run_mode == GIMP_RUN_INTERACTIVE && ! run_dialog (procedure, config))
    return gimp_procedure_new_return_values (procedure, GIMP_PDB_CANCEL, NULL);

  mode = gimp_procedure_config_get_choice_id (config, "mode");
  guided = mode == MODE_GUIDED;
  g_object_get (config, "path", &path, NULL);

  gimp_progress_init ("Auto Perspective: finding lines");
  if (guided)
    {
      if (! path)
        {
          g_set_error (&error, GIMP_PLUG_IN_ERROR, 0,
                       "The guided mode needs a path whose strokes lie along the lines.");
          return gimp_procedure_new_return_values (procedure, GIMP_PDB_CALLING_ERROR, error);
        }
      lines = path_lines (path, drawable, &count);
      w = gimp_drawable_get_width (drawable);
      h = gimp_drawable_get_height (drawable);
      /* SKELETON: which parameters the guided mode fits depends on the
       * lines drawn (PLAN.md); for now as the automatic mode */
      mode = UP_MODE_AUTO;
    }
  else
    {
      float *gray = read_gray (drawable, &w, &h);

      if (! gray || up_detect_lines (gray, w, h, &lines, &count) != UP_OK)
        {
          g_free (gray);
          g_clear_object (&path);
          g_set_error (&error, GIMP_PLUG_IN_ERROR, 0, "Auto Perspective: out of memory.");
          return gimp_procedure_new_return_values (procedure, GIMP_PDB_EXECUTION_ERROR, error);
        }
      g_free (gray);
    }
  g_clear_object (&path);

  gimp_progress_set_text ("Auto Perspective: fitting");
  status = up_fit (lines, count, w, h, (UpMode) mode, NULL, &params, &fit);
  if (guided)
    g_free (lines);
  else
    free (lines);

  /* SKELETON: with parameters that are not neutral, the transform comes
   * here (PLAN.md, "Applying the result"): gimp_context_push, interpolation
   * and transform-resize from "crop", gimp_item_transform_matrix with
   * up_homography's matrix on the layer or on every item of the image in
   * one undo group, then the canvas; gimp_context_pop. The skeleton's fit
   * is always neutral, so nothing is changed. */
  gimp_progress_end ();

  ret = gimp_procedure_new_return_values (procedure, GIMP_PDB_SUCCESS, NULL);
  GIMP_VALUES_SET_STRING (ret, 1, up_status_name (status));
  GIMP_VALUES_SET_DOUBLE (ret, 2, params.rotation);
  GIMP_VALUES_SET_DOUBLE (ret, 3, params.shift_v);
  GIMP_VALUES_SET_DOUBLE (ret, 4, params.shift_h);
  GIMP_VALUES_SET_DOUBLE (ret, 5, params.shear);
  return ret;
}
