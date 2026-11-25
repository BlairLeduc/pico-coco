#!/bin/sh
# MAME 0.289's Speech/Sound Cartridge, its TMS7040 traced, for
# test_tms7000's oracle (design.md §12.9).
#
#   tools/mame/trace-ssc.sh [FIELDS]       default 600
#
# Needs mame on the PATH, the CoCo 2B's ROMs where MAME finds them, and
# pic-7040-510.bin and sp0256-al2.bin in roms/ (staged for MAME in
# out/mame/roms/coco_ssc). Writes out/mame/ssc/ssc.tr, ssc.end and
# ssc.bytes; the trace is large (about 20 MB a second traced).
set -eu
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/out/mame/ssc"
mkdir -p "$out" "$root/out/mame/roms/coco_ssc"
cp "$root/roms/pic-7040-510.bin" "$root/roms/sp0256-al2.bin" "$root/out/mame/roms/coco_ssc/"
fields="${1:-600}"
SSC_OUT="$out" SSC_FRAMES="$fields" mame coco2b -ext ssc \
    -rompath "$HOME/.mame/roms;$root/out/mame/roms" \
    -video none -sound none -nothrottle -skip_gameinfo -debug -debugger none \
    -seconds_to_run $(( fields / 60 + 5 )) \
    -autoboot_script "$root/tools/mame/ssc-trace.lua" > "$out/mame.log" 2>&1 || true
wc -l "$out/ssc.tr" | sed 's/^ */trace: /'
cat "$out/ssc.end"
