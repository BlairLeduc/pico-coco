#!/bin/sh
# MAME's files that the M18 oracles compile as tools, and M19's RS-232
# pak and 6551, read (design.md §12.11), fetched at a pinned
# commit into reference/mame (gitignored), as build-xroar.sh fetches
# XRoar (design.md §12.9; MAME is never source in the tree, D2, D8).
#
#   tools/mame/fetch-mame.sh
#
# The commit is MAME's master of 2026-08-14, Zbiciak's correction of the
# SP0256's divider to the datasheet's 10 kHz; the samples do not depend on
# the divider. Re-run cmake afterwards.
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
commit=638224548b
out="$root/reference/mame"
mkdir -p "$out"
for f in src/devices/sound/sp0256.cpp src/devices/sound/sp0256.h \
         src/devices/bus/coco/coco_ssc.cpp src/devices/bus/coco/coco_orch90.cpp \
         src/devices/cpu/tms7000/tms7000.cpp src/devices/cpu/tms7000/tms7000op.cpp \
         src/devices/bus/coco/coco_rs232.cpp src/devices/machine/mos6551.cpp \
         src/devices/machine/mos6551.h; do
    curl -sfL -o "$out/$(basename "$f")" \
        "https://raw.githubusercontent.com/mamedev/mame/$commit/$f"
done
echo "MAME's files at $commit in $out"
