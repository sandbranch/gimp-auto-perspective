#!/usr/bin/env python3
"""Our LSD (src/core/lsd.c, through auto-perspective-cli lsd) against the
reference implementation, LSD 1.6 of the IPOL article (tests/lsd-ref.sh
fetches and builds it in tests/output; it is never shipped):

    tests/lsd-ref.py CLI REFDIR OUTDIR

1. chairs.pgm, the article's own example, at LSD's default scale 0.8,
   against chairs.lsd.txt that comes with LSD 1.6;
2. synthetic facades (tests/synth.py, quantised to 8 bits for both) at
   ashift's scale 0.99, against the reference built from source.

A reference segment counts as found if one of ours has both end points
within 1 pixel of its end points (in either order); LSD 1.6 puts (0, 0)
at the centre of the top left pixel, we at its corner, so ours are moved
by half a pixel first. Prints the share of the reference segments found,
by number and by length, and the share of ours that match one of the
reference; "PASS ..." or "FAIL ..." against the thresholds below.

Copyright 2026 David
SPDX-License-Identifier: GPL-3.0-or-later
"""
import importlib.util
import os
import subprocess
import sys

import numpy as np

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('synth', os.path.join(here, 'synth.py'))
synth = importlib.util.module_from_spec(spec)
spec.loader.exec_module(synth)

# measured 2026-11-18 on chairs.pgm: 89.9 % of the reference length
# found, 92.0 % of ours matching, 95.6 % of the reference segments of 40
# pixels or more found. The rest are short segments of low significance
# (median -log10 NFA 1.7 against 13.5 for the matched ones), where
# OpenCV's code, which ours is ported from, differs from LSD 1.6 (its
# rect_nfa, for one, was rewritten in 2023). The thresholds sit a little
# below the measured values.
MIN_FOUND_LENGTH = 0.85
MIN_OURS_MATCH = 0.88
MIN_FOUND_LONG = 0.93
LONG = 40.0
failures = 0


def check(ok, what):
    global failures
    print('%s %s' % ('PASS' if ok else 'FAIL', what), flush=True)
    if not ok:
        failures += 1


def read_pgm(path):
    with open(path, 'rb') as f:
        data = f.read()
    parts = []
    pos = 0
    # header: magic, width, height, maxval (comments skipped)
    while len(parts) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b'#':
            pos = data.index(b'\n', pos)
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[pos:end])
        pos = end
    w, h, maxval = int(parts[1]), int(parts[2]), int(parts[3])
    if parts[0] == b'P2':
        vals = np.array(data[pos:].split()[:w * h], dtype=float)
    else:
        vals = np.frombuffer(data[pos + 1:pos + 1 + w * h], dtype=np.uint8).astype(float)
    return vals.reshape(h, w) * (255.0 / maxval)


def write_pgm(path, img8):
    h, w = img8.shape
    with open(path, 'wb') as f:
        f.write(b'P5\n%d %d\n255\n' % (w, h))
        f.write(img8.astype(np.uint8).tobytes())


def segments(text):
    rows = [list(map(float, l.split()[:4])) for l in text.splitlines() if l.strip()]
    return np.array(rows).reshape(-1, 4)


def compare(ref, ours, tol=1.0):
    """shares of the reference found (count, length) and of ours matched"""
    if len(ref) == 0 or len(ours) == 0:
        return 0.0, 0.0, 0.0, 0.0
    o = ours - 0.5   # our corner based coordinates to LSD 1.6's

    def match(a, b):
        d1 = np.maximum(np.hypot(a[:, None, 0] - b[None, :, 0], a[:, None, 1] - b[None, :, 1]),
                        np.hypot(a[:, None, 2] - b[None, :, 2], a[:, None, 3] - b[None, :, 3]))
        d2 = np.maximum(np.hypot(a[:, None, 0] - b[None, :, 2], a[:, None, 1] - b[None, :, 3]),
                        np.hypot(a[:, None, 2] - b[None, :, 0], a[:, None, 3] - b[None, :, 1]))
        return (np.minimum(d1, d2) <= tol).any(axis=1)

    found = match(ref, o)
    length = np.hypot(ref[:, 2] - ref[:, 0], ref[:, 3] - ref[:, 1])
    long_ = length >= LONG
    return (found.mean(), (length * found).sum() / length.sum(), match(o, ref).mean(),
            found[long_].mean() if long_.any() else 1.0)


