# gimp-upright: the plan

Automatic line based perspective correction for GIMP 3: find the lines
that should be vertical and horizontal, fit a transform that makes them
so, and apply it. A port of the method of darktable's "rotate and
perspective" module (ashift, GPL-3.0-or-later, after ShiftN), as a GIMP
3 plug-in with a C core, in the shape of gimp-fusion and gimp-lqr-tng.

**Status: skeleton, for review.** Nothing is corrected yet. What exists
(README.md has the list) is the interface of the core, the plug-in's
registration and dialog, and tests that pass. The facts this plan rests
on, with sources: [docs/research.md](docs/research.md) and
[docs/patents.md](docs/patents.md). Questions for you are at the end.

## 1. What the user sees

**Image > Transform > Upright...** (`<Image>/Image/Transform`, below
Arbitrary Rotation; verified in GIMP 3.2.6, docs/transform-menu.png).
One dialog:

- **Preview** at the top: the image as it will be, with the lines that
  were used drawn over it (vertical ones in one colour, horizontal ones
  in another, rejected ones grey, as ashift does) and the crop
  rectangle. Updates when anything below changes.
- **Mode**
  | Mode | Fits | From | ashift equivalent |
  |---|---|---|---|
  | Automatic | chooses one of the rows below (section 3.4) | detected lines | none (ashift has no auto choice) |
  | Vertical | rotation, vertical lens shift | vertical lines | "fit vertical" |
  | Horizontal | rotation, horizontal lens shift | horizontal lines | "fit horizontal" |
  | Full | rotation, both lens shifts, shear | all lines | "fit both" (default, with shear) |
  | Rotation only | rotation | all lines | Ctrl + "fit both" |
  | Guided by a path | what the drawn lines allow (section 3.5) | the strokes of a path | "draw lines" / "draw rectangle" |
- **Guide path** (for the guided mode): a path of the image; each
  stroke is one line. The user draws them with the Paths tool before
  opening the dialog (a plug-in cannot draw on the canvas itself).
- **Fine tuning**, collapsed: the four fitted values as sliders
  (rotation in degrees, vertical and horizontal shift, shear) so the
  result can be adjusted by hand after the fit, and the lens model
  (generic 28 mm, or the focal length from EXIF; section 3.3).
- **Crop**: nothing (enlarge the canvas to show everything), the
  largest rectangle, or the largest rectangle of the original shape.
- **Apply to**: the whole image (all layers, channels, paths, the canvas)
  or only the layer.
- **Interpolation**: GIMP's own list (none, linear, cubic, NoHalo,
  LoHalo), cubic by default.
- A status line: how many lines were found and used, and the fitted
  values; "not enough lines" instead of a silent no-op.

The procedure also runs non-interactively with the same arguments and
returns the status and the four values, so it can be scripted and
batched (BIMP). It already does, in the skeleton.

## 2. How it is built

```
src/core/      the core: no GIMP, no GEGL; C11, malloc only
  upright.h      the interface (exists)
  upright.c      modes, classification, fit driver (exists as a stub)
  lsd.c          line segments (to write; section 3.1)
  ransac.c       outlier removal on vanishing points
  model.c        the homography of the four parameters
  simplex.c      Nelder-Mead (Hutt's MIT code, as darktable, or our own)
  crop.c         the largest rectangle inside the transformed outline
  image.c        downscaling and the gray conversion for detection
src/plugin/    the GIMP 3 plug-in (main.c exists)
  main.c         procedure, arguments, run, apply
  dialog.c       the dialog and its preview
tools/upright-cli.c   the core on the command line (exists)
tests/         check.sh, gimp-test.sh, gui/, synth.py, run.sh (exist)
```

The core runs the same way from `upright-cli` as from the plug-in, so
every numeric test runs without GIMP and under ASan. The CLI grows as
the core does:

