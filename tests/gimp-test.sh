#!/bin/sh
# Tests the plug-in in the Flatpak GIMP without a window: tests/gimp-test.py
# checks the registration (procedure, menu path, arguments) and, while
# the plug-in is a skeleton, that every mode leaves the image exactly as
# it was, on images of 8 and 16 bits and floating point, RGB and gray.
#
#   tests/gimp-test.sh            as usual
#   tests/gimp-test.sh --asan     the plug-in built with AddressSanitizer and
#                                 UBSan (GIMP runs with the SDK: --devel)
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
    run_args="--devel --env=ASAN_OPTIONS=log_path=$out/sanitizer/asan:detect_leaks=0 \
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
mkdir -p "$out/sanitizer" "$g/in"
status=0

python3 "$here/synth.py" --size=320,240 --pitch=10 --roll=2 "$g/in/facade.pfm" || exit 1

# shellcheck disable=SC2086
gimp_run --timeout=600 --flatpak $run_args --filesystem="$top" \
  --env=GIMP3_DIRECTORY="$profile" --env=UT_IN="$g/in" -- \
  gimp-console-3.2 --new-instance --no-interface --no-data --no-fonts \
  --batch-interpreter python-fu-eval -b "exec(open('$here/gimp-test.py').read())" \
  >"$g/gimp.log" 2>&1
grep -E '^UT FAIL|Traceback|^  File|Error' "$g/gimp.log"
echo "UT $(grep -c '^UT PASS' "$g/gimp.log") checks passed"
grep -q '^UT failures: 0$' "$g/gimp.log" || status=1
if grep -E 'gimp-upright.*(WARNING|CRITICAL)|inconsistent state' "$g/gimp.log"; then
    echo "UT FAIL warnings from the plug-in, see $g/gimp.log"
    status=1
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
