# gimp-auto-perspective

Automatic line based perspective correction for GIMP 3:
find the lines of a building that should be vertical and horizontal,
fit a rotation, lens shifts and a shear that make them so, and transform
the image. The method is that of darktable's "rotate and perspective"
module (ashift, after ShiftN).

**Status: it corrects; the dialog with its preview has passed its first
run in GIMP's window (OK applies the correction; PLAN.md 5.5).** Line detection
(LSD), the fit (ashift's model, cost and RANSAC, our own Nelder-Mead),
the crop and the warp run in the core and on the command line, tested
against darktable's homography, LSD 1.6's output and facades rendered
through a camera with known angles: after the fit the facade edges are
straight within 0.02 degrees (PLAN.md, section 5.2). The plug-in
applies the result to the whole image (layers, channels, paths, the
selection) or to the layer, in one undo group, and crops; in GIMP the
facade's true edges come out vertical within 0.005 degrees (5.4). On
13 freely licensed real photos (buildings, interiors, streets, a forest
and a landscape) the automatic mode was looked at before and after and
measured: on the nine with a building's lines the strongest verticals
end up within 0.08 to 0.47 degrees (weighted median), photos taken
straight are left nearly alone, and a landscape without straight lines is left alone
entirely, in under a second each (5.3). The guided mode, with lines
drawn as a path, is measured on known edges too (5.4).
The plan is in [PLAN.md](PLAN.md); the research behind it in
[docs/research.md](docs/research.md) and
[docs/patents.md](docs/patents.md).

![Image > Transform > Auto Perspective...](docs/transform-menu.png)

## Before and after

Kirkgate Market in Leeds, taken from the street looking up; the
automatic mode chose Vertical (rotation -4.09 degrees, vertical shift
0.341) and the largest rectangle keeps 78 % of the photo. Made with
`auto-perspective-cli warp --mode=auto --crop=largest --interp=cubic`,
the same core and the same numbers as the plug-in (PLAN.md, 5.3 and
5.4).

| Before | After |
|---|---|
| ![Kirkgate Market, before](docs/kirkgate-before.jpg) | ![Kirkgate Market, corrected](docs/kirkgate-after.jpg) |

Photo: [Kirkgate Market 02.jpg](https://commons.wikimedia.org/wiki/File:Kirkgate_Market_02.jpg)
by Immanuel Giel, 2018, Wikimedia Commons, released under
[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) (no
credit is required; given anyway). Scaled down here; the right hand
picture is our correction of it.

## What is there

- `src/core/`: the core, without GIMP: `lsd.c` (LSD, a C port of
  OpenCV's BSD licensed implementation), `model.c` (ashift's homography
  and cost), `simplex.c` (Nelder-Mead), `ransac.c` (outliers),
  `crop.c` (the largest rectangle, and the warp), `perspective.c`
  (detection, merging of collinear segments, the fit and the automatic
  mode); the interface is `perspective.h`.
- `tools/auto-perspective-cli.c`: the core on the command line (PFM
  images, or guide lines from a text file): `lines`, `fit`, `warp`,
  `cost`, `lsd`.
- `src/plugin/main.c`: **Image > Transform > Auto Perspective...**
  (`plug-in-auto-perspective`) with its modes (Automatic, Vertical, Horizontal,
  Full, Rotation only, Guided by a path), crop, apply-to,
  interpolation, focal length (0: from EXIF), lens dependence and four
  fine tuning offsets, and a dialog with a preview (the result, the
  lines used, the crop). It returns the status, the four values and the
  mode used, so it can be scripted.
- Tests, all isolated from your own folders:
  - `tests/check.sh`: unit tests of the core, as usual and under
    AddressSanitizer and UBSan, `auto-perspective-cli` under the
    sanitizers, and `tests/recovery.py`: facades rendered through a
    camera with known angles, fitted, and measured on their true edges.
  - `tests/lsd-ref.sh`: our LSD against the reference LSD 1.6 (fetched
    at test time into tests/output, AGPL, never shipped); not part of
    `run.sh`.
  - `tests/real.sh`: 13 freely licensed photos from Wikimedia Commons
    (CC0 and public domain, the licence read on each file page;
    fetched into tests/output, never committed), fitted and corrected,
    with what the automatic mode picks, time and peak memory; not part
    of `run.sh` (it needs the network once).
  - `tests/gimp-test.sh` (also `--asan`): the plug-in in GIMP without a
    window: registration, the same parameters and pixels as the command
    line, paths, channels and the selection, every mode and format, the
    guided mode measured on known edges.
  - `tests/gui/gui-test.sh`: the menu, the dialog and undo in GIMP's
    window on Broadway (needs Chrome and node).
  - `tests/run.sh`: all of them.
  - `tests/synth.py`: synthetic facades seen by a camera turned by known
    angles, the truth for the recovery tests to come.

The dialog on a synthetic facade seen from below: the lines it found
(green vertical, blue horizontal) and the crop (yellow) in the preview,
the settings beside it.

![The Auto Perspective dialog](docs/dialog.png)

After OK: the verticals straightened and the canvas cropped.

![The facade corrected](docs/result.png)

## Building

Against the native GIMP 3 and its development files (on Debian and
Ubuntu `sudo apt install gimp libgimp-3.0-dev meson ninja-build`),
through gimp-devtools next to this folder:

```
../gimp-devtools/gimp-build.sh . meson setup build
../gimp-devtools/gimp-build.sh . ninja -C build
tests/run.sh
```

`meson setup build -Dplugin=disabled` builds only the core, the CLI and
the unit tests. Nothing here installs into your GIMP unless you run
`ninja install` with a `-Dplugindir` of your choice.

## Licence

GPL-3.0-or-later (COPYING). Parts are ported, with attribution in each
file, from darktable's ashift (GPL-3.0-or-later): the homography, the
cost, RANSAC and the crop. `src/core/lsd.c` is a port of OpenCV's
`lsd.cpp` and keeps its 3-clause BSD licence (its header and
docs/research.md have the history of that file's licence).
`tests/ref/ashift-homography.c` is darktable's code, copied unchanged
as a test oracle. Copyright 2026 David.