```
upright-cli lines  image.pfm                      segments, with their class
upright-cli fit    [--mode=M] [--lines=F] [--focal=MM] image.pfm
upright-cli warp   [--mode=M] [--crop=C] image.pfm -o out.pfm
upright-cli cost   --params=r,sv,sh,sh image.pfm  the cost of given values
```

## 3. The method

### 3.1 Line detection

LSD (von Gioi et al., IPOL 2012) with ashift's settings (scale 0.99,
sigma scale 0.6, quantisation 2, angle tolerance 22.5 degrees, log
epsilon 0, density 0.7, 1024 bins) on a gray copy of the layer, scaled
down so its longer side is at most about 1500 pixels (ashift works on
darktable's preview for the same reason; the exact size is to be tuned
on the test photos), then scaled back. Segments along the border are
dropped. The detector sits behind `up_detect_lines`, so Hough (OpenCV
style) can be tried against it on the same images.

Which LSD code: see open question 1. Recommended: port OpenCV's
BSD-licensed `lsd.cpp` to C (or write it from the paper, with the MIT
NFA code by the LSD author), so the plug-in stays plain GPL-3.0; check
it against the IPOL reference output in the tests.

### 3.2 Classification and outliers

As ashift (research.md, section 1, steps 4 and 5): within 30 degrees of vertical or
horizontal and at least 5 pixels long (done: `up_classify_line`), weight
= length x width x precision, then RANSAC on vanishing points for each
direction. The lines RANSAC rejects stay visible (grey) in the preview.

### 3.3 The model

ashift's homography of rotation, vertical and horizontal lens shift and
shear, with its generic lens model (28 mm equivalent, lens dependence 0,
aspect 1) by default. `up_homography` returns it as the 3x3 matrix that
`gimp_item_transform_matrix` takes, in image pixel coordinates. The
port keeps ashift's matrices step by step (so its parameters and ours
mean the same), with the attribution in the file. An option: the focal
length from the image's EXIF (GIMP reads the metadata) instead of 28
mm; ashift has this as its "specific" lens model (open question 6).

### 3.4 The fit

ashift's cost (research.md, section 1, step 7) minimised by Nelder-Mead over the
parameters the mode frees, each through a logit onto ashift's ranges
(rotation +-10 degrees, shifts +-1, shear +-0.2); fewer than 2 lines of a
needed kind is "not enough lines", an area growth over 4 times is
"insane". Both are reported, never applied.

**Automatic** is ours; ashift leaves the choice to the user. Proposal:
fit Vertical; if there are enough horizontal lines, fit Full as well,
and take Full only if its cost is clearly lower and it keeps enough of
the image (crop area); otherwise Vertical; with too few vertical lines,
Horizontal; with too few of both, Rotation only if any line is left.
The thresholds come from the synthetic and real tests (open question
4). This is the part where Adobe's Auto "balances"; ours is a plain
rule on costs and areas.

### 3.5 The guided mode

Each stroke of the chosen path is a segment from its first to its last
anchor (done in the skeleton), classified like detected lines, weight 1.
What is fitted follows from what was drawn: 2 or more vertical lines
free rotation and vertical shift; 2 or more horizontal lines free
rotation and horizontal shift; both kinds give Full; a single line of
either kind gives Rotation only. A closed stroke of 4 anchors could
count as a rectangle (ashift's "draw rectangle": its 4 sides). No RANSAC
on drawn lines.

### 3.6 Crop

Two ways, to be compared in the tests: GIMP's own (the context's
transform resize "Crop to result" or "Crop with aspect", computed in
`app/core/gimp-transform-resize.c`), or ashift's Nelder-Mead crop in
the core (`crop.c`). The core's crop is needed anyway for the preview,
so the likely choice is: compute the crop in the core, transform with
"Adjust", then `gimp_image_crop` (or crop the layer) to that rectangle,
so preview and result agree exactly.

## 4. Applying the result in GIMP

