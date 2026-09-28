# Patents near automatic upright correction

**Not legal advice.** A record of what the patent documents say, read on
2026-09-28 on Google Patents and in the USPTO's full text PDFs (the
links below). "Status" is what Google Patents states; that is derived
data, not the USPTO's official maintenance fee record. Where something
could not be read (Google Patents rate limited part of the search), it
says "not checked".

## Summary

- Adobe's Upright is covered by two families from one 2012 provisional
  (US 61/639,721), by Shechtman, Jue Wang, Hyunjoon Lee and Seungyong
  Lee: "Automatic adjustment of images using a homography" and "Camera
  calibration and automatic adjustment of images". All US members are
  **active**, expiring 2033.
- The calibration patents claim specific steps (joint estimation of
  camera intrinsics, rotation and vanishing points from an energy;
  several orthogonal frames; eye-level estimation). The ashift method
  does none of these.
- The homography patents are broader: a homography (or "planar
  transformation") computed "according to one or more human
  perception-based criteria" so that lines align with an image axis.
  Whether a plain line deviation cost counts as such a criterion is the
  open point; the specification uses the phrase for its picture frame
  alignment term, next to eye-level and distortion terms that ashift
  does not have.
- The ashift method is older than Adobe's priority date: ShiftN did
  automatic correction of converging verticals publicly in 2006, and
  Kodak's 1999 patent (line detection, then a perspective correction
  from the line) is expired published prior art. darktable has shipped
  ashift since 2016.
- No Adobe patent specific to **Guided Upright** (user-drawn lines) was
  found; the search was not exhaustive.

## Adobe, "Upright" (priority US 61/639,721, 2012-04-27)

Inventors on every member: Elya Shechtman, Jue Wang, Hyunjoon Lee,
Seungyong Lee. Assignee Adobe Inc (the PCT, CN and DE members together
with POSTECH Academy-Industry Foundation). The matching paper: Lee,
Shechtman, Wang, Lee, "Automatic Upright Adjustment of Photographs",
CVPR 2012.

