#!/bin/sh
# Tests the plug-in in the native GIMP without a window: tests/gimp-test.py
# checks the registration (procedure, menu path, arguments) and the
# correction itself (PLAN.md 5.4): the parameters equal those of
# auto-perspective-cli on the same pixels, the result matches
# auto-perspective-cli warp, the facade's true edges drawn as a path come
# out vertical, channels, paths and the selection follow, the canvas or
# the layer is cropped, every mode, crop and apply-to setting on 8 and 16
# bit and floating point, RGB and gray, alpha, offsets, several layers;
# blank images and indexed ones are left alone; the guided mode with
# strokes along known edges of a facade and of a real photo, measured on
# edges not drawn. Afterwards the corrected facade is fitted again by
# auto-perspective-cli (nothing left to fix), and the lines of the guided
# real photo are found again and measured.
#
#   tests/gimp-test.sh            as usual
#   tests/gimp-test.sh --asan     the plug-in built with AddressSanitizer and
#                                 UBSan
#
# GIMP runs with a throwaway profile in tests/output (the plug-in is
# installed there, nothing in yours) and isolated from your folders
# (tests/isolate.sh): before and after, your folders of GIMP and the other
# apps are listed, and the test fails if anything there changed. GIMP loads
# no fonts and no data (--no-fonts --no-data) and starts as a new instance.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
out=$here/output
g=$out/gimp
mkdir -p "$out"
# (isolate.sh needs $src)
# shellcheck disable=SC2034
src=$top
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$out/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
snapshot_take "$out/snapshot-gimp-before.txt"
gimp_build=${GIMP_BUILD:-$devtools/gimp-build.sh}
in_sdk () { "$gimp_build" "$top" "$*"; }

build=tests/output/build
profile=$out/profile
run_args=
if [ "$1" = --asan ]; then
    build=tests/output/build-asan
    profile=$out/profile-asan
    run_args="--env=ASAN_OPTIONS=log_path=$out/sanitizer/asan:detect_leaks=0 \
      --env=UBSAN_OPTIONS=log_path=$out/sanitizer/ubsan:print_stacktrace=1"
    [ -f "$top/$build/build.ninja" ] ||
      in_sdk meson setup "$build" -Db_sanitize=address,undefined -Db_lundef=false \
        "-Dplugindir=$profile/plug-ins" >"$out/setup-asan.log" 2>&1 ||
      { cat "$out/setup-asan.log"; exit 1; }
fi
[ -f "$top/$build/build.ninja" ] || in_sdk meson setup "$build" >"$out/setup.log" 2>&1 ||
  { cat "$out/setup.log"; exit 1; }
in_sdk meson configure "$build" "-Dplugindir=$profile/plug-ins" >/dev/null
in_sdk ninja -C "$build" install >"$out/build.log" 2>&1 || { cat "$out/build.log"; exit 1; }
rm -rf "$out/sanitizer" "$g"
mkdir -p "$out/sanitizer" "$g/in" "$g/out"
status=0

python3 "$here/synth.py" --size=320,240 --pitch=10 --roll=2 --truth="$g/in/truth.txt" "$g/in/facade.pfm" || exit 1
# the facade's true edges in the image (through the true camera): two
# verticals and a horizontal, for the guided mode and the path test
python3 - "$here" "$g/in" <<'PY' || exit 1
import importlib.util, math, os, sys
import numpy as np
here, inp = sys.argv[1], sys.argv[2]
spec = importlib.util.spec_from_file_location('rec', os.path.join(here, 'recovery.py'))
rec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rec)
w, h = 320, 240
R = rec.synth.rot(2, 10, 0)   # roll, pitch, yaw as above
f = 28.0 / 36.0 * w
def edge(u0, v0, u1, v1):
    P = np.array([[u0, v0, 20.0], [u1, v1, 20.0]])
    x, y, _ = rec.project(P, R, f, w, h)
    return '%.6f %.6f %.6f %.6f' % (x[0], y[0], x[1], y[1])
with open(os.path.join(inp, 'edges.txt'), 'w') as fp:
    fp.write(edge(-4, -4, -4, 5) + '\n' + edge(4, -4, 4, 5) + '\n' + edge(-5, 0.8, 5, 0.8) + '\n')
# the guided mode (PLAN.md 3.5): a second facade, turned in all three
# directions; the edges a user would draw (two pilaster edges, two sill
# lines) and, to measure the result, others that are not drawn
w, h = 480, 360
R = rec.synth.rot(1.5, 8, 14)
f = 28.0 / 36.0 * w
with open(os.path.join(inp, 'guided-drawn.txt'), 'w') as fp:
    for e in ((-6, -6, -6, 6), (6, -6, 6, 6), (-6, -5.2, 6, -5.2), (-6, 3.8, 6, 3.8)):
        fp.write(edge(*e) + '\n')
