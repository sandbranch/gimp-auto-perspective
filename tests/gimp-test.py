# Runs inside GIMP 3 without a window (tests/gimp-test.sh starts it):
# checks that Image > Transform > Auto Perspective... is registered as planned and,
# while it is a skeleton, that it leaves every image exactly as it was:
# every mode, crop and apply-to setting, on 8 and 16 bit and floating
# point, RGB and gray, with and without alpha, a layer with offsets, and
# the guided mode with a path. Prints "UT PASS ..." or "UT FAIL ...".
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import os

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gegl, Gimp  # noqa: E402

inp = os.environ['UT_IN']
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
    with open(path, 'rb') as f:
        kind = f.readline().strip()
        w, h = map(int, f.readline().split())
        f.readline()
        c = 3 if kind == b'PF' else 1
        data = f.read(w * h * c * 4)
    row = w * c * 4
    return w, h, b''.join(data[(h - 1 - y) * row:(h - y) * row] for y in range(h))


def make_image(precision, gray=False, alpha=False, offsets=None):
    """the synthetic facade as one layer; offsets: a smaller second layer
    on top, moved, which is the one selected"""
    w, h, data = read_pfm(os.path.join(inp, 'facade.pfm'))
    base = Gimp.ImageBaseType.GRAY if gray else Gimp.ImageBaseType.RGB
    image = Gimp.Image.new_with_precision(w, h, base, precision)
    if gray:
        itype = Gimp.ImageType.GRAYA_IMAGE if alpha else Gimp.ImageType.GRAY_IMAGE
    else:
        itype = Gimp.ImageType.RGBA_IMAGE if alpha else Gimp.ImageType.RGB_IMAGE
    layer = Gimp.Layer.new(image, 'facade', w, h, itype, 100.0, Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    buf = layer.get_buffer()
    buf.set(Gegl.Rectangle.new(0, 0, w, h), 'Y float', data)
    buf.flush()
    if offsets:
        top = Gimp.Layer.new(image, 'part', w // 2, h // 2, itype, 100.0, Gimp.LayerMode.NORMAL)
        image.insert_layer(top, None, 0)
        tb = top.get_buffer()
        row = w * 4
        part = b''.join(data[y * row:y * row + (w // 2) * 4] for y in range(h // 2))
        tb.set(Gegl.Rectangle.new(0, 0, w // 2, h // 2), 'Y float', part)
        tb.flush()
        top.set_offsets(*offsets)
        image.set_selected_layers([top])
    image.clean_all()
    return image


def state(image):
    """everything the skeleton must leave as it is: size, layers, their
    offsets and sizes, and every pixel in the layer's own format"""
    s = [image.get_width(), image.get_height(), image.get_precision()]
    for layer in image.get_layers():
        buf = layer.get_buffer()
        ext = buf.get_extent()
        s += [layer.get_name(), layer.get_offsets(), ext.width, ext.height,
              # (float holds 8 and 16 bit values exactly)
              buf.get(ext, 1.0, 'YA float' if layer.is_gray() else 'RGBA float',
                      Gegl.AbyssPolicy.NONE)]
    return s


def run(image, **args):
    proc = pdb.lookup_procedure(PROC)
    config = proc.create_config()
    config.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    config.set_property('image', image)
    config.set_core_object_array('drawables', image.get_selected_drawables())
    for k, v in args.items():
        config.set_property(k, v)
    return proc.run(config)


def tests():
    # ---- registration
    proc = pdb.lookup_procedure(PROC)
    check(proc is not None, 'the procedure %s exists' % PROC)
    if proc is not None:
        check(proc.get_menu_paths() == ['<Image>/Image/Transform'],
              'menu path <Image>/Image/Transform (got %s)' % proc.get_menu_paths())
        check(proc.get_menu_label() == '_Auto Perspective...', 'menu label _Auto Perspective... (got %s)' % proc.get_menu_label())
        names = [a.name for a in proc.get_arguments()]
        check(names[3:] == ['mode', 'path', 'crop', 'apply-to', 'interpolation'],
              'arguments (got %s)' % names)
        check([a.name for a in proc.get_return_values()] == ['status', 'rotation', 'shift-v', 'shift-h', 'shear'],
              'return values')
        config = proc.create_config()
        mode_spec = config.find_property('mode')
        choices = Gimp.param_spec_choice_get_choice(mode_spec).list_nicks()
        check(choices == ['auto', 'vertical', 'horizontal', 'full', 'rotation', 'guided'],
              'modes auto, vertical, horizontal, full, rotation, guided (got %s)' % choices)

    # ---- the skeleton leaves the image as it was
    cases = [('rgb8', P.U8_NON_LINEAR, False, False, None),
             ('rgba16', P.U16_NON_LINEAR, False, True, None),
             ('grayf', P.FLOAT_LINEAR, True, False, None),
             ('rgb8-offsets', P.U8_NON_LINEAR, False, True, (17, 9))]
    for name, prec, gray, alpha, offsets in cases:
        for mode in ('auto', 'vertical', 'horizontal', 'full', 'rotation'):
            for crop, apply_to in (('largest', 'image'), ('none', 'layer'), ('aspect', 'image')):
                image = make_image(prec, gray, alpha, offsets)
                before = state(image)
                r = run(image, mode=mode, crop=crop, **{'apply-to': apply_to})
                what = '%s %s crop=%s apply-to=%s' % (name, mode, crop, apply_to)
                ok = r.index(0) == Gimp.PDBStatusType.SUCCESS
                check(ok, '%s: success' % what)
                if ok:
                    # (the fit runs; applying it is the next step)
                    check(r.index(1) in ('ok', 'not-enough-lines'),
                          '%s: status %s' % (what, r.index(1)))
                check(state(image) == before, '%s: the image is unchanged' % what)
                check(not image.is_dirty(), '%s: the image is not dirty' % what)
                image.delete()

    # ---- the guided mode
    image = make_image(P.U8_NON_LINEAR)
    before = state(image)
    r = run(image, mode='guided')
    check(r.index(0) == Gimp.PDBStatusType.CALLING_ERROR, 'guided without a path: a calling error')
    path = Gimp.Path.new(image, 'guides')
    image.insert_path(path, None, 0)
    for x1, y1, x2, y2 in ((40, 20, 44, 200), (260, 25, 255, 205), (20, 120, 300, 118)):
        # a straight Bezier stroke: control, anchor, control at each end
        path.bezier_stroke_new_moveto(x1, y1)
        sid = path.get_strokes()[-1]
        path.bezier_stroke_lineto(sid, x2, y2)
    r = run(image, mode='guided', path=path)
    check(r.index(0) == Gimp.PDBStatusType.SUCCESS and r.index(1) == 'ok',
          'guided with 3 strokes (2 vertical, 1 horizontal): success, status ok (got %s %s)'
          % (r.index(0).value_nick, r.index(1) if r.index(0) == Gimp.PDBStatusType.SUCCESS else ''))
    check(state(image) == before, 'guided: the image is unchanged')
    image.delete()


try:
    tests()
except Exception:
    import traceback
    traceback.print_exc()
    check(False, 'the tests ended with a Python error')
print('UT failures: %d' % failures, flush=True)
pdb.lookup_procedure('gimp-quit').run(None)
