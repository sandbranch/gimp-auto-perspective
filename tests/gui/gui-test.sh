#!/bin/sh
# Tests the plug-in in GIMP's real window, on a Broadway display with a
# headless Chrome, and leaves screenshots in tests/output/gui/:
#
#   menu    Image > Transform open: Auto Perspective... under Arbitrary Rotation
#   dialog  the synthetic facade, Image > Transform > Auto Perspective..., OK:
#           success, the fit ok (the facade's camera looks up 10 degrees,
#           so automatic picks vertical), the image corrected and cropped
#
# Run tests/gimp-test.sh first (it installs the plug-in into the test
# profile). Needs a headless Chrome, node 22 and ../gimp-devtools (or
# GIMP_PLUGIN_DEVTOOLS) for gui/cdp.mjs; skipped without them. GIMP runs
# isolated from your folders (start.sh) as a new instance; before and
# after, your folders are listed and the test fails if anything there
# changed. Stops only the GIMP and the Chrome it started.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later

# (pass never fails, so "check && pass || fail" is an if-then-else)
# shellcheck disable=SC2015
here=$(cd "$(dirname "$0")" && pwd)
status=0
pass () { echo "GUI PASS $1"; }
fail () { echo "GUI FAIL $1"; status=1; }

# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
# shellcheck source=SCRIPTDIR/../isolate.sh
. "$tests/isolate.sh"
rm -f "$out"/0*.png
mkdir -p "$tests/output/gui-in"
python3 "$tests/synth.py" --size=640,480 --pitch=10 --roll=2 "$tests/output/gui-in/facade.pfm" ||
  { echo "GUI FAIL tests/synth.py"; exit 1; }
snapshot_take "$out/snapshot-before.txt"

# positions: in the image window (single-window mode, the page's size),
# and from the dialog's top left corner (the Broadway canvas); found on
# the screenshots, override with the variables if the theme moves them
IMAGE_MENU=${GUI_IMAGE_MENU:-212,49}
TRANSFORM_ITEM=${GUI_TRANSFORM_ITEM:-246,175}
# OK: from the dialog's bottom right corner (GIMP puts OK last), as the
# dialog's size depends on the preview and the fonts
OK_FROM_RIGHT=${GUI_OK_FROM_RIGHT:-55}
OK_FROM_BOTTOM=${GUI_OK_FROM_BOTTOM:-22}

click_in_dialog () {
    pos=$(p "${1%,*}" "${1#*,}")
    $cdp "$view" move:"$(p 100 60)" wait:400 move:"$pos" wait:400 click:"$pos" wait:"${2:-800}" >/dev/null
}

result_is () {
    grep -q "^$1\$" "$out/result.txt" && pass "$2" || fail "$2: $(tr '\n' ' ' < "$out/result.txt")"
}

session () {
    name=$1
    GUI_MODE=$name
    export GUI_MODE
    start_gimp "$name"
    if ! wait_for "$out/ready" 120; then
        fail "$name: GIMP did not get ready (log: $out/gimp-$name.log)"
        stop_all
        return
    fi
    case $name in
    menu)
        sleep 4
        $cdp "$view" move:"$IMAGE_MENU" wait:300 click:"$IMAGE_MENU" wait:1500 \
          move:"$TRANSFORM_ITEM" wait:1500 shot:"$out/01-transform-menu.png" \
          key:Escape wait:300 key:Escape wait:300 >/dev/null
        touch "$out/check"
        wait_for "$out/result.txt" 60 && pass "menu: Image > Transform opened (see 01-transform-menu.png)" ||
          fail "menu: no result";;
    dialog)
        find_dialog
        [ -n "$at" ] || { fail "dialog: the dialog did not open"; stop_all; return; }
        pass "dialog: the dialog opened (at $at, ${dw}x$dh)"
        $cdp "$view" wait:800 shot:"$out/02-dialog.png" >/dev/null
        $cdp "$view" wait:3000 shot:"$out/02-dialog-preview.png" >/dev/null
        click_in_dialog "$((dw - OK_FROM_RIGHT)),$((dh - OK_FROM_BOTTOM))" 1000
        # (should the click miss OK: Enter activates the default button)
        wait_for "$out/result.txt" 20 || $cdp "$view" key:Enter >/dev/null
        if ! wait_for "$out/result.txt" 120; then
            fail "dialog: no result after OK (log: $out/gimp-$name.log)"; stop_all; return
        fi
        result_is "status success" "dialog: status success"
        result_is "layers 1" "dialog: still one layer"
        result_is "fit ok" "dialog: the fit found its lines"
        result_is "mode vertical" "dialog: automatic chose vertical"
        grep -q '^size 640x480$' "$out/result.txt" &&
          fail "dialog: the canvas was not cropped: $(tr '\n' ' ' < "$out/result.txt")" ||
          pass "dialog: the canvas was cropped ($(grep '^size' "$out/result.txt"))"
        result_is "unchanged no" "dialog: the image was corrected"
        $cdp "$view" wait:800 shot:"$out/03-result.png" >/dev/null;;
    esac
    touch "$out/done"
    stop_all
}

for s in ${GUI_ONLY:-menu dialog}; do
    session "$s"
done

snapshot_check "$out/snapshot-before.txt" "GUI " || status=1
[ $status = 0 ] && echo "GUI all passed (screenshots in $out)" || echo "GUI FAILED (screenshots in $out)"
exit $status
