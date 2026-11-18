# Runs inside GIMP 3 without a window (tests/gimp-test.sh starts it):
# checks that Image > Transform > Auto Perspective... is registered as
# planned and that it corrects (PLAN.md 5.4): the same parameters as
# auto-perspective-cli on the same pixels, the same picture as
# auto-perspective-cli warp (within the difference of the two
# interpolations), the facade's true vertical edges drawn as a path come
# out vertical, channels and the selection follow, the canvas or only the
# layer is cropped; every mode, crop and apply-to setting on 8 and 16 bit
# and floating point, RGB and gray, alpha, offsets, several layers; a
# blank image and indexed images are left alone. Prints "UT PASS ..." or
# "UT FAIL ...". (Undo cannot be driven from here: GIMP has no undo
# procedure; the GUI test does that.)
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import array
import math
import os

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gegl, Gimp  # noqa: E402

inp = os.environ['UT_IN']
outd = os.environ['UT_OUT']
pdb = Gimp.get_pdb()
failures = 0
P = Gimp.Precision
PROC = 'plug-in-auto-perspective'


def check(ok, what):
    global failures
    print('UT %s %s' % ('PASS' if ok else 'FAIL', what), flush=True)
    if not ok:
        failures += 1


def read_pfm(path):
    """width, height and the rows top first, as bytes of floats"""
    with open(path, 'rb') as f:
        kind = f.readline().strip()
        w, h = map(int, f.readline().split())
        f.readline()
        c = 3 if kind == b'PF' else 1
        data = f.read(w * h * c * 4)
    row = w * c * 4
    return w, h, b''.join(data[(h - 1 - y) * row:(h - y) * row] for y in range(h))


def write_pfm(path, w, h, data):
    row = w * 4
    with open(path, 'wb') as f:
        f.write(b'Pf\n%d %d\n-1.0\n' % (w, h))
        f.write(b''.join(data[(h - 1 - y) * row:(h - y) * row] for y in range(h)))


def read_kv(path):
    return dict(line.split(' ', 1) for line in open(path).read().splitlines())


