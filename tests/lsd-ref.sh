#!/bin/sh
# Our LSD against the reference implementation, LSD 1.6 of the IPOL
# article (www.ipol.im/pub/art/2012/gjmr-lsd/, AGPL-3.0-or-later): the
# archive is fetched once into tests/output/lsd-ref (checked by SHA-256),
# built there and used as a test oracle only; nothing of it is part of
# the plug-in or of this repository. tests/lsd-ref.py does the comparing.
#
#   tests/lsd-ref.sh       (after tests/check.sh has built auto-perspective-cli)
#
# Without a network connection (and no archive fetched before) it says
# SKIP and succeeds.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
out=$here/output
ref=$out/lsd-ref
mkdir -p "$ref"
# (isolate.sh needs $src)
# shellcheck disable=SC2034
src=$top
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$out/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
gimp_build=${GIMP_BUILD:-$devtools/gimp-build.sh}
in_sdk () { "$gimp_build" "$top" "$*"; }

url=http://www.ipol.im/pub/art/2012/gjmr-lsd/lsd_1.6.zip
sum=feca4daf74ccb11da7c66cae309b235cf756aecabbb58c5b9e84bc9ee52b5634
zip=$ref/lsd_1.6.zip
if [ ! -f "$zip" ]; then
    curl -sSfL -A generic-client -o "$zip.part" "$url" && mv "$zip.part" "$zip" ||
      { rm -f "$zip.part"; echo "SKIP LSD 1.6 not fetched from $url (offline?)"; exit 0; }
fi
echo "$sum  $zip" | sha256sum -c --quiet - ||
  { echo "FAIL $zip is not the archive checked on 2026-11-18 (SHA-256)"; exit 1; }
if [ ! -x "$ref/lsd_1.6/lsd" ]; then
    (cd "$ref" && unzip -oq lsd_1.6.zip) || { echo "FAIL cannot unpack $zip"; exit 1; }
    in_sdk "cc -O2 -o tests/output/lsd-ref/lsd_1.6/lsd tests/output/lsd-ref/lsd_1.6/lsd_cmd.c \
      tests/output/lsd-ref/lsd_1.6/lsd.c -lm" >"$ref/build.log" 2>&1 ||
      { echo "FAIL building LSD 1.6:"; cat "$ref/build.log"; exit 1; }
fi
cli=$out/build/auto-perspective-cli
[ -x "$cli" ] || { echo "FAIL no $cli: run tests/check.sh first"; exit 1; }
python3 "$here/lsd-ref.py" "$cli" "$ref/lsd_1.6" "$out/lsd-ref/run"
