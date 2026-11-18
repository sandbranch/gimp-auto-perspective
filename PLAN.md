# gimp-auto-perspective: the plan

Automatic line based perspective correction for GIMP 3: find the lines
that should be vertical and horizontal, fit a transform that makes them
so, and apply it. A port of the method of darktable's "rotate and
perspective" module (ashift, GPL-3.0-or-later, after ShiftN), as a GIMP
3 plug-in with a C core, in the shape of gimp-fusion and gimp-lqr-tng.

**Status (2026-11-18): the core works and the plug-in applies it**
(steps 1 to 4 of section 6: LSD, model, fit, RANSAC, crop, warp, with
their tests and the measured recovery numbers in 5.2; the transform in
GIMP, non-interactive, with the tests of 5.4). Step 5, the dialog with
its preview and the fine tuning, is built but not yet tried in GIMP's
window: the GUI tests are for David to run (5.5). The facts this plan rests on, with sources:
[docs/research.md](docs/research.md) and
[docs/patents.md](docs/patents.md). The open questions of section 7 got
defaults while David was away; each is marked there and easy to change.

## 1. What the user sees

**Image > Transform > Auto Perspective...** (`<Image>/Image/Transform`, below
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
batched (BIMP). It does: the arguments as above, and it returns the
status, the four values and the mode used.

## 2. How it is built

```
src/core/      the core: no GIMP, no GEGL; C11, malloc only
  perspective.h      the interface (exists)
  perspective.c      modes, classification, fit driver (exists as a stub)
  lsd.c          line segments (to write; section 3.1)
  ransac.c       outlier removal on vanishing points
  model.c        the homography of the four parameters
  simplex.c      Nelder-Mead (Hutt's MIT code, as darktable, or our own)
  crop.c         the largest rectangle inside the transformed outline
  image.c        downscaling and the gray conversion for detection
src/plugin/    the GIMP 3 plug-in (main.c exists)
  main.c         procedure, arguments, run, apply
  dialog.c       the dialog and its preview
tools/auto-perspective-cli.c   the core on the command line (exists)
tests/         check.sh, gimp-test.sh, gui/, synth.py, run.sh (exist)
```

The core runs the same way from `auto-perspective-cli` as from the plug-in, so
every numeric test runs without GIMP and under ASan. The CLI grows as
the core does:

```
auto-perspective-cli lines  image.pfm                      segments, with their class
auto-perspective-cli fit    [--mode=M] [--lines=F] [--focal=MM] image.pfm
auto-perspective-cli warp   [--mode=M] [--crop=C] image.pfm -o out.pfm
auto-perspective-cli cost   --params=r,sv,sh,sh image.pfm  the cost of given values
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

Which LSD code (question 1, default chosen): a C port of OpenCV's
`lsd.cpp`, 3-clause BSD, in `src/core/lsd.c` with its licence and
history (research.md, section 2, has what was checked). Two details
follow the LSD paper and LSD 1.6 rather than OpenCV, because they were
measured to matter against LSD 1.6's own output on its example image
(tests/lsd-ref.py): the Gaussian subsampling places output sample x at
input x / scale (agreement 44 % before, 89.7 % after), and the pseudo
ordering scans column after column (89.9 %); segments of 40 pixels or
more agree 95.6 %, the rest are short ones of low significance where
OpenCV's code differs from LSD 1.6.

**Merging collinear segments** (ours, not in ashift): after LSD, the
segments of one kind that lie on one line (within 2 degrees, both ends
within 1.5 pixels of the line so far) become one line, the weighted
total least squares line through their ends, with the summed weight.
Measured on the synthetic facades: short segments (window heads of
about 20 pixels) had their angle biased by 0.13 degrees on average, all
the same way, by the corners at their ends, while the long roof line
was within 0.003; with merging the largest deviation after a Full fit
went from 0.24 to 0.012 degrees (section 5.2). Weighting by length
squared instead was tried and is worse: long clutter lines take over.

### 3.2 Classification and outliers

As ashift (research.md, section 1, steps 4 and 5): within 30 degrees of vertical or
horizontal and at least 5 pixels long (done: `up_classify_line`), weight
= length x width x precision, then RANSAC on vanishing points for each
direction. The lines RANSAC rejects stay visible (grey) in the preview.

### 3.3 The model

ashift's homography of rotation, vertical and horizontal lens shift and
shear, with its generic lens model (28 mm equivalent, lens dependence 0,
aspect 1) by default. A fact found while porting: with lens dependence
0 the focal length has no effect at all on ashift's matrix (it only
enters the compression factors r_v and r_h, which are then 1), so an
EXIF focal length (question 6) matters only together with a lens
dependence above 0. `up_homography` returns it as the 3x3 matrix that
`gimp_item_transform_matrix` takes, in image pixel coordinates. The
port keeps ashift's matrices step by step (so its parameters and ours
mean the same), with the attribution in the file. An option: the focal
length from the image's EXIF (GIMP reads the metadata) instead of 28
mm; ashift has this as its "specific" lens model (open question 6).

### 3.4 The fit

ashift's cost (research.md, section 1, step 7) minimised by Nelder-Mead over the
parameters the mode frees, each through a logit onto its range: those
darktable's ashift fits with today, rotation +-180 degrees, shifts +-2,
shear +-0.5 (its "soft" ranges; research.md said +-10, +-1, +-0.2,
which are the ranges of its sliders' defaults; both gave the same
results in 5.2, and one Full case there needs 9.3 degrees). The
Nelder-Mead is our own (src/core/simplex.c), with ashift's settings.
Fewer than 2 lines of a needed kind is "not enough lines", an area
growth over 4 times is "insane". Both are reported, never applied.

**Automatic** is ours; ashift leaves the choice to the user. Proposal:
fit Vertical; if there are enough horizontal lines, fit Full as well,
and take Full only if its cost is clearly lower and it keeps enough of
the image (crop area); otherwise Vertical; with too few vertical lines,
Horizontal; with too few of both, Rotation only if any line is left.
The thresholds come from the synthetic and real tests (open question
4). This is the part where Adobe's Auto "balances"; ours is a plain
rule on costs and areas. Implemented (default chosen, question 4):
Full wins if its cost over all lines is below half of Vertical's and
its largest crop keeps at least half of the image (`AUTO_COST_RATIO`,
`AUTO_MIN_AREA` in perspective.c); on the synthetic facades it picks
Vertical for pitch, Full for yaw and for pitch with yaw (5.2). Not yet
tuned on real photos (step 6).

### 3.5 The guided mode

Each stroke of the chosen path is a segment from its first to its last
anchor, classified like detected lines, weight 1. What is fitted
follows from what was drawn (`up_mode_for_lines`): 2 or more vertical
lines free rotation and vertical shift; 2 or more horizontal lines free
rotation and horizontal shift; both kinds give Full; fewer give
Rotation only. A closed stroke of 4 anchors counts as a rectangle
(ashift's "draw rectangle": its 4 sides). No RANSAC on drawn lines.
All done and tested in GIMP (5.4).

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

Done (step 4), as described below; the crop is the core's (3.6), so a
preview can show exactly what is applied.

1. `gimp_context_push`; interpolation from the argument; transform
   direction forward; transform resize "Adjust" (or GIMP's crop, 3.6).
2. One undo group.
3. **Whole image**: every layer (with its offsets; the matrix is in
   image coordinates), every channel, every path, with
   `gimp_item_transform_matrix`; then the canvas to the crop, or to all
   layers when not cropping. **Layer**: only that layer, then its own
   crop. As built: the matrix is the layer's (lines found in the layer)
   moved into image coordinates by the layer's offsets; with a crop,
   `gimp_image_resize_to_layers` first (so the rectangle lies inside the
   canvas), then `gimp_image_crop`, which crops the layers too; without
   one, `gimp_image_resize` to the transformed layer's bounding box. For
   the layer alone, `gimp_layer_resize` to the rectangle. Channels are
   clipped to the canvas by GIMP when transformed, so with "crop:
   nothing" a channel loses what moves outside the old canvas.
4. A selection makes `gimp_item_transform_matrix` cut a floating
   selection: the plug-in saves the selection to a channel, clears it,
   transforms, and gives the user back a transformed selection
   (open question 9).
5. `gimp_context_pop`.

## 5. Tests

As in gimp-fusion and gimp-lqr-tng: everything through
`tests/isolate.sh` (a throwaway HOME in tests/output, the user's GIMP,
Blender, Godot, Krita and Tiled folders listed before and after and
compared), builds against the native GIMP via `gimp-devtools/gimp-build.sh`,
GUI GIMP with `--new-instance`, only processes the tests started are
stopped. `tests/run.sh` runs it all. What exists now is marked (done).

### 5.1 Unit tests of the core (`tests/check.sh`, also ASan and UBSan)

- (done) classification, mode names, fit status and the identity for
  neutral parameters, bad input gives UP_ERROR.
- (done) The homography: identity for zero; each parameter alone and
  together against ashift's `_homography` compiled as a test oracle
  (`tests/ref/ashift-homography.c`, copied unchanged with attribution):
  mapped corners agree within 1.6e-7 of the image size (ashift computes
  in float, we in double); the inverse round trip within 7e-15; the
  sign of the rotation.
- (done) The cost: zero for perfect lines; 1e6 sin (1 degree) for one
  line 1 degree off, and the combination of both kinds.
- (done) Nelder-Mead: Rosenbrock, a 4 dimensional bowl, non convergence
  reported; the logit mapping at and beyond its bounds.
- (done) The fit recovers the parameters from exact lines in every mode
  (rotation within 0.01 degrees, the rest within 1e-3), and gives the
  same bits twice; RANSAC drops the one line off the vanishing point.
- (done) LSD: on anti-aliased edges at known positions and angles: x
  within 0.03 pixels, angle within 0.004 degrees; nothing on flat or
  noise images. Against LSD 1.6: `tests/lsd-ref.sh` (not part of
  `run.sh`: it fetches the AGPL reference at test time) compares with
  LSD 1.6's own output on its example image (numbers in 3.1) and,
  built in tests/output, on the facades. The image comparison was run;
  building and running LSD 1.6 itself was blocked by the session's
  permissions on 2026-11-18 and has not been run yet.
- (done) Crop: rotated rectangles against the closed form of the
  largest axis aligned rectangle and of the largest one of the same
  shape: 98.6 to 99.7 % of the area (rounding inwards to whole pixels),
  always inside; the bounding box for "none".
- (done) Warp: the identity and whole pixel shifts give the input back
  for nearest, linear and cubic.

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

Implemented as `tests/recovery.py` (run by check.sh). Measured
2026-11-18, 480x360 and odd sizes, focal 18, 28 and 50 mm:

| Check | Target | Measured |
|---|---|---|
| Vertical, clean (45 cases: pitch -20..20, roll -5..3) | < 0.1 | 0.016 degrees |
| Full, clean (6 cases, pitch and yaw up to 20, roll up to 5, and 2 odd sizes) | < 0.1 | 0.012 degrees |
| Vertical, noise 0.03 and 6 clutter bars | < 0.3 | 0.024 degrees |
| Full, noise and clutter | < 0.3 | 0.020 degrees |
| Rotation = roll (5 rolls, Rotation and Vertical) | < 0.05 | 0.0011 degrees |
| Auto: pitch, yaw, both, roll, noisy pitch | the right mode | vertical, full, full, vertical, vertical |
| Blank, noise only, one edge, vanishing point in the frame | not-enough-lines, identity | all four |
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

Implemented as `tests/real.sh` and `tests/real.py` (not in `run.sh`:
the first run needs the network). Licences read again on each file page
on 2026-11-18 (wikitext template and the API's LicenseShortName): CC0,
public domain, CC0. Measured 2026-11-18 with auto-perspective-cli:

| Photo | Mode | Result | Crop keeps | Strongest 20 verticals afterwards |
|---|---|---|---|---|
| Kirkgate Market, 3072x2304 | vertical, auto (picks vertical) | rotation -4.09, shift-v 0.341 | 78 % | weighted median 0.257, largest 1.21 degrees |
| Stolbergs, 1536x1024 | vertical, auto (picks vertical) | rotation -3.74, shift-v 0.190 | 83 % | median 0.079, largest 0.75 |
| Stolbergs | full | "insane": the receding street would need more than 4 times the area; nothing applied | | |

The largest values are street furniture: on Kirkgate the worst line is
a pole on the right, and the two edges of the tapering lamp post lean
by design; the facade's long edges are within 0.02 to 0.17 degrees.
So the check is the weighted median below 0.5 degrees (it was first
the largest of the 10 strongest; changed after seeing these data, and
said so here). Time and memory (VmHWM measured by the CLI itself): 0.34
s and 164 MiB for the 7 megapixel photo; Kirkgate scaled to 6000x4000,
24 megapixels: 0.86 s, 551 MiB (the CLI holds the RGB image as floats,
275 MiB; the plug-in reads only the gray). In GIMP (tests/gimp-test.sh,
when the photo is there) the whole run on Kirkgate, fit, transform and
crop, takes 0.98 s. Before and after previews are written into
tests/output/real (not committed: README screenshots still to do).
The third photo, Iwase Bunko Library (the horizontal case), could not
be fetched on 2026-11-18: upload.wikimedia.org answered 429 (too many
requests) to four tries over several minutes; `tests/real.sh` checks it
by the SHA-1 Commons publishes and skips it while it is missing. So the
Horizontal and Full modes have not yet been measured on a real photo.

### 5.4 GIMP without a window (`tests/gimp-test.sh`, also `--asan`)

- (done) registration: the procedure, the menu path, the arguments,
  the return values, the six modes.
- (done) guided without a path is a calling error.
- (done, 127 checks, also with ASan) The synthetic facade corrected in
  GIMP: the parameters returned equal those of `auto-perspective-cli
  fit` on the same pixels to all 9 printed digits (auto, vertical,
  full); the canvas is the CLI's crop rectangle; the pixels against
  `auto-perspective-cli warp` (both linear): mean difference 0.0019,
  99th percentile 0.048 (gray 0..1); the corrected facade fitted again
  by the CLI: rotation 0.0011 degrees, shift 0.00002, nothing left. The
  facade's true vertical edges drawn as a path are vertical afterwards
  within 0.0042 degrees (paths follow); the selection follows (its
  bounds move), no floating selection, the saved channel removed; three
  layers all transformed and cropped; apply-to layer leaves the canvas
  and the other layers alone; every mode, crop and apply-to setting on
  8 and 16 bit and float, RGB and gray, alpha, offsets; guided with the
  true edges (vertical, the strokes exactly vertical afterwards) and
  with a closed 4 anchor stroke (full); a blank image unchanged and not
  dirty; indexed images refused (the procedure takes RGB* and GRAY*).
- Undo cannot be driven from a script (GIMP has no undo procedure): it
  is for the GUI test (one undo group is used).

### 5.5 GUI on Broadway (`tests/gui/gui-test.sh`)

- (done) Image > Transform shows Auto Perspective...; the dialog opens.
- (updated 2026-11-18, not run yet: David runs the GUI tests) OK
  succeeds, the fit is ok with mode vertical, the canvas is cropped and
  the image changed; a screenshot of the result (03-result.png).
- Once it works: the preview draws (a screenshot compared with the
  result), switching modes changes it, the path chooser lists the
  image's paths, Cancel changes nothing, OK applies what the preview
  showed. Screenshots into docs/ for the README.

## 6. Steps

1. (done) **LSD** in the core, with its tests (5.1). `auto-perspective-cli lines`.
2. (done) **Model and fit**: homography with the ashift oracle, cost,
   Nelder-Mead, RANSAC; the recovery tests (5.2). `auto-perspective-cli fit`.
3. (done) **Crop and warp** in the core: `auto-perspective-cli warp`; crop tests.
4. (done) **Applying in GIMP** (section 4), non-interactive; GIMP tests (5.4).
5. (built, GUI test not run yet) **The dialog with the preview** and the
   fine tuning; GUI tests. The preview is a 400 pixel copy of the
   layer, transformed by the core's warp with the same matrix (scaled),
   the lines used drawn over it (vertical green, horizontal blue,
   outliers grey) and the crop with the outside darkened; a status line
   with the mode, the line counts and the values. "Fine tuning" holds
   four offsets added to the fit (arguments `adjust-rotation`,
   `adjust-shift-v`, `adjust-shift-h`, `adjust-shear`, so scripts can
   use them too; reset to 0 each time the dialog opens), the focal
   length and the lens dependence. With fine tuning but a failed fit,
   the offsets alone are applied (tested in 5.4). Detection runs in the
   dialog once, when it opens; the fits run again on every change.
6. **Automatic mode** rule tuned on synthetic and real images (5.3).
   Started: on the two real photos measured it picks Vertical, which
   is right for both; still to do with the third photo and more.
7. README with screenshots, then offer upstream (GIMP issue #2026 is
   the natural place to mention it).

## 7. Open questions for you

1. **LSD licence route.** (a) take darktable's LSD 1.6 as it is
   (AGPL-3.0-or-later inside our GPL-3.0-or-later plug-in, allowed by
   GPL-3 section 13, the AGPL network clause then applies to the
   combination); (b) port OpenCV's BSD-licensed `lsd.cpp` to C; (c)
   write LSD from the IPOL paper with the author's MIT NFA code. I
   recommend (b) or (c) for a plain GPL plug-in. Which?
   **Decided 2026-11-18 (David): keep (b).** The plug-in stays plain GPL-3.0-or-later, darktable's
   AGPL LSD is not used. Checked first (research.md, section 2): the
   file header of OpenCV's current lsd.cpp is the 3-clause BSD licence;
   OpenCV removed the file on 2019-03-01 (commit 3ba49ccecc) because
   parts came from the AGPL LSD 1.6, and restored it unchanged on
   2021-10-01 (commit 9b76872708, pull request 20797, release 4.5.4)
   after the LSD author published the NFA code under MIT and wrote in
   opencv_contrib issue 2524 that OpenCV should keep its implementation
   and only update the doubtful parts. So the BSD licence of the file
   rests on the author's public consent in that issue, not on a formal
   relicensing of the rest of LSD 1.6; OpenCV accepted that. If you want
   to avoid even that, route (c) (writing LSD from the paper) remains;
   `src/core/lsd.c` is one file behind one function.
2. **Patents.** Adobe's US9008460 and US9582855 ("homography ...
   according to human perception-based criteria") are active until
   2033 (docs/patents.md); the ashift method is older (ShiftN 2006,
   Kodak 1999) and does not do the calibration steps of the other three.
   Do you want to go ahead with all modes, start with the guided and
   manual ones, or have it looked at first?
   **Decided 2026-11-18 (David, after reading docs/patents.md): the
   patent side is fine.** All modes, with the ashift/ShiftN method only
   (published, older, in darktable since 2016); nothing of Adobe's
   "human perception-based criteria" (no eye level, no camera
   calibration, no perception energy).
3. **The name.** Decided 2026-11-18: "Auto Perspective..." and the
   repository gimp-auto-perspective (it was gimp-upright; "Upright" is
   Adobe's name for its feature).
4. **Automatic mode.** Is the rule in 3.4 (Vertical unless Full is
   clearly better and keeps enough of the image) what you want, or
   should Automatic simply be ashift's "fit both" (Full with shear)?
   *Default chosen 2026-11-18 while David was away; easy to change:* the rule of 3.4, implemented with the thresholds stated
   there.
5. **Apply to.** The whole image by default (all layers, canvas cropped),
   or the selected layer?
   *Default chosen 2026-11-18 while David was away; easy to change:* the whole image.
6. **Lens model.** Generic 28 mm always (as ashift's default), or the
   focal length from EXIF when the image has it, with the "lens
   dependence" and aspect controls of ashift's specific model?
   *Default chosen 2026-11-18 while David was away; easy to change:* the EXIF focal length when the image has one, otherwise
   the generic 28 mm. (As found in 3.3, it changes the result only with
   a lens dependence above 0.)
7. **Fine tuning sliders** in the dialog after the fit (like ashift):
   yes, or fit only?
   *Default chosen 2026-11-18 while David was away; easy to change:* yes.
8. **Crop default.** Largest rectangle (ashift's default), same shape,
   or none?
   *Default chosen 2026-11-18 while David was away; easy to change:* the largest rectangle.
9. **Selection.** Ignore it (transform everything, and the selection
   with it), or refuse to run with a selection, or correct only the
   selected part (GIMP's floating selection behaviour)?
   *Default chosen 2026-11-18 while David was away; easy to change:* transform the selection along with the image.
10. **GEGL op later.** A `gegl:` perspective op for upstream GEGL would
    only become non-destructive in GIMP once transform filters are
    allowed as layer filters; worth doing now or not at all?
    *Default chosen 2026-11-18 while David was away; easy to change:* not now.