| Patent | Title | Filed | Granted | Status (Google Patents) |
|---|---|---|---|---|
| [US9008460B2](https://patents.google.com/patent/US9008460B2/en) | Automatic adjustment of images using a homography | 2013-04-26 | 2015-04-14 | Active, expires 2033-05-14 |
| [US9582855B2](https://patents.google.com/patent/US9582855B2/en) | same (continuation) | 2015-04-08 | 2017-02-28 | Active, expires 2033-06-25 |
| [US9098885B2](https://patents.google.com/patent/US9098885B2/en) | Camera calibration and automatic adjustment of images | 2013-04-26 | 2015-08-04 | Active, anticipated 2033-04-26 |
| [US9519954B2](https://patents.google.com/patent/US9519954B2/en) | same (continuation) | 2015-07-13 | 2016-12-13 | Active, anticipated 2033-04-26 |
| [US9729787B2](https://patents.google.com/patent/US9729787B2/en) | same (continuation) | 2016-11-21 | 2017-08-08 | Active, anticipated 2033-04-26 |
| [WO2013163579A2](https://patents.google.com/patent/WO2013163579A2/en) | PCT | | | Ceased |
| [GB2516405B](https://patents.google.com/patent/GB2516405B/en) | | | 2016-06-15 | Active, anticipated 2033-04-26 (claims not checked) |
| [CN105453131B](https://patents.google.com/patent/CN105453131B/en) | | | 2019-04-12 | Active, anticipated 2033-04-26 (claims not checked) |
| [DE112013002200T5](https://patents.google.com/patent/DE112013002200T5/en) | | | | Pending (claims not checked) |

The independent claims, briefly:

- **US9008460** (claims 1, 14, 19): obtain camera information for an
  image with line features; compute a homography from it "at least in
  part according to one or more human perception-based criteria";
  reproject the image so that the lines align with an image axis. The
  criteria are named in dependent claims: picture frame alignment (10),
  eye-level alignment through the line joining two vanishing points
  (11), image distortion (12); K and R matrices and vanishing points in
  claims 4 to 6.
  The 11.5 year maintenance fee window of this patent ends about
  2026-10-14 (plus a 6 month grace period): our own calculation from
  the grant date, not checked with the USPTO.
- **US9582855**: claim 1 as above (a device); claims 11 and 18 are
  broader: image data (an image axis and the lines of a feature that
  form its perspective) and "a planar transformation of the digital
  image for an upright adjustment of the feature ... computed at least
  in part according to one or more human perception based criteria".
  No camera calibration or vanishing points are required in these two.
- **US9098885** (claims 1, 12): calibration with a first orthogonal
  (Manhattan) frame and further orthogonal frames not aligned with it
  (an "Atlanta world"), then adjustment with the estimated parameters.
- **US9519954** (claims 1, 9, 17): extract line segments, initialise an
  energy from them, and optimise it iteratively to estimate the
  intrinsic matrix K, the orientation R and the vanishing points "in
  conjunction with each other".
- **US9729787** (claims 1, 9, 15): an orthogonal frame plus an extra
  horizontal direction with the same vertical; estimate the eye level
  from it; calibrate and adjust by perception criteria that include the
  eye level.

### Other Adobe patents nearby (not on this method)

- [US10652472B2](https://patents.google.com/patent/US10652472B2/en)
  "Enhanced automatic perspective and horizon correction" (filed
  2018-02-22, active, expires 2038-08-13): claim 1 needs orientation
  sensor data from capture time. Its claim 8 (horizon tilt from the
  image) was only seen in its preamble: not checked.
- [US11393072B2](https://patents.google.com/patent/US11393072B2/en)
  (a CNN for rotation) and
  [US10515460B2](https://patents.google.com/patent/US10515460B2/en)
  (neural network camera calibration): machine learning claims, not
  this method.

## Older and other line based patents

| Patent | Owner | What | Status |
|---|---|---|---|
| [US6400848B1](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/6400848) | Kodak (Gallagher), filed 1999-03-30 | locate a line (edge image, Hough transform), compute a perspective correction from it, resample | expired by 2019-03-30 at the latest (20 years from filing, no term adjustment on the front page): our calculation |
| [US6968094B1](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/6968094) | Kodak (Gallagher), filed 2000-09-15 | estimate and correct camera rotation from a vanishing point | expired about mid 2022 (633 days adjustment): our calculation |
| [US7893963B2](https://patents.google.com/patent/US7893963B2/en) | Kodak, now Monument Peak Ventures | in-camera: lines of horizontal and vertical structures, rotation from their intersections, warp | Expired, lifetime (2024-04-19) |
| [US6963365B2](https://patents.google.com/patent/US6963365B2/en) | HP | in-camera correction of converging verticals from a detected segment | Expired, lifetime (2023-05-23) |
| [US9064309B2](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/9064309) | Google (Chaudhury, DiVerdi), filed 2013-10-29 | edgelets, vanishing points from pairs of edgelets chosen by how many edgelets agree, transform (claim read on [WO2015066067A1](https://patents.google.com/patent/WO2015066067A1/en)) | **not checked** (rate limited); nominal expiry about 2033-11; the WO is ceased |
| [US9183620B2](https://patents.google.com/patent/US9183620B2/en) | IBM | focus plane tilt, not perspective lines | Expired, fee related |
| [US9589333B2](https://patents.google.com/patent/US9589333B2/en) | Casio | document image distortion | Expired, fee related |

Not found, search cut short: specific Apple, Microsoft, Nokia or Canon
patents on line based photo keystone correction (the searches mostly
returned projector keystone patents, which use sensors, and document
imaging patents). A Canon vanishing point patent (US8045804B2) showed up
and was not opened.

## Prior art dates

- ShiftN (Marcus Hebel): an XnView forum thread of 2006-09-17
  (newsgroup.xnview.com/viewtopic.php?t=8786) already shows automatic
  correction of converging verticals; v3.0 January 2008; v4.0 May 2013.
  Its method page (www.shiftn.de/ShiftN_functionality.html): find
  straight lines, keep the sufficiently vertical ones, optimise so they
  become parallel; lines can be deselected.
- darktable ashift: announced 2016-03-10 by Ulrich Pegelow
  (darktable.org, "A new module for automatic perspective correction"),
  shear added 2016-03-18, "inspired by ShiftN".
- Academic vanishing point work goes back to Barnard 1983 and Lutton et
  al., PAMI 1994 (as cited by the Kodak patents).

## What this means for the plan (facts, not advice)

The plan follows ashift: segments, classification, a line deviation
cost, Nelder-Mead over rotation, lens shifts and shear. It does not
calibrate a camera, estimate the eye level, use Manhattan or Atlanta
frames, or weigh perception terms. Whether to go ahead with the
automatic mode while the Adobe homography patents are active, and
whether to have this looked at by someone qualified, is the user's
decision (PLAN.md, open questions).
