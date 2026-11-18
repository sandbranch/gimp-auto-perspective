#!/usr/bin/env python3
"""Recovery of known camera angles (PLAN.md, section 5.2): renders facades
with tests/synth.py through a camera turned by known angles, lets
auto-perspective-cli fit them, and measures the result on the true
facade edges: each edge is projected through the true camera (so the
truth does not come from our model), then through the fitted matrix,
and its deviation from vertical (or horizontal) is measured in degrees.

    tests/recovery.py CLI OUTDIR [--quick]

Prints one line per case and "PASS ..." or "FAIL ..." per check; exits
non-zero if a check failed. The targets (PLAN.md): the largest deviation
of a vertical (Vertical mode) or of any edge (Full mode) below 0.1
degrees on clean images and below 0.3 with noise and clutter; with pitch
and yaw zero the fitted rotation equals the roll within 0.05 degrees;
the failure cases report and change nothing.

Copyright 2026 David
SPDX-License-Identifier: GPL-3.0-or-later
"""
import importlib.util
import math
import os
import subprocess
import sys

import numpy as np

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('synth', os.path.join(here, 'synth.py'))
synth = importlib.util.module_from_spec(spec)
spec.loader.exec_module(synth)

failures = 0


def check(ok, what):
    global failures
    print('%s %s' % ('PASS' if ok else 'FAIL', what), flush=True)
    if not ok:
        failures += 1


def render(path, w, h, roll=0.0, pitch=0.0, yaw=0.0, focal=28.0, noise=0.0, clutter=0, seed=1):
    img, f, R = synth.render(w, h, roll, pitch, yaw, focal)
    rng = np.random.default_rng(seed)
    if clutter:
        img = synth.add_clutter(img, clutter, rng)
    if noise:
        img = img + rng.normal(0.0, noise, img.shape)
    synth.write_pfm(path, img)
    return f, R


def run_cli(cli, *args):
    out = subprocess.run([cli] + list(args), capture_output=True, text=True, check=True).stdout
    res = {}
    for line in out.splitlines():
        k, _, v = line.partition(' ')
        res[k] = v
    if 'matrix' in res:
        res['matrix'] = np.array([float(x) for x in res['matrix'].split()]).reshape(3, 3)
    return res


def project(P, R, f, w, h):
    """world points (N x 3) to pixel coordinates (corner based), and
    whether they are in front of the camera"""
    c = P @ R            # (R^T P) for row vectors
    front = c[:, 2] > 1e-9
    z = np.where(front, c[:, 2], 1.0)
    x = f * c[:, 0] / z + 0.5 * w
    y = 0.5 * h - f * c[:, 1] / z
    return x, y, front


def facade_lines():
    """the edges of synth.facade as (kind, points on the facade plane):
    pilaster boundaries and window jambs (vertical, between street and
    roof), window heads and sills, the roof line and the street line"""
    lines = []
    v = np.linspace(-9.0, 9.0, 721)
    for k in range(-40, 40):
        for u0 in (2.0 * k, 2.0 * k + 0.5, 2.0 * k + 1.5):
            lines.append(('v', np.stack([np.full_like(v, u0), v], axis=1)))
    u = np.linspace(-80.0, 80.0, 3201)
    for v0 in [-9.0, 9.0] + [3.0 * k + d for k in range(-3, 3) for d in (0.8, 2.4)]:
        if -9.0 <= v0 <= 9.0:
            lines.append(('h', np.stack([u, np.full_like(u, v0)], axis=1)))
    return lines


LINES = facade_lines()


def straightness(H, R, f, w, h, distance=20.0):
    """largest deviation in degrees of the visible vertical and horizontal
    facade edges after the matrix H, and how many of each were measured"""
    dev = {'v': 0.0, 'h': 0.0}
    n = {'v': 0, 'h': 0}
    for kind, uv in LINES:
        P = np.stack([uv[:, 0], uv[:, 1], np.full(len(uv), distance)], axis=1)
        x, y, front = project(P, R, f, w, h)
        inside = front & (x >= 0) & (x <= w) & (y >= 0) & (y <= h)
        if inside.sum() < 2:
            continue
        xs, ys = x[inside], y[inside]
        if math.hypot(xs[-1] - xs[0], ys[-1] - ys[0]) < 0.15 * min(w, h):
            continue
        a = H @ np.array([xs[0], ys[0], 1.0])
        b = H @ np.array([xs[-1], ys[-1], 1.0])
        dx = b[0] / b[2] - a[0] / a[2]
        dy = b[1] / b[2] - a[1] / a[2]
        d = math.degrees(math.atan2(abs(dx), abs(dy)) if kind == 'v' else math.atan2(abs(dy), abs(dx)))
        dev[kind] = max(dev[kind], d)
        n[kind] += 1
    return dev, n


