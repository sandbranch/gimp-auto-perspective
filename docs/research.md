# Research: automatic upright correction for GIMP 3

What exists, how it works, under which licence, and what GIMP 3.2 offers
to apply the result. Everything here was checked on 2026-09-28 in the
source named next to it; where something could not be checked, it says
so. Patents are in [patents.md](patents.md).

## 1. darktable's "rotate and perspective" module (ashift)

The reference implementation. Read at darktable master, commit
`94ccc7504aa8` (2026-09-28), github.com/darktable-org/darktable.

### Files and licences

| File | Lines | What | Licence (header of the file) |
|---|---|---|---|
| `src/iop/ashift.c` | 6223 | the module: detection driver, RANSAC, model, fit, crop, GUI | GPL-3.0-or-later, "Copyright (C) 2016-2026 darktable developers" |
| `src/iop/ashift_lsd.c` | 2283 | LSD 1.6 (downloaded 2016-01-30), lightly changed (static functions, a division by zero caught, two renames) | the LSD part: **AGPL-3.0-or-later**, "Copyright (c) 2007-2011 rafael grompone von gioi"; the file also carries darktable's GPL-3.0-or-later header |
| `src/iop/ashift_nmsimplex.c` | 420 | Nelder-Mead simplex by Michael F. Hutt (1997, downloaded 2016-02-06), changed for darktable | MIT style permission notice ("Permission is hereby granted, free of charge ..."), plus darktable's GPL-3.0-or-later header |

`ashift.c` includes both of the others with `#include`. It says its
inspiration is ShiftN by Marcus Hebel, and its homography says "most of
this comes from ShiftN".

GPL-3.0 section 13 (in our COPYING) allows combining a GPL-3 work with
an AGPL-3 work and conveying the result; the AGPL part keeps its terms
and the AGPL's network interaction clause applies to the combination.
That is how darktable ships LSD. For a desktop plug-in the network
clause has no practical effect, but it is a real condition on the
combined work, and it is a choice for the user (PLAN.md, open
questions). Our COPYING is GPL-3.0-or-later, so porting `ashift.c`
with attribution is compatible.

### The method, step by step (as the code does it)

1. **Input for detection.** The image is turned into gray values
   (`rgb2grey256`, 0..256); raw images get a gamma of 0.45 first. Two
   optional enhancements: "edges" (a 1D edge enhancement,
   `edge_enhance`) and "detail" (`detail_enhance`, uses a bilateral
   filter). The detection runs on darktable's preview buffer, so on a
   reduced image.
2. **Line segments: LSD.** `LineSegmentDetection` with scale 0.99,
   sigma scale 0.6, quantization bound 2.0, angle tolerance 22.5
   degrees, log epsilon 0.0 (NFA threshold), density 0.7 and 1024 bins.
   Segments lying along the image border (within 2 pixels and parallel
   to it) are dropped as artefacts.
