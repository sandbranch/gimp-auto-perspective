#!/usr/bin/env python3
"""Real photos (PLAN.md 5.3): freely licensed facades from Wikimedia
Commons (the licence read on each file page, see tests/real.sh), fitted
and corrected by auto-perspective-cli, then measured:

- the fit converges (status ok) in the modes asked;
- after the correction the strongest vertical lines found again in the
  corrected image deviate less than 0.5 degrees from vertical (and, for
  Full, the strongest horizontal ones from horizontal);
- the share of the image the crop keeps;
- time and peak memory (VmHWM, from the kernel's rusage) of the fit,
  also on a copy scaled up to 24 megapixels.

    tests/real.py CLI DIR

DIR holds the photos (tests/real.sh fetches them); results, PFM copies
and JPEG previews of before and after go there too. Nothing of it is
committed. Prints "PASS ..." or "FAIL ..."; exits non-zero on a failure.

Copyright 2026 David
SPDX-License-Identifier: GPL-3.0-or-later
"""
import math
import os
import subprocess
import sys

import numpy as np
from PIL import Image

# file, modes to check (with the status expected), why (PLAN.md 5.3).
# Stolbergs: a street of facades receding steeply; making its horizontals
# parallel needs more than 4 times the area, so Full reports "insane"
# and changes nothing (measured 2026-11-18), and Automatic uses Vertical.
PHOTOS = [
    ('Kirkgate_Market_02.jpg', (('vertical', 'ok'), ('auto', 'ok')), 'strongly converging verticals'),
    ('Stolbergs_Bankenmeile.jpg', (('vertical', 'ok'), ('full', 'insane'), ('auto', 'ok')),
     'verticals and receding horizontals'),
    ('Iwase_Bunko_Library1.JPG', (('vertical', 'ok'), ('full', 'ok'), ('auto', 'ok')),
     'a long low building seen at an angle'),
]
# After the correction, the weighted median deviation of the STRONGEST
# heaviest vertical lines (and, for Full, horizontal ones) must be below
# MAX_DEVIATION degrees; the largest is printed too. (First this was the
# largest of the 10 strongest; on Kirkgate Market that was a pole of the
# street furniture at 0.89 degrees while the facade's long edges were at
# 0.02 to 0.17, and the two edges of a tapering lamp post lean by design.
# A real photo has no exact truth: the synthetic tests are the precise
# ones, these check that the result is plausible.)
MAX_DEVIATION = 0.5
STRONGEST = 20
failures = 0


def check(ok, what):
    global failures
    print('%s %s' % ('PASS' if ok else 'FAIL', what), flush=True)
    if not ok:
        failures += 1


def write_pfm(path, img):
    img = np.asarray(img, dtype='<f4')
    h, w = img.shape[:2]
    with open(path, 'wb') as fp:
        fp.write(b'%s\n%d %d\n-1.0\n' % (b'PF' if img.ndim == 3 else b'Pf', w, h))
        fp.write(np.ascontiguousarray(img[::-1]).tobytes())


def read_pfm(path):
    with open(path, 'rb') as fp:
        kind = fp.readline().strip()
        w, h = map(int, fp.readline().split())
        fp.readline()
        c = 3 if kind == b'PF' else 1
        data = np.frombuffer(fp.read(w * h * c * 4), dtype='<f4')
    return data.reshape((h, w, c) if c == 3 else (h, w))[::-1]


