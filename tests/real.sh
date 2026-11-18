#!/bin/sh
# Real photos (PLAN.md 5.3): fetches thirteen freely licensed photos from
# Wikimedia Commons into tests/output/real (once; checked by SHA-256; never
# committed) and runs tests/real.py on them with auto-perspective-cli.
#
#   tests/real.sh          (after tests/check.sh has built auto-perspective-cli)
#
# The licence of each file was read on its file page (the wikitext's
# licence template and the API's LicenseShortName), all on 2026-11-18;
# the ten after the first three are for the tuning of the automatic mode
# (docs/research.md, section 6, has authors and links):
#
#   Kirkgate_Market_02.jpg     {{self|cc-zero}}, CC0, Immanuel Giel
#   Stolbergs "Bankenmeile" in der Rathausstraße.jpg
#                              {{PD-self}}, public domain, Aufklärung
#   Iwase_Bunko_Library1.JPG   {{self|Cc-zero}}, CC0, At by At
#   Gooderham (Flatiron) Building.Toronto. (49145357446).jpg
#                              {{cc-zero}}, FlickreviewR passed, Bernard Spragg
#   Iwase_Bunko_Library2.JPG   {{self|Cc-zero}}, CC0, At by At
#   Former Tonoi Post Office Building - front facade.jpg
#                              {{self|cc-zero}}, CC0, Huugo
#   Fifty-Ninth Bank Head Office Main Building 20200921.jpg
#                              {{self|cc-zero}}, CC0, Kaburamata
#   Chester Cathedral Nave 1.jpg
#                              {{self|cc-zero}}, CC0, Michael D Beckwith
#   Nave - Church of St Mary the Virgin.jpg
#                              {{self|cc-zero}}, CC0, LAS30a
#   Commercial street scene in Arusha city centre, showing M-Pesa and NMB
#   Wakala agents.jpg          {{self|cc-zero}}, CC0, Brayan Tesha
#   Broadway street scene at 116th Street, Manhattan, New York City.jpg
#                              {{self|cc-zero}}, CC0, Z3lvs
#   Hartz Lake at Hartz Mountain.jpg
#                              {{self|cc-zero}}, CC0, JustinH
#   Autumn Forest Path with Tall Trees.jpg
#                              {{self|cc-zero}}, CC0, Vovogov90
#
# The ten new ones are Commons' 3840 pixel renditions (the thumbnail
# server only serves its standard widths: 2560 answers 400 "Use thumbnail
# sizes listed on https://w.wiki/GHai"), except Fifty-Ninth Bank, whose
# original is narrower. A rendition can be made again by the server and
# then differ in its bytes, so for those a different checksum is a NOTE,
# not a failure (the measured numbers may then move a little).
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
    if ! echo "$sum  $f" | $check -c --quiet - 2>/dev/null; then
        case $path in
            thumb/*) echo "NOTE $name: Commons' rendition differs from the one measured on 2026-11-18" ;;
            *) echo "FAIL $name is not the file checked on 2026-11-18 ($check)"; status=1 ;;
        esac
    fi
done <<'LIST'
Kirkgate_Market_02.jpg c/cc/Kirkgate_Market_02.jpg 249f9bdd9a62d02ea653949becf7fc9ab10213f33635a884c62bfdfb0464ada3
Stolbergs_Bankenmeile.jpg 4/46/Stolbergs_%22Bankenmeile%22_in_der_Rathausstra%C3%9Fe.jpg 1148f6a90de0392036a71df569bdf8ea91705bbbe1a4c04a03feedc39b32073f
Iwase_Bunko_Library1.JPG a/a7/Iwase_Bunko_Library1.JPG be9317456b931236df9bb959b14bcdefacf418ef
Gooderham_Flatiron_Toronto.jpg thumb/3/38/Gooderham_%28Flatiron%29_Building.Toronto._%2849145357446%29.jpg/3840px-Gooderham_%28Flatiron%29_Building.Toronto._%2849145357446%29.jpg 3508a21528e22567c0d0005e844036fcfdbe6cd6964d4e54d2c0b2903de47ec4
Iwase_Bunko_Library2.jpg thumb/6/6c/Iwase_Bunko_Library2.JPG/3840px-Iwase_Bunko_Library2.JPG 228569a5b7d5fda028312da74067faa99e1ec98848256b58366e9c70b63a2ac3
Tonoi_Post_Office.jpg thumb/b/be/Former_Tonoi_Post_Office_Building_-_front_facade.jpg/3840px-Former_Tonoi_Post_Office_Building_-_front_facade.jpg 5b00270b5f83588eef17e9f56f017816feffad5e74cdb4c70b6181648f7946a1
Fifty-Ninth_Bank.jpg e/e1/Fifty-Ninth_Bank_Head_Office_Main_Building_20200921.jpg 2b48a21f8376fc6613bd250c869d93dc1ed7f65766be58baba4d887232e7d8d0
Chester_Cathedral_Nave.jpg thumb/0/0a/Chester_Cathedral_Nave_1.jpg/3840px-Chester_Cathedral_Nave_1.jpg 5acf00f3649fe83bdf90c1c4d4f043915b4b54df0c730eb48eae028d9ae81b82
St_Mary_the_Virgin_Nave.jpg thumb/3/30/Nave_-_Church_of_St_Mary_the_Virgin.jpg/3840px-Nave_-_Church_of_St_Mary_the_Virgin.jpg 2134ca340f9e8e9f1fcb2a1315a4b103e39f4dc938db155f0d6d9676cc935ea7
Arusha_street.jpg thumb/c/c8/Commercial_street_scene_in_Arusha_city_centre%2C_showing_M-Pesa_and_NMB_Wakala_agents.jpg/3840px-Commercial_street_scene_in_Arusha_city_centre%2C_showing_M-Pesa_and_NMB_Wakala_agents.jpg 6f03ef14e749621bf15752728df794cfed565f73632ef7cf04488eb3c753fc75
Broadway_116th.jpg thumb/f/f8/Broadway_street_scene_at_116th_Street%2C_Manhattan%2C_New_York_City.jpg/3840px-Broadway_street_scene_at_116th_Street%2C_Manhattan%2C_New_York_City.jpg 2c1a6071e5e7c0083a1b47a99405bc0dd394b9c6417a83ae33c2635492d0b298
Hartz_Lake.jpg thumb/5/53/Hartz_Lake_at_Hartz_Mountain.jpg/3840px-Hartz_Lake_at_Hartz_Mountain.jpg ba3add3fadec6e83b97db84fb8c5e226d3e257ad571cd759ba38d55b51714337
Autumn_Forest_Path.jpg thumb/2/20/Autumn_Forest_Path_with_Tall_Trees.jpg/3840px-Autumn_Forest_Path_with_Tall_Trees.jpg 2e96d3e52aed6d4597ad9c649f6ffbaa444e145cffb33751e4ee8707e7721b61
LIST
cli=tests/output/build/auto-perspective-cli
[ -x "$top/$cli" ] || { echo "FAIL no $cli: run tests/check.sh first"; exit 1; }
in_sdk "python3 tests/real.py $cli tests/output/real" || status=1
exit $status