3. **Per segment:** end points in homogeneous coordinates, the line
   through them normalised so that x^2 + y^2 = 1, length, the width of
   the LSD rectangle, and a **weight = length x width x angle
   precision** (LSD's output p).
4. **Classification.** Vertical if within 30 degrees of vertical
   (`MAX_TANGENTIAL_DEVIATION`), horizontal if within 30 degrees of
   horizontal, and at least 5 pixels long (`MIN_LINE_LENGTH`); anything
   else is ignored. (Our `up_classify_line` implements exactly this.)
5. **Outliers: RANSAC on vanishing points** (`ransac`,
   `_remove_outliers`), separately for the vertical and the horizontal
   set. Two random lines give a candidate vanishing point (their cross
   product); a vanishing point inside the image frame is no valid model.
   Every other line counts as inlier if |V . L| (both normalised) is
   below epsilon. Quality is one third number of inliers, one third
   their weight, one third their closeness. 400 runs; epsilon tunes
   itself (start 10^-2, steps of one decade, 5 steps of 50 dry runs);
   below 5 lines all permutations are tried instead of random ones. The
   outliers are deselected, not deleted (the user can toggle lines).
6. **The model: a homography of four parameters** (`_homography`):
   rotation (degrees), vertical lens shift, horizontal lens shift, and
   shear. Built as a product of matrices: swap x and y (ShiftN's
   convention), rotate about the image center, shear, vertical lens
   shift (a projective matrix in exp(shift)), a horizontal compression
   that depends on the focal length, swap back, horizontal lens shift,
   vertical compression, an aspect ratio scaling, and finally a
   translation so that no output coordinate is negative. In the
   "generic" lens model the focal length is 28 mm (35 mm equivalent),
   "lens dependence" (orthocorr) is 0 and aspect is 1; the "specific"
   model takes focal length, crop factor, lens dependence and aspect
   from the user.
7. **The cost** (`model_fitness`): transform both end points of every
   selected line, take the line through them (normalised), and its dot
   product s with the axis it should be perpendicular to (s = 0 when a
   vertical line is vertical). Per direction the weighted mean of s^2,
   times the share of lines in that direction: v and h. Cost =
   sqrt(1 - (1 - v)(1 - h)) x 10^6.
8. **The fit** (`nmsfit`): Nelder-Mead simplex over the chosen subset
   of parameters, each mapped through a logit onto its range (so the
   simplex is unbounded but the parameters are not). Corrected
   2026-11-18: the ranges the fit uses are the "soft" ones, rotation
   +-180 degrees, lens shifts +-2, shear +-0.5 (`g->rotation_range =
   ROTATION_RANGE_SOFT` and so on in `gui_init` and `reload_defaults`,
   at commit 131eca7025 of 2026-11-15); +-10, +-1, +-0.2 (the defines
   without _SOFT) are not what `nmsfit` gets. Epsilon 10^-3, scale 1, at
   most 400 iterations.
   Results: success, not enough lines (fewer than 2 of a needed kind),
   did not converge, or "insane": the transformed image would cover
   more than 4 times the original area.
9. **Which parameters** (the three "fit" buttons, `_event_fit_*`):
   vertical fits rotation and vertical shift from vertical lines
   (Ctrl: rotation only; Shift: shift only); horizontal the same with
   horizontal lines; "both" by default fits rotation, both shifts and
   **shear** from all lines (Ctrl+Shift: without shear; Ctrl: rotation
   only; Shift: both shifts without rotation).
10. **Manual structure.** Besides automatic detection
    (`ASHIFT_METHOD_AUTO`) the user can draw a rectangle
    (`ASHIFT_METHOD_QUAD`: four lines) or draw lines
    (`ASHIFT_METHOD_LINES`, up to 50 saved in the parameters); the same
    fit then runs on those.
11. **Crop** (`do_crop`, `crop_fitness`): off, largest area, or original
    format. A second Nelder-Mead searches the center of the crop
    rectangle (in input coordinates, so it is surely inside) and, for
    "largest area", its aspect angle; the area of the largest
    rectangle with that center and angle inside the transformed outline
    is maximised.

### ShiftN

Marcus Hebel's ShiftN (www.shiftn.de): "automatic correction of
converging lines", Windows only, version 4.0. The page offers the source
as "Visual C++, Visual Studio 2008, License LGPL: Source.zip" (checked on
the page). darktable's ashift (since 2.2.0, as the page also says) is
the maintained descendant; we port from darktable, not from ShiftN.

## 2. LSD, the line segment detector

R. Grompone von Gioi, J. Jakubowicz, J.-M. Morel, G. Randall, "LSD: a
Line Segment Detector", Image Processing On Line 2 (2012),
doi:10.5201/ipol.2012.gjmr-lsd. Linear time, no parameters to tune in
practice: a segment is kept only if its number of false alarms is below
1 (log epsilon 0).

Licences of the implementations, as checked:

| Implementation | Licence | Notes |
|---|---|---|
| IPOL, LSD 1.6 (www.ipol.im/pub/art/2012/gjmr-lsd/) | AGPL-3.0-or-later (stated on the IPOL page) | the reference; C89, one file |
| darktable `ashift_lsd.c` | the same LSD 1.6 code, AGPL-3.0-or-later | see above |
| OpenCV `modules/imgproc/src/lsd.cpp` (4.x) | 3-clause BSD header in the file; the repository is Apache-2.0 | OpenCV's own note in `imgproc.hpp`: removed in 3.4.6 to 3.4.15 and 4.1.0 to 4.5.3 "due original code license conflict", restored after the NFA code was published under MIT. C++ with `cv::Mat` |
| github.com/rafael-grompone-von-gioi/binomial_nfa | MIT (GitHub licence field and LICENSE) | only the NFA computation (the part that blocked OpenCV), by the LSD author |

So three routes: (a) take LSD 1.6 as darktable does (AGPL part inside a
GPL-3 plug-in); (b) port OpenCV's BSD-licensed C++ to C; (c) write LSD
from the paper, with the MIT NFA code. The algorithm is fully described
in the IPOL article. PLAN.md recommends (b) or (c) and asks.

### OpenCV's lsd.cpp, checked 2026-11-18 (route (b) taken)

- The file today (`modules/imgproc/src/lsd.cpp`, branch 4.x, last
  commit ecb072401a of 2026-11-17): the OpenCV 3-clause BSD licence
  header, "Copyright (C) 2013, OpenCV Foundation"; no other licence in
  it.
- History (GitHub API, commits touching the file): removed on
  2019-03-01 by commit 3ba49ccecc, "imgproc: removed LSD code due
  original code license conflict". The trigger was opencv_contrib issue
  2016 (a "Compare doubles by relative error" section copied from the
  AGPL LSD code); in opencv_contrib issue 2524 (2020-05-10) an OpenCV
  maintainer wrote that "almost all removed code ... has license
  conflict" as a derived work.
- Restored on 2021-10-01 by commit 9b76872708 ("restore LSD", pull
  request 20797, released in 4.5.4), a revert of the removal, plus
  259c39a63a (documentation). The restoring PR followed two things in
  issue 2524: the LSD author, Rafael Grompone von Gioi, published his
  NFA code under the MIT licence
  (github.com/rafael-grompone-von-gioi/binomial_nfa, LICENSE: MIT,
  "Copyright (c) 2007-2021 rafael grompone von gioi"), and on
  2021-05-10 he wrote there: "LSD was already in OpenCV before. Why not
  using the same implementation and only updating the parts where
  there were doubts about the license? This should be minimal work."
  The maintainer then agreed to the revert ("Sure!").
- So: OpenCV ships the code under BSD on the strength of the author's
  public consent (he is the copyright holder of LSD 1.6, "Copyright (c)
  2007-2011 rafael grompone von gioi"); there is no formal relicensing
  document for the parts other than the NFA. The floating point
  comparison of issue 2016 came from Bruce Dawson's blog, which he
  relicensed in 2021 (same issue); our `lsd.c` has its own version.
- Our port: `src/core/lsd.c`, C, keeping OpenCV's BSD notice and this
  history in its header, licensed BSD-3-Clause like its source.

## 3. Alternative: the Hough transform

The classic approach (Kodak's expired 1999 patent US6400848 used an edge
image and a Hough transform, see patents.md). OpenCV's
`HoughLinesP` (Apache-2.0) is the common implementation. It needs an
edge detector with thresholds, an angle and distance quantisation, and
a vote threshold, and it finds infinite lines or loosely joined
segments; LSD gives segments with a width and a precision, which is
exactly what ashift's weights use. Not measured here; the plan keeps
the detector behind one function so that it can be compared on the
test images.

## 4. Adobe "Upright" and the paper behind it

- Lightroom Classic and Camera Raw: Upright modes **Auto** (balance,
  keep as much of the image as possible), **Level** (horizontal),
  **Vertical**, **Full** (Level plus Vertical plus Auto), and
  **Guided** (draw two or more guides). Source: helpx.adobe.com,
  "Upright automatic perspective correction in Lightroom Classic" and
  "How to correct distorted horizontal and vertical perspective in
  photos using Lightroom CC 2015.6" (Guided arrived with Lightroom CC
  2015.6 and ACR 9.6, June 2016).
- The paper: Hyunjoon Lee, Eli Shechtman, Jue Wang, Seungyong Lee,
  "Automatic Upright Adjustment of Photographs", CVPR 2012, pp. 877-884
  (POSTECH and Adobe; Aseem Agarwala is not an author). Extended in
  TPAMI 2014 ("... with Robust Camera Calibration"). It detects line
  segments, estimates vanishing points and the camera (intrinsics and
  rotation, Manhattan and "Atlanta" worlds), and then computes a
  homography by minimising an energy of "human perception" criteria:
  picture frame alignment, eye-level alignment, and a distortion term.
- It is patented; see patents.md. The ashift method is a different,
  older one (ShiftN, public since at least 2006): no camera calibration,
  no vanishing point estimation for the fit (only for outlier removal),
  no eye-level term.

## 5. What GIMP 3.2 has

Checked in the GIMP 3.2.6 Flatpak (org.gimp.GIMP, the one the tests
run) and in the GIMP source at tag `GIMP_3_2_0`.

- **Tools** (Tools > Transform Tools, `menus/image-menu.ui.in.in`):
  Rotate, Scale, Shear, Perspective, 3D Transform, Unified Transform,
  Handle Transform, Flip, Cage, Warp. All manual. The 3D Transform tool
  (since GIMP 2.10.18: "You can set a vanishing point, then rotate the
  layer in X, Y, and Z axes", gimp.org release news) is the closest,
  and it is manual too. Nothing detects lines.
- **Image > Transform**: Flip Horizontally, Flip Vertically, Rotate 90
  clockwise, 90 counter-clockwise, 180, Arbitrary Rotation... Our
  "Upright..." registers at `<Image>/Image/Transform` and shows below
  Arbitrary Rotation (verified in the GUI test, docs/transform-menu.png).
- **Open requests**: GIMP issue #2026 "Allow horizontal/vertical
  perspective correction" (opened 2018-08-13) and #2165 "More generic
  straightening (leveling) of lines" (2018-09-05), both open.
- **GEGL**: the GEGL of GIMP 3.2.6 (`gegl --list-all` inside the
  Flatpak) has `gegl:transform`, `gegl:rotate`, `gegl:rotate-on-center`,
  `gegl:shear`, `gegl:reflect`, `gegl:scale-*`, but **no
  `gegl:perspective`**. `gegl:transform` takes an SVG style string;
  `gegl_matrix3_parse_string` (gegl/gegl-matrix.c) reads
  `matrix(...)` with 9 values, column after column, through `gfloat`.
- **Not as a non-destructive filter.** From a plug-in,
  `gimp_drawable_filter_new (layer, "gegl:transform", ...)` fails in
  GIMP 3.2.6 with "Filters from category "transform" are hidden."
  (tried in the headless GIMP; `gegl:gaussian-blur` works as a
  control). So the plug-in transforms destructively, with undo.
- **The PDB** (listed in the running 3.2.6):
  - `gimp-item-transform-matrix` (item, 9 coefficients): "assembling a
    3x3 matrix from the coefficients passed"; affected by
    `gimp-context-set-interpolation`, `-transform-direction` and
    `-transform-resize`. If there is a selection and the item is a
    drawable, only the selected part is cut and transformed as a
    floating selection (the plug-in must deal with that).
  - `gimp-item-transform-perspective` (item, the new positions of the 4
    corners): the same, given as corners.
  - `gimp-context-set-interpolation`: none, linear, cubic, NoHalo,
    LoHalo.
  - `gimp-context-set-transform-resize`: Adjust (enlarge to show all),
    Clip, **Crop to result**, **Crop with aspect**. GIMP computes the
    crop itself (`app/core/gimp-transform-resize.c`,
    `gimp_transform_resize_crop`): it builds candidate rectangles from
    the transformed outline's corners and edge intersections and keeps
    the one of largest area. That is ashift's "largest area" and
    "original format" without our own code; whether it matches
    ashift's Nelder-Mead crop is to be measured.
  - `gimp-image-crop`, `gimp-image-resize-to-layers` for the canvas.
  - `gimp-path-get-strokes` and `gimp-path-stroke-get-points` for the
    guided mode (the plug-in skeleton already reads them).

## 6. Test images

Generated: `tests/synth.py` renders a facade through a pinhole camera
with known roll, pitch and yaw (ray casting, 4x4 samples per pixel), so
the truth does not come from our own homography.

Real, freely licensed (licence read on each file page on Wikimedia
Commons, in the file's wikitext):

| File | Licence on the page | Author, date | Why |
|---|---|---|---|
| [Kirkgate Market 02.jpg](https://commons.wikimedia.org/wiki/File:Kirkgate_Market_02.jpg) | `{{self|cc-zero}}` (CC0) | Immanuel Giel, 2018-08-29 | strongly converging verticals, ornate facade |
| [Stolbergs "Bankenmeile" in der Rathausstraße.jpg](https://commons.wikimedia.org/wiki/File:Stolbergs_%22Bankenmeile%22_in_der_Rathausstra%C3%9Fe.jpg) | `{{PD-self}}` (public domain) | Aufklärung, 2006-08 | verticals plus receding horizontals: the full mode |
| [Iwase Bunko Library1.JPG](https://commons.wikimedia.org/wiki/File:Iwase_Bunko_Library1.JPG) | `{{self|Cc-zero}}` (CC0) | At by At, 2011-05-15 | a long low building seen at an angle: the horizontal mode |
| [Gooderham (Flatiron) Building.Toronto. (49145357446).jpg](https://commons.wikimedia.org/wiki/File:Gooderham_(Flatiron)_Building.Toronto._(49145357446).jpg) | `{{cc-zero}}` (CC0; from Flickr, FlickreviewR passed with licence cc-zero, 2022-10-18) | Bernard Spragg. NZ, 2019-09-28 | towers seen from below in a street |
| [Iwase Bunko Library2.JPG](https://commons.wikimedia.org/wiki/File:Iwase_Bunko_Library2.JPG) | `{{self|Cc-zero}}` (CC0) | At by At, 2011-05-15 | a long facade at a steep angle |
| [Former Tonoi Post Office Building - front facade.jpg](https://commons.wikimedia.org/wiki/File:Former_Tonoi_Post_Office_Building_-_front_facade.jpg) | `{{self|cc-zero}}` (CC0) | Huugo, 2026-11-01 | a small facade, nearly frontal, slightly from below |
| [Fifty-Ninth Bank Head Office Main Building 20200921.jpg](https://commons.wikimedia.org/wiki/File:Fifty-Ninth_Bank_Head_Office_Main_Building_20200921.jpg) | `{{self|cc-zero}}` (CC0) | Kaburamata, 2020-09-21 | frontal, taken with care: to leave nearly alone |
| [Chester Cathedral Nave 1.jpg](https://commons.wikimedia.org/wiki/File:Chester_Cathedral_Nave_1.jpg) | `{{self|cc-zero}}` (CC0) | Michael D Beckwith, 2019-11-14 | a symmetric interior |
| [Nave - Church of St Mary the Virgin.jpg](https://commons.wikimedia.org/wiki/File:Nave_-_Church_of_St_Mary_the_Virgin.jpg) | `{{self|cc-zero}}` (CC0) | LAS30a, 2024-12-30 | an interior, tilted |
| [Commercial street scene in Arusha city centre, showing M-Pesa and NMB Wakala agents.jpg](https://commons.wikimedia.org/wiki/File:Commercial_street_scene_in_Arusha_city_centre,_showing_M-Pesa_and_NMB_Wakala_agents.jpg) | `{{self|cc-zero}}` (CC0) | Brayan Tesha, 2026-04-23 | a street with poles, wires, people and signs |
| [Broadway street scene at 116th Street, Manhattan, New York City.jpg](https://commons.wikimedia.org/wiki/File:Broadway_street_scene_at_116th_Street,_Manhattan,_New_York_City.jpg) | `{{self|cc-zero}}` (CC0) | Z3lvs, 2023-07-14 | a street from a height, many road lines |
| [Hartz Lake at Hartz Mountain.jpg](https://commons.wikimedia.org/wiki/File:Hartz_Lake_at_Hartz_Mountain.jpg) | `{{self|cc-zero}}` (CC0) | JustinH, 2021-01-21 | a landscape without straight lines |
| [Autumn Forest Path with Tall Trees.jpg](https://commons.wikimedia.org/wiki/File:Autumn_Forest_Path_with_Tall_Trees.jpg) | `{{self|cc-zero}}` (CC0) | Vovogov90, 2025-02-08 | a forest: tree trunks, no building |

The first three are in Commons' "Images requiring perspective
correction" category. The other ten (found with Commons' search
restricted to `incategory:CC-zero`, the licence then read in each
file's wikitext on 2026-11-18, and the API's LicenseShortName "CC0"
for each) are for the tuning of the automatic mode (PLAN.md 3.4 and
5.3). CC0 asks for no credit; the authors are named here and next to
any photo the README shows anyway. None of the user's own photos is used, now or later.