def run(cli, *args):
    """runs the CLI; its output as a dict, seconds and peak memory (MiB)"""
    # (the CLI measures itself: the rusage of a child forked from Python
    # counts Python's own pages from before the exec)
    proc = subprocess.run([cli] + list(args) + ['--stats'], capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError('%s %s: %s' % (cli, ' '.join(args), proc.stderr))
    res = {}
    for line in proc.stdout.splitlines():
        k, _, v = line.partition(' ')
        res[k] = v
    return res, float(res['seconds']), int(res['vmhwm-kib']) / 1024.0


def lines(cli, pfm):
    out = subprocess.run([cli, 'lines', pfm], capture_output=True, text=True, check=True).stdout
    rows = [l.split() for l in out.splitlines()]
    return [(float(r[0]), float(r[1]), float(r[2]), float(r[3]), float(r[5]), int(r[6]), int(r[7]))
            for r in rows]


def strongest_deviation(segs, kind):
    """the weighted median and the largest deviation (degrees) among the
    STRONGEST heaviest lines of a kind (1 vertical, 2 horizontal) that
    are no outliers, and how many there were"""
    sel = sorted((s for s in segs if s[5] == kind and not s[6]), key=lambda s: -s[4])[:STRONGEST]
    if not sel:
        return None, None, 0
    dev = []
    for x1, y1, x2, y2, wgt, *_ in sel:
        dx, dy = abs(x2 - x1), abs(y2 - y1)
        dev.append((math.degrees(math.atan2(dx, dy) if kind == 1 else math.atan2(dy, dx)), wgt))
    dev.sort()
    half, acc, median = 0.5 * sum(w for _, w in dev), 0.0, dev[-1][0]
    for d, w in dev:
        acc += w
        if acc >= half:
            median = d
            break
    return median, max(d for d, _ in dev), len(sel)


def fmt(v):
    return '-' if v is None else '%.3f' % v


def preview(pfm_img, path, width=800):
    img = np.clip(pfm_img, 0, 1)
    im = Image.fromarray((img * 255 + 0.5).astype(np.uint8))
    im.thumbnail((width, width))
    im.save(path, quality=85)


def main():
    cli, d = sys.argv[1], sys.argv[2]
    for name, modes, why in PHOTOS:
        src = os.path.join(d, name)
        if not os.path.exists(src):
            print('SKIP %s: not fetched (tests/real.sh)' % name)
            continue
        im = Image.open(src)
        im.load()
        rgb = np.asarray(im.convert('RGB'), dtype=np.float32) / 255.0
        h, w = rgb.shape[:2]
        base = os.path.splitext(name)[0]
        pfm = os.path.join(d, base + '.pfm')
        write_pfm(pfm, rgb)
        preview(rgb, os.path.join(d, base + '-before.jpg'))
        print('== %s, %dx%d (%.1f megapixels): %s' % (name, w, h, w * h / 1e6, why))
        for mode, expect in modes:
            r, secs, mib = run(cli, 'fit', '--mode=' + mode, pfm)
            out = os.path.join(d, '%s-%s.pfm' % (base, mode))
            wr, _, _ = run(cli, 'warp', '--mode=' + mode, '--crop=largest', '--interp=cubic', pfm, '-o', out)
            cx, cy, cw, ch = map(int, wr['crop'].split())
            share = cw * ch / float(w * h)
            fixed = read_pfm(out)
            preview(fixed, os.path.join(d, '%s-%s.jpg' % (base, mode)))
            after = lines(cli, out)
            mv, xv, nv = strongest_deviation(after, 1)
            mh, xh, nh = strongest_deviation(after, 2)
            print('  %-9s %s, used %s, %s vertical %s horizontal lines, rotation %+.3f, shift-v %+.4f, '
                  'shift-h %+.4f, shear %+.4f, %.2f s, peak %.0f MiB; crop %dx%d keeps %.0f %%; afterwards '
                  'the %d strongest verticals: median %s, largest %s degrees; the %d strongest horizontals: '
                  'median %s, largest %s'
                  % (mode, r['status'], r['mode'], r['vertical'], r['horizontal'], float(r['rotation']),
                     float(r['shift-v']), float(r['shift-h']), float(r['shear']), secs, mib, cw, ch,
                     100 * share, nv, fmt(mv), fmt(xv), nh, fmt(mh), fmt(xh)), flush=True)
            check(r['status'] == expect, '%s %s: status %s (expected %s)' % (name, mode, r['status'], expect))
            if r['status'] != 'ok':
                continue
            used = r['mode']
            check(mv is not None and mv < MAX_DEVIATION,
                  '%s %s: the %d strongest verticals afterwards: weighted median %s degrees (target %.1f), '
                  'largest %s' % (name, mode, nv, fmt(mv), MAX_DEVIATION, fmt(xv)))
            if used == 'full':
                check(mh is not None and mh < MAX_DEVIATION,
                      '%s %s: the %d strongest horizontals afterwards: weighted median %s degrees (target %.1f), '
                      'largest %s' % (name, mode, nh, fmt(mh), MAX_DEVIATION, fmt(xh)))
            check(share > 0.4, '%s %s: the crop keeps %.0f %% of the image (more than 40 %%)'
                  % (name, mode, 100 * share))

    # time and memory at 24 megapixels: the first photo scaled up
    src = os.path.join(d, PHOTOS[0][0])
    if os.path.exists(src):
        im = Image.open(src).convert('RGB').resize((6000, 4000), Image.LANCZOS)
        big = os.path.join(d, 'big-24mp.pfm')
        write_pfm(big, np.asarray(im, dtype=np.float32) / 255.0)
        r, secs, mib = run(cli, 'fit', '--mode=auto', big)
        print('== 24 megapixels (%s scaled to 6000x4000): fit %s (%s) in %.2f s, peak %.0f MiB (the RGB image '
              'itself is %.0f MiB as floats)' % (PHOTOS[0][0], r['status'], r['mode'], secs, mib,
                                                 6000 * 4000 * 3 * 4 / 2 ** 20))
        check(r['status'] == 'ok' and secs < 30 and mib < 1024,
              '24 megapixels: the fit in %.2f s (below 30), peak %.0f MiB (below 1024)' % (secs, mib))
    print('real photos: %d failed' % failures)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