def fan(w, h):
    """alternating dark and light sectors 5 degrees wide above the point
    (w/2, 0.6 h), within 25 degrees of vertical (4x4 supersampled): every
    edge passes through that point, a vanishing point inside the frame"""
    ss = 4
    ys, xs = np.mgrid[0:h * ss, 0:w * ss]
    ys = (ys + 0.5) / ss
    xs = (xs + 0.5) / ss
    cx, cy = 0.5 * w, 0.6 * h
    a = np.degrees(np.arctan2(xs - cx, cy - ys))
    dark = (ys < cy) & (np.abs(a) < 25) & (np.floor((a + 25) / 5) % 2 == 0)
    img = np.where(dark, 0.2, 0.8)
    return img.reshape(h, ss, w, ss).mean(axis=(1, 3))


def case(cli, out, name, mode, w, h, focal_arg=None, **kw):
    path = os.path.join(out, 'rec-%s.pfm' % name)
    f, R = render(path, w, h, **kw)
    args = ['fit', '--mode=' + mode, path]
    if focal_arg:
        args.insert(2, '--focal=%g' % focal_arg)
    r = run_cli(cli, *args)
    before, nl = straightness(np.eye(3), R, f, w, h)
    after, _ = straightness(r['matrix'], R, f, w, h)
    print('  %-24s %-10s %-8s used %-10s v %3s h %3s  before v %6.3f h %6.3f  after v %6.3f h %6.3f  '
          'rot %+.4f sv %+.4f sh %+.4f shear %+.4f'
          % (name, mode, r['status'], r['mode'], r['vertical'], r['horizontal'],
             before['v'], before['h'], after['v'], after['h'], float(r['rotation']),
             float(r['shift-v']), float(r['shift-h']), float(r['shear'])), flush=True)
    return r, before, after, nl