def run(cli, scale, pfm):
    return segments(subprocess.run([cli, 'lsd', '--scale=%g' % scale, pfm], capture_output=True,
                                   text=True, check=True).stdout)


def main():
    cli, refdir, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)

    chairs = read_pgm(os.path.join(refdir, 'chairs.pgm'))
    pfm = os.path.join(out, 'chairs.pfm')
    synth.write_pfm(pfm, chairs / 255.0)
    with open(os.path.join(refdir, 'chairs.lsd.txt')) as f:
        ref = segments(f.read())
    ours = run(cli, 0.8, pfm)
    n, l, m, g = compare(ref, ours)
    print('chairs.pgm, scale 0.8: reference %d segments, ours %d; reference found %.1f %% '
          '(%.1f %% of the length, %.1f %% of those of %g pixels or more); ours matching %.1f %%'
          % (len(ref), len(ours), 100 * n, 100 * l, 100 * g, LONG, 100 * m))
    check(l >= MIN_FOUND_LENGTH and m >= MIN_OURS_MATCH and g >= MIN_FOUND_LONG,
          'LSD on chairs.pgm against LSD 1.6\'s own output: %.1f %% of the length found, %.1f %% of the '
          'long segments, %.1f %% of ours match' % (100 * l, 100 * g, 100 * m))

    lsd = os.path.join(refdir, 'lsd')
    if not os.access(lsd, os.X_OK):
        print('SKIP the facades against LSD 1.6: no built reference %s' % lsd)
        return 1 if failures else 0
    for name, kw in (('flat', {}), ('pitch', dict(pitch=12, roll=2)), ('full', dict(pitch=-8, yaw=14, roll=-1)),
                     ('noisy', dict(pitch=10, yaw=-6, noise=0.03, clutter=5))):
        img, _, _ = synth.render(400, 300, kw.get('roll', 0), kw.get('pitch', 0), kw.get('yaw', 0), 28.0)
        rng = np.random.default_rng(3)
        if kw.get('clutter'):
            img = synth.add_clutter(img, kw['clutter'], rng)
        if kw.get('noise'):
            img = img + rng.normal(0, kw['noise'], img.shape)
        img8 = np.clip(np.round(img * 255.0), 0, 255)
        pgm = os.path.join(out, 'lsdref-%s.pgm' % name)
        pfm = os.path.join(out, 'lsdref-%s.pfm' % name)
        txt = os.path.join(out, 'lsdref-%s.txt' % name)
        write_pgm(pgm, img8)
        synth.write_pfm(pfm, img8 / 255.0)
        subprocess.run([lsd, '-s', '0.99', pgm, txt], check=True, capture_output=True)
        with open(txt) as f:
            ref = segments(f.read())
        ours = run(cli, 0.99, pfm)
        n, l, m, g = compare(ref, ours)
        print('facade %s, scale 0.99: reference %d, ours %d; reference found %.1f %% (%.1f %% of the length, '
              '%.1f %% of the long ones); ours matching %.1f %%'
              % (name, len(ref), len(ours), 100 * n, 100 * l, 100 * g, 100 * m))
        check(l >= MIN_FOUND_LENGTH and m >= MIN_OURS_MATCH and g >= MIN_FOUND_LONG,
              'LSD on the %s facade against LSD 1.6: %.1f %% of the length found, %.1f %% of the long '
              'segments, %.1f %% of ours match' % (name, 100 * l, 100 * g, 100 * m))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
