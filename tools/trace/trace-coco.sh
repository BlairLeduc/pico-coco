#!/bin/sh
# Trace the same boot and typing under XRoar and this core, and diff them
# (design.md §15.2, EL §11.4).
#
#   tools/trace/trace-coco.sh [-ram K] [-ext] [-fields N] [-type STRING]
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

ram=64; ext=0; fields=300; type=""
while [ $# -gt 0 ]; do
    case "$1" in
        -ram) ram="$2"; shift 2 ;;
        -ext) ext=1; shift ;;
        -fields) fields="$2"; shift 2 ;;
        -type) type="$2"; shift 2 ;;
        *) echo "trace-coco.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done

[ -x "$xroar" ] || { echo "no XRoar at $xroar: tools/trace/build-xroar.sh" >&2; exit 1; }
[ -x "$ours" ] || { echo "no coco-trace at $ours: build the host tree" >&2; exit 1; }

if [ "$ext" = 1 ]; then rom="-extbas extbas11"; oext="-ext"; else rom="-no-extbas"; oext=""; fi
# XRoar's timeout is in emulated seconds; give it a second more than ours.
secs=$(( fields / 60 + 2 ))
tag="ram${ram}${oext}"

# shellcheck disable=SC2086
"$xroar" -machine cocous -ram "$ram" -bas bas13 $rom -no-machine-cart \
    -rompath "$root/roms" -ram-init clear -ui null -ao null -trace \
    -timeout "$secs" ${type:+-type "$type"} > "$out/$tag.xroar" 2>&1 || true
# shellcheck disable=SC2086
"$ours" -ram "$ram" $oext -fields "$fields" ${type:+-type "$type"} \
    > "$out/$tag.ours" 2> "$out/$tag.screen"
python3 "$root/tools/trace/trace-diff.py" "$out/$tag.xroar" "$out/$tag.ours"