def main():
    cli, out = sys.argv[1], sys.argv[2]
    quick = '--quick' in sys.argv
    os.makedirs(out, exist_ok=True)
    W, H = (480, 360)
    worst = {}

    def track(key, value):
        worst[key] = max(worst.get(key, 0.0), value)

    print('== Vertical mode, clean (target below 0.1 degrees on the verticals)')
    for pitch in ((-20, 12) if quick else (-20, -10, 6, 12, 20)):
        for roll in ((0, 3) if quick else (-5, 0, 3)):
            for focal in ((28,) if quick else (18, 28, 50)):
                name = 'v-p%+d-r%+d-f%d' % (pitch, roll, focal)
                r, b, a, n = case(cli, out, name, 'vertical', W, H, pitch=pitch, roll=roll, focal=focal)
                track('vertical clean', a['v'] if r['status'] == 'ok' else 99)
    print('== Full mode, clean (target below 0.1 degrees on all edges)')
    for pitch, yaw, roll in ((10, -15, 2), (-12, 20, -3)) if quick else \
            ((10, -15, 2), (-12, 20, -3), (0, 12, 0), (15, 8, 1), (-20, -20, 4), (5, 18, -5)):
        name = 'f-p%+d-y%+d-r%+d' % (pitch, yaw, roll)
        r, b, a, n = case(cli, out, name, 'full', W, H, pitch=pitch, yaw=yaw, roll=roll)
        track('full clean', max(a['v'], a['h']) if r['status'] == 'ok' else 99)
    print('== odd sizes, clean')
    for (w, h, mode, pitch, yaw) in ((333, 517, 'vertical', 14, 0), (517, 333, 'full', -8, 11),
                                    (401, 401, 'full', 9, -9)):
        name = 'odd-%dx%d-%s' % (w, h, mode)
        r, b, a, n = case(cli, out, name, mode, w, h, pitch=pitch, yaw=yaw, roll=1.5)
        track('%s clean' % mode, (a['v'] if mode == 'vertical' else max(a['v'], a['h']))
              if r['status'] == 'ok' else 99)
    print('== noise and clutter (target below 0.3 degrees)')
    for i, (mode, pitch, yaw, roll) in enumerate((('vertical', 12, 0, 2), ('vertical', -16, 0, -3),
                                                 ('full', 10, -12, 1), ('full', -8, 15, -2))):
        name = 'noisy-%s-%d' % (mode, i)
        r, b, a, n = case(cli, out, name, mode, W, H, pitch=pitch, yaw=yaw, roll=roll,
                          noise=0.03, clutter=6, seed=10 + i)
        track('%s noisy' % mode, (a['v'] if mode == 'vertical' else max(a['v'], a['h']))
              if r['status'] == 'ok' else 99)
    print('== rotation (pitch and yaw zero: rotation = roll within 0.05 degrees)')
    rot_err = 0.0
    for roll in (-5, -2, 0.5, 3, 5):
        for mode in ('rotation', 'vertical'):
            name = 'rot-%+g-%s' % (roll, mode)
            r, b, a, n = case(cli, out, name, mode, W, H, roll=roll)
            rot_err = max(rot_err, abs(float(r['rotation']) - roll) if r['status'] == 'ok' else 99)
    print('== automatic mode')
    auto = {}
    for name, kw in (('pitch', dict(pitch=14)), ('yaw', dict(yaw=16)), ('both', dict(pitch=10, yaw=-14)),
                     ('roll', dict(roll=3)), ('pitch-noisy', dict(pitch=-12, noise=0.03, clutter=6))):
        r, b, a, n = case(cli, out, 'auto-' + name, 'auto', W, H, **kw)
        auto[name] = (r, a)

    print('== failure cases')
    blank = os.path.join(out, 'rec-blank.pfm')
    synth.write_pfm(blank, np.full((240, 320), 0.5))
    rb = run_cli(cli, 'fit', '--mode=full', blank)
    noise = os.path.join(out, 'rec-noise.pfm')
    synth.write_pfm(noise, np.random.default_rng(5).uniform(0, 1, (240, 320)))
    rn = run_cli(cli, 'fit', '--mode=vertical', noise)
    single = os.path.join(out, 'rec-single.pfm')
    img = np.full((240, 320), 0.2)
    img[:, 150:] = 0.8
    synth.write_pfm(single, img)
    rs = run_cli(cli, 'fit', '--mode=vertical', single)
    # a fan of near vertical rays from a point inside the frame: their
    # vanishing point is inside, which no correction can reach
    inside = os.path.join(out, 'rec-vp-inside.pfm')
    synth.write_pfm(inside, fan(W, H))
    ri = run_cli(cli, 'fit', '--mode=vertical', inside)

    print()
    print('== results')
    check(worst['vertical clean'] < 0.1,
          'Vertical, clean: largest vertical deviation %.4f degrees (target < 0.1)' % worst['vertical clean'])
    check(worst['full clean'] < 0.1,
          'Full, clean: largest deviation of any edge %.4f degrees (target < 0.1)' % worst['full clean'])
    check(worst['vertical noisy'] < 0.3,
          'Vertical, noise and clutter: %.4f degrees (target < 0.3)' % worst['vertical noisy'])
    check(worst['full noisy'] < 0.3,
          'Full, noise and clutter: %.4f degrees (target < 0.3)' % worst['full noisy'])
    check(rot_err < 0.05, 'rotation equals the roll: largest error %.4f degrees (target < 0.05)' % rot_err)
    check(auto['pitch'][0]['mode'] == 'vertical', 'auto on converging verticals uses vertical (got %s)'
          % auto['pitch'][0]['mode'])
    check(auto['both'][0]['mode'] == 'full', 'auto on pitch and yaw uses full (got %s)' % auto['both'][0]['mode'])
    check(auto['yaw'][0]['mode'] == 'full', 'auto on converging horizontals uses full (got %s)'
          % auto['yaw'][0]['mode'])
    check(all(a[1]['v'] < 0.3 for a in auto.values()),
          'auto: the verticals end up straight in every case (largest %.4f degrees)'
          % max(a[1]['v'] for a in auto.values()))
    for what, r in (('a blank image', rb), ('noise only', rn), ('a single edge', rs),
                    ('a vanishing point inside the frame', ri)):
        check(r['status'] == 'not-enough-lines' and np.allclose(r['matrix'], np.eye(3)),
              '%s: %s, identity (lines %s, vertical %s, horizontal %s)'
              % (what, r['status'], r['lines'], r['vertical'], r['horizontal']))
    print('recovery: %d failed' % failures)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
