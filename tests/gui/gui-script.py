# Runs inside GIMP on a Broadway display (tests/gui/start.sh). Opens the
# synthetic facade of tests/synth.py (GUI_IN/facade.pfm) and:
#
#   GUI_MODE=menu    runs nothing: the test opens Image > Transform
#   GUI_MODE=dialog  runs Image > Transform > Auto Perspective... with its dialog,
#                    as from the menu
#
# When the dialog is closed (or the test has looked at the menu), writes
# the status, the image size, the layers and a checksum of the pixels
# before and after to GUI_OUT/result.txt, waits for GUI_OUT/done (the
# test takes its screenshots meanwhile) and quits GIMP.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import os
import time

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gegl, Gimp  # noqa: E402

out = os.environ['GUI_OUT']
inp = os.environ['GUI_IN']
mode = os.environ.get('GUI_MODE', 'dialog')
pdb = Gimp.get_pdb()


def write(name, lines):
    with open(os.path.join(out, name + '.tmp'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    os.rename(os.path.join(out, name + '.tmp'), os.path.join(out, name))


def wait(name, seconds=300):
    t = time.time()
    while not os.path.exists(os.path.join(out, name)) and time.time() - t < seconds:
        time.sleep(0.5)


def open_facade():
    with open(os.path.join(inp, 'facade.pfm'), 'rb') as f:
        f.readline()
        w, h = map(int, f.readline().split())
        f.readline()
        data = f.read(w * h * 4)
    row = w * 4
    top = b''.join(data[(h - 1 - y) * row:(h - y) * row] for y in range(h))
    image = Gimp.Image.new_with_precision(w, h, Gimp.ImageBaseType.RGB, Gimp.Precision.U8_NON_LINEAR)
    layer = Gimp.Layer.new(image, 'facade', w, h, Gimp.ImageType.RGB_IMAGE, 100.0,
                           Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    buf = layer.get_buffer()
    buf.set(Gegl.Rectangle.new(0, 0, w, h), 'Y float', top)
    buf.flush()
    image.clean_all()
    return image


def checksum(image):
    s = hashlib.sha256()
    for layer in image.get_layers():
        buf = layer.get_buffer()
        ext = buf.get_extent()
        s.update(b'%d %d %d %d' % (ext.x, ext.y, ext.width, ext.height))
        s.update(buf.get(ext, 1.0, "R'G'B'A u8", Gegl.AbyssPolicy.NONE))
    return s.hexdigest()


image = open_facade()
display = Gimp.Display.new(image)
Gimp.displays_flush()
before = checksum(image)
write('ready', ['ready'])

if mode == 'menu':
    wait('check')
    write('result.txt', ['layers %d' % len(image.get_layers())])
else:
    proc = pdb.lookup_procedure('plug-in-auto-perspective')
    time.sleep(2)
    config = proc.create_config()
    config.set_property('run-mode', Gimp.RunMode.INTERACTIVE)
    config.set_property('image', image)
    config.set_core_object_array('drawables', image.get_selected_drawables())
    result = proc.run(config)
    Gimp.displays_flush()
    write('result.txt', ['status %s' % result.index(0).value_nick,
                         'layers %d' % len(image.get_layers()),
                         'size %dx%d' % (image.get_width(), image.get_height()),
                         'unchanged %s' % ('yes' if checksum(image) == before else 'no')])

wait('done')
quit_proc = pdb.lookup_procedure('gimp-quit')
qc = quit_proc.create_config()
qc.set_property('force', True)
quit_proc.run(qc)