Destructive, with undo. A non-destructive filter is not possible from a
plug-in today: GIMP 3.2.6 refuses `gegl:transform` as a drawable filter
("Filters from category "transform" are hidden", tried), and GEGL has
no perspective op.

1. `gimp_context_push`; interpolation from the argument; transform
   direction forward; transform resize "Adjust" (or GIMP's crop, 3.6).
2. One undo group.
3. **Whole image**: every layer (with its offsets; the matrix is in
   image coordinates), every channel, every path, with
   `gimp_item_transform_matrix`; then the canvas to the crop, or to all
   layers when not cropping. **Layer**: only that layer, then its own
   crop.
4. A selection makes `gimp_item_transform_matrix` cut a floating
   selection: the plug-in saves the selection to a channel, clears it,
   transforms, and gives the user back a transformed selection
   (open question 9).
5. `gimp_context_pop`.

## 5. Tests

As in gimp-fusion and gimp-lqr-tng: everything through
`tests/isolate.sh` (a throwaway HOME in tests/output, the user's GIMP,
Blender, Godot, Krita and Tiled folders listed before and after and
compared), builds in the Flatpak SDK via `gimp-devtools/gimp-build.sh`,
GUI GIMP with `--new-instance`, only processes the tests started are
stopped. `tests/run.sh` runs it all. What exists now is marked (done).

### 5.1 Unit tests of the core (`tests/check.sh`, also ASan and UBSan)

- (done) classification, mode names, fit status and the identity for
  neutral parameters, bad input gives UP_ERROR.
- The homography: identity for zero; each parameter alone against
  ashift's `_homography` compiled as a test oracle
  (`tests/ref/ashift-homography.c`, copied with attribution), to 1e-6
  relative; the inverse round trip.
- The cost: zero for perfect lines; known values for a line tilted by
  a known angle.
- Nelder-Mead: known minima of analytic functions (Rosenbrock), the
  logit mapping at its bounds.
- LSD: on synthetic edges, segments found at known positions (end
  points within 1 pixel, angle within 0.1 degrees); against the IPOL
  LSD 1.6 reference on the same images (the reference fetched and
  built in tests/output at test time, never shipped).
- Crop: a known quadrilateral with a known largest rectangle.

### 5.2 Recovery of known parameters (synthetic, numeric)

`tests/synth.py` (done) renders a facade through a pinhole camera with
known roll, pitch and yaw by casting rays, so the truth does not come
from our model. For a grid of cases (roll -5..5 degrees, pitch
-20..20, yaw -20..20, focal 18 to 50 mm, odd sizes, noise, some clutter
drawn in front):

- **Straightness** after the fitted correction, measured on the true
  facade edges (projected through the true camera, then through our
  homography): the largest deviation from vertical of any vertical
  edge, and from horizontal of any horizontal edge, in degrees. Target:
  below 0.1 degrees for Vertical and Full on clean images, below 0.3
  with noise and clutter.
- **Rotation**: with pitch and yaw zero, the fitted rotation equals the
  roll within 0.05 degrees.
- **Mode behaviour**: Vertical leaves horizontal convergence alone,
  Horizontal the vertical one, Rotation only changes no convergence;
  Automatic picks what the case needs.
- **Failure cases** report and do nothing: a blank image, a single
  line, noise only, a vanishing point inside the frame.

### 5.3 Real photos (freely licensed only)

Three Wikimedia Commons photos, licence read on each file page
(research.md, section 6): Kirkgate Market (CC0, converging verticals),
Stolbergs "Bankenmeile" (public domain, verticals and receding
horizontals), Iwase Bunko Library (CC0, horizontal perspective). Fetched
once into tests/output by a script with a generic User-Agent, checked
by SHA-256. Checks: the fit converges; after correction the strongest
detected vertical lines deviate less than 0.5 degrees; the crop keeps a
stated share; times and peak memory (VmHWM, as gimp-fusion) for 24
megapixel images. Screenshots for the README. Never the user's photos.

### 5.4 GIMP without a window (`tests/gimp-test.sh`, also `--asan`)

- (done) registration: the procedure, the menu path, the arguments,
  the return values, the six modes.
- (done, skeleton) every mode, crop and apply-to setting leaves the
  image as it was, on 8 and 16 bit and float, RGB and gray, alpha,
  layer offsets; guided without a path is a calling error, with a path
  of 3 strokes it succeeds.
- Once it works: the synthetic facade corrected in GIMP, then its lines
  measured again by `upright-cli lines` (straightness as in 5.2); the
  result against `upright-cli warp` pixel by pixel within the
  difference between GIMP's and our interpolation; the parameters
  returned equal those of `upright-cli fit` exactly; undo restores the
  image; channels, paths and the selection follow; images with several
  layers; indexed images refused or converted.

### 5.5 GUI on Broadway (`tests/gui/gui-test.sh`)

- (done) Image > Transform shows Upright...; the dialog opens; OK
  succeeds and (skeleton) leaves the image unchanged.
- Once it works: the preview draws (a screenshot compared with the
  result), switching modes changes it, the path chooser lists the
  image's paths, Cancel changes nothing, OK applies what the preview
  showed. Screenshots into docs/ for the README.

## 6. Steps

1. **LSD** in the core, with its tests (5.1). `upright-cli lines`.
2. **Model and fit**: homography with the ashift oracle, cost,
   Nelder-Mead, RANSAC; the recovery tests (5.2). `upright-cli fit`.
3. **Crop and warp** in the core: `upright-cli warp`; crop tests.
4. **Applying in GIMP** (section 4), non-interactive; GIMP tests (5.4).
5. **The dialog with the preview** and the fine tuning; GUI tests.
6. **Automatic mode** rule tuned on synthetic and real images (5.3).
7. README with screenshots, then offer upstream (GIMP issue #2026 is
   the natural place to mention it).

## 7. Open questions for you

1. **LSD licence route.** (a) take darktable's LSD 1.6 as it is
   (AGPL-3.0-or-later inside our GPL-3.0-or-later plug-in, allowed by
   GPL-3 section 13, the AGPL network clause then applies to the
   combination); (b) port OpenCV's BSD-licensed `lsd.cpp` to C; (c)
   write LSD from the IPOL paper with the author's MIT NFA code. I
   recommend (b) or (c) for a plain GPL plug-in. Which?
2. **Patents.** Adobe's US9008460 and US9582855 ("homography ...
   according to human perception-based criteria") are active until
   2033 (docs/patents.md); the ashift method is older (ShiftN 2006,
   Kodak 1999) and does not do the calibration steps of the other three.
   Do you want to go ahead with all modes, start with the guided and
   manual ones, or have it looked at first?
3. **The name.** "Upright" is Adobe's name for its feature. I have not
   checked whether it is a registered trademark. Keep "Upright...", or
   use something like "Straighten Perspective..." / "Auto Perspective..."?
4. **Automatic mode.** Is the rule in 3.4 (Vertical unless Full is
   clearly better and keeps enough of the image) what you want, or
   should Automatic simply be ashift's "fit both" (Full with shear)?
5. **Apply to.** The whole image by default (all layers, canvas cropped),
   or the selected layer?
6. **Lens model.** Generic 28 mm always (as ashift's default), or the
   focal length from EXIF when the image has it, with the "lens
   dependence" and aspect controls of ashift's specific model?
7. **Fine tuning sliders** in the dialog after the fit (like ashift):
   yes, or fit only?
8. **Crop default.** Largest rectangle (ashift's default), same shape,
   or none?
9. **Selection.** Ignore it (transform everything, and the selection
   with it), or refuse to run with a selection, or correct only the
   selected part (GIMP's floating selection behaviour)?
10. **GEGL op later.** A `gegl:` perspective op for upstream GEGL would
    only become non-destructive in GIMP once transform filters are
    allowed as layer filters; worth doing now or not at all?
