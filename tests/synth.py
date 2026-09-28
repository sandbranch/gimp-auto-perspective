#!/usr/bin/env python3
"""Synthetic test images: a building facade (a grid of windows on a wall,
under a sky) seen by a pinhole camera that is turned by known angles.

    tests/synth.py [--size=W,H] [--roll=DEG] [--pitch=DEG] [--yaw=DEG]
                   [--focal=MM] [--truth=FILE] out.pfm

The facade is a plane facing the camera; with all angles zero its
vertical edges are vertical and its horizontal edges horizontal in the
image. --roll turns the camera about its axis (a tilted horizon),
--pitch tilts it up (positive) or down (converging verticals), --yaw
turns it right (positive) or left (converging horizontals). --focal is
the focal length in mm on a 36 mm wide sensor (28 mm, as ashift assumes
without EXIF data). --truth writes the angles and the camera rotation,
for the recovery tests of PLAN.md.

The picture is rendered by casting a ray through every pixel (4x4
samples) onto the facade, so it does not depend on the core's own
homography: it is an independent truth.

Copyright 2026 David
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import math

import numpy as np


def rot(roll, pitch, yaw):
    # (pitch negated: positive looks up)
    r, p, y = math.radians(roll), -math.radians(pitch), math.radians(yaw)
    rz = np.array([[math.cos(r), -math.sin(r), 0], [math.sin(r), math.cos(r), 0], [0, 0, 1]])
    rx = np.array([[1, 0, 0], [0, math.cos(p), -math.sin(p)], [0, math.sin(p), math.cos(p)]])
    ry = np.array([[math.cos(y), 0, math.sin(y)], [0, 1, 0], [-math.sin(y), 0, math.cos(y)]])
    return ry @ rx @ rz


def facade(u, v):
    """gray value of the facade plane at (u, v), in meters from its
    center, v up: wall, windows in a grid, sky above the roof line"""
    wall = 0.55
    col = (np.floor(u / 2.0) % 2 == 0)
    fu = np.mod(u, 2.0)
    fv = np.mod(v, 3.0)
    window = (fu > 0.5) & (fu < 1.5) & (fv > 0.8) & (fv < 2.4)
    g = np.where(window, 0.15, np.where(col, wall, wall + 0.05))
    g = np.where(v > 9.0, 0.85, g)          # sky
    g = np.where(v < -9.0, 0.35, g)         # street
    return g


def render(w, h, roll, pitch, yaw, focal_mm, distance=20.0, ss=4):
    f = focal_mm / 36.0 * w                  # focal length in pixels
    R = rot(roll, pitch, yaw)
    out = np.zeros((h, w))
    for sy in range(ss):
        for sx in range(ss):
            x = np.arange(w) + (sx + 0.5) / ss
            y = np.arange(h) + (sy + 0.5) / ss
            X, Y = np.meshgrid(x - 0.5 * w, 0.5 * h - y)
            d = np.stack([X, Y, np.full_like(X, f)], axis=-1) @ R.T
            t = distance / np.where(d[..., 2] > 1e-9, d[..., 2], np.nan)
            u, v = d[..., 0] * t, d[..., 1] * t
            g = facade(u, v)
            out += np.where(np.isfinite(g), g, 0.85)
    return out / (ss * ss), f, R


def write_pfm(path, img):
    h, w = img.shape
    with open(path, 'wb') as fp:
        fp.write(b'Pf\n%d %d\n-1.0\n' % (w, h))
        fp.write(np.ascontiguousarray(img[::-1], dtype='<f4').tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--size', default='320,240')
    ap.add_argument('--roll', type=float, default=0.0)
    ap.add_argument('--pitch', type=float, default=0.0)
    ap.add_argument('--yaw', type=float, default=0.0)
    ap.add_argument('--focal', type=float, default=28.0)
    ap.add_argument('--truth')
    ap.add_argument('out')
    a = ap.parse_args()
    w, h = (int(s) for s in a.size.split(','))
    img, f, R = render(w, h, a.roll, a.pitch, a.yaw, a.focal)
    write_pfm(a.out, img)
    if a.truth:
        with open(a.truth, 'w') as fp:
            fp.write('size %d %d\nroll %.9g\npitch %.9g\nyaw %.9g\nfocal-px %.9g\n'
                     % (w, h, a.roll, a.pitch, a.yaw, f))
            fp.write('R ' + ' '.join('%.12g' % c for c in R.flatten()) + '\n')


if __name__ == '__main__':
    main()