with open(os.path.join(inp, 'guided-check.txt'), 'w') as fp:
    for e in ((-4, -7, -4, 7), (-2, -7, -2, 7), (0, -7, 0, 7), (2, -7, 2, 7), (4, -7, 4, 7),
              (-5.5, -6, -5.5, 6), (4.5, -6, 4.5, 6),
              (-7, -2.2, 7, -2.2), (-7, 0.8, 7, 0.8), (-7, 2.4, 7, 2.4), (-7, 6.8, 7, 6.8)):
        fp.write(edge(*e) + '\n')
PY
python3 "$here/synth.py" --size=480,360 --roll=1.5 --pitch=8 --yaw=14 "$g/in/guided.pfm" || exit 1
# what auto-perspective-cli finds on the same pixels
cli=$top/$build/auto-perspective-cli
for m in auto vertical full; do
    "$cli" fit --mode=$m "$g/in/facade.pfm" >"$g/in/cli-fit-$m.txt" || exit 1
done
"$cli" warp --mode=vertical --crop=largest --interp=linear "$g/in/facade.pfm" -o "$g/in/cli-warp.pfm" \
  >"$g/in/cli-warp.txt" || exit 1

# shellcheck disable=SC2086
gimp_run --timeout=600 $run_args \
  --env=GIMP3_DIRECTORY="$profile" --env=UT_IN="$g/in" --env=UT_OUT="$g/out" --env=UT_REAL="$out/real" -- \
  gimp-console-3.2 --new-instance --no-interface --no-data --no-fonts \
  --batch-interpreter python-fu-eval -b "exec(open('$here/gimp-test.py').read())" \
  >"$g/gimp.log" 2>&1
grep -E '^UT (FAIL|SKIP)|Traceback|^  File|Error' "$g/gimp.log"
echo "UT $(grep -c '^UT PASS' "$g/gimp.log") checks passed"
grep -q '^UT failures: 0$' "$g/gimp.log" || status=1
if grep -E 'gimp-auto-perspective.*(WARNING|CRITICAL)|inconsistent state' "$g/gimp.log"; then
    echo "UT FAIL warnings from the plug-in, see $g/gimp.log"
    status=1
fi
# the corrected facade, fitted again: its verticals are vertical
if [ -f "$g/out/gimp-vertical.pfm" ]; then
    "$cli" fit --mode=vertical "$g/out/gimp-vertical.pfm" >"$g/out/refit.txt"
    python3 - "$g/out/refit.txt" <<'PY' || status=1
import sys
r = dict(l.split(' ', 1) for l in open(sys.argv[1]).read().splitlines())
rot, sv = float(r['rotation']), float(r['shift-v'])
ok = r['status'] == 'ok' and abs(rot) < 0.05 and abs(sv) < 0.003
print('UT %s the facade corrected in GIMP, fitted again by auto-perspective-cli: rotation %.4f, '
      'shift-v %.5f (nothing left: below 0.05 and 0.003)' % ('PASS' if ok else 'FAIL', rot, sv))
sys.exit(0 if ok else 1)
PY
else
    echo "UT FAIL no corrected facade from GIMP ($g/out/gimp-vertical.pfm)"
    status=1
fi
# the guided result on the real photo (when tests/real.sh fetched it): the
# lines found in it again, the strongest of each kind (as tests/real.py
# measures)
if [ -f "$g/out/guided-real.pfm" ]; then
    "$cli" lines "$g/out/guided-real.pfm" >"$g/out/guided-real-lines.txt"
    python3 - "$here" "$g/out/guided-real-lines.txt" <<'PY' || status=1
import importlib.util, os, sys
spec = importlib.util.spec_from_file_location('real', os.path.join(sys.argv[1], 'real.py'))
real = importlib.util.module_from_spec(spec)
spec.loader.exec_module(real)
segs = []
for line in open(sys.argv[2]):
    r = line.split()
    segs.append((float(r[0]), float(r[1]), float(r[2]), float(r[3]), float(r[5]), int(r[6]), int(r[7])))
mv, xv, nv = real.strongest_deviation(segs, 1)
mh, xh, nh = real.strongest_deviation(segs, 2)
ok = mv is not None and mh is not None and mv < 0.5 and mh < 0.5
print('UT %s guided on the real photo, the lines found again afterwards: the %d strongest verticals '
      'weighted median %.3f degrees (largest %.3f), the %d strongest horizontals %.3f (largest %.3f); '
      'target below 0.5' % ('PASS' if ok else 'FAIL', nv, mv, xv, nh, mh, xh))
sys.exit(0 if ok else 1)
PY
fi
for report in "$out"/sanitizer/*; do
    [ -e "$report" ] || continue
    echo "UT FAIL sanitizer report $report"
    head -30 "$report"
    status=1
done
snapshot_check "$out/snapshot-gimp-before.txt" "UT " || status=1
[ $status = 0 ] && echo "UT all passed" || echo "UT FAILED (log: $g/gimp.log)"
exit $status
