#!/bin/sh
# The unit tests of the core (tests/unit), built twice: as usual and with
# AddressSanitizer and UndefinedBehaviorSanitizer, leaks included. Also
# runs auto-perspective-cli under the sanitizers on synthetic facades
# (tests/synth.py) and on guide lines from a file. Builds and runs inside
# the Flatpak SDK (gimp-devtools/gimp-build.sh), isolated from your
# folders.
#
#   tests/check.sh           both builds
#   tests/check.sh quick     only the usual build
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
out=$here/output
mkdir -p "$out"
# (isolate.sh needs $src)
# shellcheck disable=SC2034
src=$top
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$out/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
gimp_build=${GIMP_BUILD:-$devtools/gimp-build.sh}
in_sdk () { "$gimp_build" "$top" "$*"; }
status=0

unit () {
    build=$1 env=$2 log=$3
    in_sdk "$env meson test -C $build --print-errorlogs" >"$log" 2>&1 || status=1
    for t in core; do
        in_sdk "$env $build/test-$t" >"$log.$t" 2>&1 || status=1
        grep -E '^(FAIL|SKIP)' "$log.$t"
        echo "$t: $(grep -E '^[0-9]+ passed' "$log.$t" || echo 'did not finish')"
    done
}

echo "== usual build"
[ -f "$out/build/build.ninja" ] || in_sdk meson setup tests/output/build >"$out/setup.log" 2>&1
in_sdk ninja -C tests/output/build >"$out/build.log" 2>&1 || { cat "$out/build.log"; exit 1; }
unit tests/output/build "" "$out/unit.log"
[ "$1" = quick ] && exit $status

echo "== AddressSanitizer and UndefinedBehaviorSanitizer"
[ -f "$out/build-asan/build.ninja" ] ||
  in_sdk meson setup tests/output/build-asan -Db_sanitize=address,undefined -Db_lundef=false \
    >"$out/setup-asan.log" 2>&1
in_sdk ninja -C tests/output/build-asan >"$out/build-asan.log" 2>&1 ||
  { cat "$out/build-asan.log"; exit 1; }
san="ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1"
unit tests/output/build-asan "$san" "$out/unit-asan.log"

python3 "$here/synth.py" --size=160,120 "$out/cli-flat.pfm" &&
  python3 "$here/synth.py" --size=161,97 --pitch=8 --roll=2 "$out/cli-tilt.pfm" ||
  { echo "FAIL tests/synth.py"; exit 1; }
printf '10 5 12 90\n50 4 49 91\n5 50 150 52\n' > "$out/cli-lines.txt"
cli=tests/output/build-asan/auto-perspective-cli
o=tests/output
in_sdk "$san $cli version && \
  $san $cli fit $o/cli-flat.pfm && \
  $san $cli fit --mode=full $o/cli-tilt.pfm && \
  $san $cli lines $o/cli-tilt.pfm && \
  $san $cli fit --mode=vertical --lines=$o/cli-lines.txt $o/cli-tilt.pfm" \
  >"$out/cli-asan.log" 2>&1 || { status=1; echo "FAIL auto-perspective-cli under the sanitizers:"; tail "$out/cli-asan.log"; }
# the guided lines: two vertical and one horizontal, as perspective.h classifies them
if grep -q '^vertical 2$' "$out/cli-asan.log" && grep -q '^horizontal 1$' "$out/cli-asan.log"; then
    echo "PASS auto-perspective-cli reads and classifies guide lines"
else
    echo "FAIL auto-perspective-cli guide lines: $(tr '\n' ' ' < "$out/cli-asan.log")"
    status=1
fi
if grep -qE 'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:' "$out"/unit-asan.log* "$out/cli-asan.log"; then
    echo "FAIL sanitizer reports:"
    grep -h -E -A5 'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:' "$out"/unit-asan.log* "$out/cli-asan.log" | head -30
    status=1
else
    echo "PASS no memory errors, leaks or undefined behaviour (unit tests and auto-perspective-cli)"
fi
[ $status = 0 ] && echo "all checks passed" || echo "SOME CHECKS FAILED"
exit $status
