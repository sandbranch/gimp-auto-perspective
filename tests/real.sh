#!/bin/sh
# Real photos (PLAN.md 5.3): fetches three freely licensed facades from
# Wikimedia Commons into tests/output/real (once; checked by SHA-256; never
# committed) and runs tests/real.py on them with auto-perspective-cli.
#
#   tests/real.sh          (after tests/check.sh has built auto-perspective-cli)
#
# The licence of each file was read on its file page on 2026-11-18 (the
# wikitext's licence template and the API's LicenseShortName):
#
#   Kirkgate_Market_02.jpg     {{self|cc-zero}}, CC0, Immanuel Giel
#   Stolbergs "Bankenmeile" in der Rathausstraße.jpg
#                              {{PD-self}}, public domain, Aufklärung
#   Iwase_Bunko_Library1.JPG   {{self|Cc-zero}}, CC0, At by At
#
# Not part of tests/run.sh (it needs the network the first time). Without
# a network connection and no copies fetched before, it says SKIP.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
out=$here/output
real=$out/real
mkdir -p "$real"
# (isolate.sh needs $src)
# shellcheck disable=SC2034
src=$top
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$out/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
gimp_build=${GIMP_BUILD:-$devtools/gimp-build.sh}
in_sdk () { "$gimp_build" "$top" "$*"; }

# Wikimedia asks for a descriptive User-Agent; this one names the purpose only
agent='gimp-auto-perspective-tests/0.1 (test images for a GIMP plug-in)'
base=https://upload.wikimedia.org/wikipedia/commons
status=0
while read -r name path sum; do
    f=$real/$name
    if [ ! -s "$f" ]; then
        # (upload.wikimedia.org answers 429 to quick repeats: wait and retry)
        for wait in 0 30 90; do
            sleep "$wait"
            curl -sSfL -A "$agent" -o "$f.part" "$base/$path" && mv "$f.part" "$f" && break
            rm -f "$f.part"
        done
    fi
    if [ ! -s "$f" ]; then
        echo "SKIP $name not fetched from $base/$path"
        continue
    fi
    # (SHA-256 as measured here; SHA-1, as Commons publishes it, for the
    # file that could not be fetched on 2026-11-18)
    if [ ${#sum} = 40 ]; then check=sha1sum; else check=sha256sum; fi
    echo "$sum  $f" | $check -c --quiet - ||
      { echo "FAIL $name is not the file checked on 2026-11-18 ($check)"; status=1; }
done <<'LIST'
Kirkgate_Market_02.jpg c/cc/Kirkgate_Market_02.jpg 249f9bdd9a62d02ea653949becf7fc9ab10213f33635a884c62bfdfb0464ada3
Stolbergs_Bankenmeile.jpg 4/46/Stolbergs_%22Bankenmeile%22_in_der_Rathausstra%C3%9Fe.jpg 1148f6a90de0392036a71df569bdf8ea91705bbbe1a4c04a03feedc39b32073f
Iwase_Bunko_Library1.JPG a/a7/Iwase_Bunko_Library1.JPG be9317456b931236df9bb959b14bcdefacf418ef
LIST
cli=tests/output/build/auto-perspective-cli
[ -x "$top/$cli" ] || { echo "FAIL no $cli: run tests/check.sh first"; exit 1; }
in_sdk "python3 tests/real.py $cli tests/output/real" || status=1
exit $status