def make_image(precision, gray=False, alpha=False, offsets=None, layers=1, fmt="Y' float", data=None):
    """the synthetic facade as one layer (its pixels set as perceptual
    gray, which the plug-in reads back exactly in a non-linear float
    image); offsets: a smaller second layer on top, moved, which is the
    one selected; layers: more full size copies below"""
    if data is None:
        w, h, data = read_pfm(os.path.join(inp, 'facade.pfm'))
    else:
        w, h, data = data
    base = Gimp.ImageBaseType.GRAY if gray else Gimp.ImageBaseType.RGB
    image = Gimp.Image.new_with_precision(w, h, base, precision)
    if gray:
        itype = Gimp.ImageType.GRAYA_IMAGE if alpha else Gimp.ImageType.GRAY_IMAGE
    else:
        itype = Gimp.ImageType.RGBA_IMAGE if alpha else Gimp.ImageType.RGB_IMAGE
    for i in range(layers):
        layer = Gimp.Layer.new(image, 'facade %d' % i, w, h, itype, 100.0, Gimp.LayerMode.NORMAL)
        image.insert_layer(layer, None, i)
        buf = layer.get_buffer()
        buf.set(Gegl.Rectangle.new(0, 0, w, h), fmt, data)
        buf.flush()
    image.set_selected_layers([image.get_layers()[0]])
    if offsets:
        top = Gimp.Layer.new(image, 'part', w // 2, h // 2, itype, 100.0, Gimp.LayerMode.NORMAL)
        image.insert_layer(top, None, 0)
        tb = top.get_buffer()
        row = w * 4
        part = b''.join(data[y * row:y * row + (w // 2) * 4] for y in range(h // 2))
        tb.set(Gegl.Rectangle.new(0, 0, w // 2, h // 2), fmt, part)
        tb.flush()
        top.set_offsets(*offsets)
        image.set_selected_layers([top])
    image.clean_all()
    return image


def pixels(layer, fmt=None):
    buf = layer.get_buffer()
    ext = buf.get_extent()
    if fmt is None:
        fmt = 'YA float' if layer.is_gray() else 'RGBA float'
    return buf.get(ext, 1.0, fmt, Gegl.AbyssPolicy.NONE)


def state(image):
    """size, layers, their offsets and sizes, and every pixel"""
    s = [image.get_width(), image.get_height(), image.get_precision()]
    for layer in image.get_layers():
        buf = layer.get_buffer()
        ext = buf.get_extent()
        s += [layer.get_name(), layer.get_offsets(), ext.width, ext.height, pixels(layer)]
    return s


def run(image, **args):
    proc = pdb.lookup_procedure(PROC)
    config = proc.create_config()
    config.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    config.set_property('image', image)
    config.set_core_object_array('drawables', image.get_selected_drawables())
    for k, v in args.items():
        config.set_property(k.replace('_', '-'), v)
    return proc.run(config)


def edges():
    return [tuple(map(float, line.split())) for line in open(os.path.join(inp, 'edges.txt'))]


def add_strokes(image, segs, name='guides'):
    path = Gimp.Path.new(image, name)
    image.insert_path(path, None, 0)
    for x1, y1, x2, y2 in segs:
        # a straight Bezier stroke: control, anchor, control at each end
        path.bezier_stroke_new_moveto(x1, y1)
        sid = path.get_strokes()[-1]
        path.bezier_stroke_lineto(sid, x2, y2)
    return path


def stroke_ends(path):
    out = []
    for sid in path.get_strokes():
        _, pts, closed = path.stroke_get_points(sid)
        out.append((pts[2], pts[3], pts[-4], pts[-3]))
    return out


def tests():
    # ---- registration
    proc = pdb.lookup_procedure(PROC)
    check(proc is not None, 'the procedure %s exists' % PROC)
    if proc is not None:
        check(proc.get_menu_paths() == ['<Image>/Image/Transform'],
              'menu path <Image>/Image/Transform (got %s)' % proc.get_menu_paths())
        check(proc.get_menu_label() == '_Auto Perspective...',
              'menu label _Auto Perspective... (got %s)' % proc.get_menu_label())
        check(proc.get_image_types() == 'RGB*, GRAY*', 'image types RGB*, GRAY* (got %s)' % proc.get_image_types())
        names = [a.name for a in proc.get_arguments()]
        check(names[3:] == ['mode', 'path', 'crop', 'apply-to', 'interpolation', 'focal-length',
                            'lens-dependence'], 'arguments (got %s)' % names)
        rnames = [a.name for a in proc.get_return_values()]
        check(rnames == ['status', 'rotation', 'shift-v', 'shift-h', 'shear', 'mode-used'],
              'return values (got %s)' % rnames)
        config = proc.create_config()
        mode_spec = config.find_property('mode')
        choices = Gimp.param_spec_choice_get_choice(mode_spec).list_nicks()
        check(choices == ['auto', 'vertical', 'horizontal', 'full', 'rotation', 'guided'],
              'modes auto, vertical, horizontal, full, rotation, guided (got %s)' % choices)

    # ---- the same parameters as auto-perspective-cli, the same picture
    for mode in ('auto', 'vertical', 'full'):
        cli = read_kv(os.path.join(inp, 'cli-fit-%s.txt' % mode))
        image = make_image(P.FLOAT_NON_LINEAR, gray=True)
        r = run(image, mode=mode, crop='largest', interpolation=Gimp.InterpolationType.LINEAR)
        ok = r.index(0) == Gimp.PDBStatusType.SUCCESS
        check(ok, 'parity %s: success' % mode)
        if not ok:
            continue
        got = [r.index(i) for i in range(2, 6)]
        want = [float(cli[k]) for k in ('rotation', 'shift-v', 'shift-h', 'shear')]
        check(r.index(1) == cli['status'] and r.index(6) == cli['mode'] and
              all(abs(a - b) < 5e-9 for a, b in zip(got, want)),
              'parity %s: status %s, mode %s, parameters %s equal auto-perspective-cli\'s %s (%s)'
              % (mode, r.index(1), r.index(6), ['%.9f' % v for v in got], ['%.9f' % v for v in want],
                 cli['mode']))
        if mode == 'vertical':
            # against auto-perspective-cli warp: the canvas is the crop, the
            # pixels agree within the difference of the interpolations
            crop = [int(v) for v in read_kv(os.path.join(inp, 'cli-warp.txt'))['crop'].split()]
            cw, ch, cdata = read_pfm(os.path.join(inp, 'cli-warp.pfm'))
            check([image.get_width(), image.get_height()] == crop[2:] == [cw, ch],
                  'parity vertical: the canvas %dx%d is the crop of auto-perspective-cli warp %s'
                  % (image.get_width(), image.get_height(), crop))
            layer = image.get_layers()[0]
            check(layer.get_offsets()[1:] == (0, 0) and layer.get_width() == cw and layer.get_height() == ch,
                  'parity vertical: the layer is cropped with the canvas (%s, %dx%d)'
                  % (layer.get_offsets()[1:], layer.get_width(), layer.get_height()))
            g = array.array('f', pixels(layer, "Y' float"))
            c = array.array('f', cdata)
            if len(g) == len(c):
                d = sorted(abs(a - b) for a, b in zip(g, c))
                mean = sum(d) / len(d)
                p99 = d[int(0.99 * (len(d) - 1))]
                print('UT     GIMP against auto-perspective-cli warp: mean difference %.5f, 99th percentile '
                      '%.5f, largest %.5f' % (mean, p99, d[-1]))
                check(mean < 0.01 and p99 < 0.1,
                      'parity vertical: the pixels agree with auto-perspective-cli warp (mean %.4f < 0.01, '
                      '99th percentile %.4f < 0.1)' % (mean, p99))
                write_pfm(os.path.join(outd, 'gimp-vertical.pfm'), cw, ch, pixels(layer, "Y' float"))
            else:
                check(False, 'parity vertical: sizes differ, no pixel comparison')
        image.delete()

    # ---- every format, mode, crop and apply-to: corrected, consistent
    cases = [('rgb8', P.U8_NON_LINEAR, False, False, None),
             ('rgba16', P.U16_NON_LINEAR, False, True, None),
             ('grayf', P.FLOAT_LINEAR, True, False, None),
             ('rgb8-offsets', P.U8_NON_LINEAR, False, True, (17, 9))]
    for name, prec, gray, alpha, offsets in cases:
        for mode in ('auto', 'vertical', 'horizontal', 'full', 'rotation'):
            for crop, apply_to in (('largest', 'image'), ('none', 'layer'), ('aspect', 'image'),
                                   ('none', 'image'), ('largest', 'layer')):
                image = make_image(prec, gray, alpha, offsets)
                before = state(image)
                nlayers = len(image.get_layers())
                r = run(image, mode=mode, crop=crop, apply_to=apply_to)
                what = '%s %s crop=%s apply-to=%s' % (name, mode, crop, apply_to)
                ok = r.index(0) == Gimp.PDBStatusType.SUCCESS
                if not ok:
                    check(False, '%s: success' % what)
                    image.delete()
                    continue
                status = r.index(1)
                # horizontal: the facade has converging verticals and level
                # horizontals: either it fits (rotation only) or too few
                if status == 'ok':
                    changed = state(image) != before
                    neutral = all(r.index(i) == 0.0 for i in range(2, 6))
                    problems = []
                    if not (changed or neutral):
                        problems.append('unchanged')
                    if image.get_floating_sel() is not None:
                        problems.append('a floating selection')
                    if len(image.get_layers()) != nlayers:
                        problems.append('layers %d, was %d' % (len(image.get_layers()), nlayers))
                    if apply_to == 'layer' and [image.get_width(), image.get_height()] != before[:2]:
                        problems.append('the canvas changed')
                    if apply_to == 'image' and crop != 'none' and changed:
                        for layer in image.get_layers():
                            ox, oy = layer.get_offsets()[1:]
                            if ox < 0 or oy < 0 or ox + layer.get_width() > image.get_width() or \
                                    oy + layer.get_height() > image.get_height():
                                problems.append('a layer outside the cropped canvas')
                    if apply_to == 'layer' and crop == 'largest' and changed:
                        sel = image.get_selected_layers()[0]
                        if sel.has_alpha() and sel.get_width() > before[3 + 5 * 0 + 2] + 2:
                            problems.append('the layer is not cropped')
                    check(not problems and image.is_dirty() == changed,
                          '%s: ok, mode %s, %s%s' % (what, r.index(6), 'changed' if changed else 'neutral',
                                                    (': ' + ', '.join(problems)) if problems else ''))
                else:
                    check(status == 'not-enough-lines' and state(image) == before and not image.is_dirty(),
                          '%s: %s, the image is unchanged and not dirty' % (what, status))
                image.delete()

    # ---- apply to the layer: the other layers stay as they are
    image = make_image(P.U8_NON_LINEAR, layers=2)
    other = image.get_layers()[1]
    other_before = pixels(other)
    w0, h0 = image.get_width(), image.get_height()
    r = run(image, mode='vertical', crop='largest', apply_to='layer')
    top = image.get_layers()[0]
    check(r.index(1) == 'ok' and pixels(other) == other_before and [image.get_width(), image.get_height()] == [w0, h0]
          and (top.get_width(), top.get_height()) != (w0, h0),
          'apply-to=layer: the other layer and the canvas stay, the layer is %dx%d at %s'
          % (top.get_width(), top.get_height(), top.get_offsets()[1:]))
    image.delete()

    # ---- several layers, the whole image: all transformed and cropped
    image = make_image(P.U8_NON_LINEAR, layers=3)
    r = run(image, mode='vertical', crop='largest', apply_to='image')
    sizes = set((l.get_width(), l.get_height(), l.get_offsets()[1:]) for l in image.get_layers())
    check(r.index(1) == 'ok' and sizes == {(image.get_width(), image.get_height(), (0, 0))},
          'three layers: all transformed and cropped to the canvas %dx%d (%s)'
          % (image.get_width(), image.get_height(), sizes))
    image.delete()

    # ---- paths, channels and the selection follow
    image = make_image(P.U8_NON_LINEAR, alpha=True)
    path = add_strokes(image, edges()[:2], 'edges')
    chan = Gimp.Channel.new(image, 'mask', image.get_width(), image.get_height(), 50.0, Gegl.Color.new('black'))
    image.insert_channel(chan, None, 0)
    # (inserting the channel selected it; the plug-in wants the layer)
    image.set_selected_layers([image.get_layers()[0]])
    image.select_rectangle(Gimp.ChannelOps.REPLACE, 100, 60, 120, 100)
    sel_before = Gimp.Selection.bounds(image)
    nch = len(image.get_channels())
    r = run(image, mode='vertical', crop='largest', apply_to='image')
    worst = 0.0
    for x1, y1, x2, y2 in stroke_ends(path):
        worst = max(worst, math.degrees(math.atan2(abs(x2 - x1), abs(y2 - y1))))
    sel_after = Gimp.Selection.bounds(image)
    check(r.index(0) == Gimp.PDBStatusType.SUCCESS and r.index(1) == 'ok' and worst < 0.1,
          'paths follow: the facade\'s true vertical edges drawn as a path are vertical after the '
          'correction within %.4f degrees (target 0.1) (%s %s, canvas %dx%d, path ends %s)'
          % (worst, r.index(0).value_nick, r.index(1) if r.index(0) == Gimp.PDBStatusType.SUCCESS else
             '', image.get_width(), image.get_height(), stroke_ends(path)))
    check(len(image.get_channels()) == nch and image.get_floating_sel() is None,
          'channels: %d before and after, no floating selection, the saved selection removed'
          % len(image.get_channels()))
    check(sel_after[1] and sel_after[2:] != sel_before[2:],
          'the selection follows: bounds %s before, %s after' % (sel_before[2:], sel_after[2:]))
    image.delete()

    # ---- the guided mode
    image = make_image(P.U8_NON_LINEAR)
    before = state(image)
    r = run(image, mode='guided')
    check(r.index(0) == Gimp.PDBStatusType.CALLING_ERROR and state(image) == before,
          'guided without a path: a calling error, unchanged')
    path = add_strokes(image, edges())
    r = run(image, mode='guided', path=path, crop='none')
    ok = r.index(0) == Gimp.PDBStatusType.SUCCESS
    check(ok and r.index(1) == 'ok' and r.index(6) == 'vertical' and state(image) != before,
          'guided with the true edges (2 vertical, 1 horizontal): ok, vertical, changed (got %s %s %s)'
          % (r.index(0).value_nick, r.index(1) if ok else '', r.index(6) if ok else ''))
    if ok:
        worst = max(math.degrees(math.atan2(abs(x2 - x1), abs(y2 - y1)))
                    for x1, y1, x2, y2 in stroke_ends(path)[:2])
        check(worst < 0.01, 'guided: the drawn verticals are vertical afterwards within %.5f degrees' % worst)
    image.delete()
    image = make_image(P.U8_NON_LINEAR)
    path = Gimp.Path.new(image, 'rectangle')
    image.insert_path(path, None, 0)
    # a closed stroke of 4 anchors along a window bay: its 4 sides
    e = edges()
    path.bezier_stroke_new_moveto(e[0][2], e[0][3])
    sid = path.get_strokes()[-1]
    for x, y in ((e[1][2], e[1][3]), (e[1][0], e[1][1]), (e[0][0], e[0][1])):
        path.bezier_stroke_lineto(sid, x, y)
    path.stroke_close(sid)
    r = run(image, mode='guided', path=path)
    check(r.index(0) == Gimp.PDBStatusType.SUCCESS and r.index(1) == 'ok' and r.index(6) == 'full',
          'guided with a closed stroke of 4 anchors: its 4 sides, full (got %s %s)'
          % (r.index(1) if r.index(0) == Gimp.PDBStatusType.SUCCESS else r.index(0).value_nick,
             r.index(6) if r.index(0) == Gimp.PDBStatusType.SUCCESS else ''))
    image.delete()

    # ---- nothing to find: unchanged; indexed: refused
    w, h = 160, 120
    blank = (w, h, array.array('f', [0.5] * (w * h)).tobytes())
    image = make_image(P.U8_NON_LINEAR, data=blank)
    before = state(image)
    r = run(image, mode='auto')
    check(r.index(0) == Gimp.PDBStatusType.SUCCESS and r.index(1) == 'not-enough-lines' and
          state(image) == before and not image.is_dirty(),
          'a blank image: success, not-enough-lines, unchanged, not dirty')
    image.delete()
    image = make_image(P.U8_NON_LINEAR)
    image.convert_indexed(Gimp.ConvertDitherType.NONE, Gimp.ConvertPaletteType.GENERATE, 16, False, False, '')
    image.clean_all()
    before = state(image)
    r = run(image, mode='auto')
    check(r.index(0) != Gimp.PDBStatusType.SUCCESS and state(image) == before,
          'an indexed image is refused (%s) and unchanged' % r.index(0).value_nick)
    image.delete()


try:
    tests()
except Exception:
    import traceback
    traceback.print_exc()
    check(False, 'the tests ended with a Python error')
print('UT failures: %d' % failures, flush=True)
pdb.lookup_procedure('gimp-quit').run(None)
