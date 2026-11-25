#!/bin/sh
# Trace the same boot and typing under XRoar and this core, and diff them
# (design.md §15.2, EL §11.4).
#
#   tools/trace/trace-coco.sh [-bas bas13|bas11] [-ram K] [-ext]
#                             [-fields N] [-type STRING]
#                             [-cart rsdos|mpi] [-mpi-slot N] [-mpi-load-cart N=rsdos]
#
# The cartridge options are XRoar's, given to both (design.md §12.8).
#
# The CoCo is XRoar's cocous (NTSC).
#
# Needs XRoar built headless (tools/trace/build-xroar.sh) and the host
# build's coco-trace (build/host/tools/trace/coco-trace, or COCO_TRACE).
# The ROMs come from roms/ for both. Traces and the diff go to out/trace/.
set -eu

root="$(cd "$(dirname "$0")/../.." && pwd)"
xroar="${XROAR:-$root/out/xroar/src/xroar}"
ours="${COCO_TRACE:-$root/build/host/coco-trace}"
out="$root/out/trace"
mkdir -p "$out"

bas=bas13; ram=""; ext=0; fields=300; type=""; cart=""; mslot=""; mload=""
while [ $# -gt 0 ]; do
    case "$1" in
        -bas) bas="$2"; shift 2 ;;
        -ram) ram="$2"; shift 2 ;;
        -ext) ext=1; shift ;;
        -fields) fields="$2"; shift 2 ;;
        -type) type="$2"; shift 2 ;;
        -cart) cart="$2"; shift 2 ;;
        -mpi-slot) mslot="$2"; shift 2 ;;
        -mpi-load-cart) mload="$mload -mpi-load-cart $2"; shift 2 ;;
        *) echo "trace-coco.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done

[ -x "$xroar" ] || { echo "no XRoar at $xroar: tools/trace/build-xroar.sh" >&2; exit 1; }
[ -x "$ours" ] || { echo "no coco-trace at $ours: build the host tree" >&2; exit 1; }

ram="${ram:-64}"; hz=60
if [ "$ext" = 1 ]; then rom="-extbas extbas11"; oext="-ext"; else rom="-no-extbas"; oext=""; fi
xargs="-machine cocous -bas $bas $rom"
oargs="-bas $bas $oext"
cargs="${cart:+-cart $cart}${mslot:+ -mpi-slot $mslot}$mload"
nocart="-no-machine-cart"; [ -z "$cart" ] || nocart=""
tag="ram${ram}${oext}${cart:+-$cart}${mslot:+$mslot}"
[ "$bas" = bas13 ] || tag="$bas-$tag"
# XRoar's timeout is in emulated seconds; give it a second more than ours.
secs=$(( fields / hz + 2 ))

# shellcheck disable=SC2086
"$xroar" $xargs -ram "$ram" $cargs $nocart \
    -rompath "$root/roms" -ram-init clear -ui null -ao null -trace \
    -timeout "$secs" ${type:+-type "$type"} > "$out/$tag.xroar" 2>&1 || true
# shellcheck disable=SC2086
"$ours" $oargs -ram "$ram" $cargs -fields "$fields" ${type:+-type "$type"} \
    > "$out/$tag.ours" 2> "$out/$tag.screen"
python3 "$root/tools/trace/trace-diff.py" "$out/$tag.xroar" "$out/$tag.ours"
