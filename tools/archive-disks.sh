#!/bin/sh
# M13's disk archive run (design.md §15.4): every title fetched by
# tools/fetch-disks.sh started on the host from Disk Extended Color BASIC
# 1.1 on a 64 KB CoCo, and again under XRoar 1.13, and the two pictures
# compared at the same E cycle.
#
#   tools/archive-disks.sh [DISKS] [OUT] [FIELDS]
#       DISKS   default out/disks (its dsk/ is read)
#       OUT     default out/m13/archive
#       FIELDS  default 1800 (30 s of guest time): the capture is at
#               FIELDS fields' worth of E cycles from power-on
#
# coco-corpus reads drive 0's directory and says how the title starts
# (DOS, RUN or LOADM and EXEC; disk_entry in coco-corpus.c), or that it
# cannot tell, and runs it. XRoar is given the same disks and types the
# same command, and a watchpoint over the whole map, which XRoar's CoCo
# checks on every E cycle, writes a snapshot on the cycle our capture is
# taken at. XRoar would otherwise write a disk the guest wrote back over
# the archive's image, keeping the first as .bak: -no-disk-write-back
# keeps its writes in memory, as coco-corpus does. coco-corpus -sna draws that snapshot as it draws its own
# frames, and tools/disk-compare.py compares the two. XRoar is a tool
# here, never source (D2, D8): built by tools/trace/build-xroar.sh.
#
# The two disk models time a load differently, by seconds either way, so
# ours is compared with XRoar's picture at every field within 10 s of the
# capture, and the nearest match and its distance are reported.
#
# Leaves OUT/disks.tsv, a line per title; OUT/shots/<title>.{png,txt},
# ours, and <title>.xroar.{png,txt}, XRoar's; OUT/xroar/<title>.sna;
# OUT/report.txt.
set -eu

disks="${1:-out/disks}"
out="${2:-out/m13/archive}"
fields="${3:-1800}"
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
xroar="${XROAR:-$root/out/xroar/src/xroar}"
[ -x "$xroar" ] || { echo "no XRoar at $xroar: tools/trace/build-xroar.sh" >&2; exit 1; }

cmake -S "$root" -B build/corpus -DPICO_COCO_HOST=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/corpus -j --target coco-corpus >/dev/null
bin="$(pwd)/build/corpus/coco-corpus"

shots="$out/shots"
rm -rf "$shots"
mkdir -p "$shots"
n="$(find "$disks/dsk" -mindepth 1 -maxdepth 1 | wc -l | tr -d ' ')"
echo "disks: $n titles, 64 KB, Disk BASIC 1.1, capture at $fields fields, $jobs at a time"
# First, how each title starts (coco-corpus -entry runs nothing).
# Each step to a file and its status checked: under sh a pipeline's status
# is its last command's, so set -e would not see coco-corpus fail.
find "$disks/dsk" -mindepth 1 -maxdepth 1 -print0 | LC_ALL=C sort -z >"$out/titles.lst"
xargs -0 -n 16 -P "$jobs" "$bin" -entry <"$out/titles.lst" >"$out/entry.raw"
LC_ALL=C sort "$out/entry.raw" >"$out/entry.tsv"

# XRoar, a title at a time, as many at once as there are cores. Each job
# gets the title's name and command; its disks are found again as
# coco-corpus found them, in name order (LC_ALL=C). The snapshots are
# kept in OUT/xroar/ for the last pass.
snaps="$out/xroar"
rm -rf "$snaps"
mkdir -p "$snaps"
cycles=$((fields * 14934))   # COCO_FIELD_TICKS / 16 (config.h)
export xroar disks shots snaps cycles bin root
cat >"$out/xroar-job.sh" <<'JOB'
#!/bin/sh
name="$1"; cmd="$2"; os9="$3"
set --
if [ -d "$disks/dsk/$name" ]; then
    i=0
    list="$(find "$disks/dsk/$name" -mindepth 1 -maxdepth 1 -type f -iname '*.dsk' | LC_ALL=C sort | head -4)"
    IFS='
'
    for f in $list; do
        set -- "$@" "-load-fd$i" "$f"
        i=$((i + 1))
    done
    unset IFS
else
    set -- -load-fd0 "$disks/dsk/$name.dsk"
fi
cr="$(printf '\r')"
# XRoar takes a headerless image whose first sector looks like OS-9's
# LSN0 by that sector's shape; an RS-DOS disk can look so by chance, and
# its sectors are then not where RS-DOS asks (ViziDump, Knock Out). Our
# jvc_parse decides, as it does on the board: unless it took one of the
# title's disks as OS-9's, XRoar is told not to guess.
[ "$os9" = os9 ] || set -- -no-disk-auto-os9 "$@"
# A title that sends XRoar into a loop where time stands still would hold
# the run for ever: two minutes each, where timeout(1) is installed.
guard=""
command -v timeout >/dev/null 2>&1 && guard="timeout 120"
$guard "$xroar" -machine cocous -bas bas13 -extbas extbas11 -ram 64 -cart rsdos \
    -rompath "$root/roms" -ram-init clear -ui null -ao null -no-ratelimit \
    -no-disk-write-back "$@" -type "$cmd$cr" \
    -trap access=0-0xffff -trap-range "$cycles-$cycles" -trap-snap "$snaps/$name.sna" \
    -trap-timeout 0.01 -timeout 600 >/dev/null 2>&1 || true
if [ -s "$snaps/$name.sna" ]; then
    "$bin" -s "$shots" -sna "$snaps/$name.sna" "$name.xroar"
fi
JOB
chmod +x "$out/xroar-job.sh"
awk -F '\t' '{ printf "%s%c%s%c%s%c", $1, 0, $2, 0, $3, 0 }' "$out/entry.tsv" |
    xargs -0 -n 3 -P "$jobs" "$out/xroar-job.sh"

# Then ours, each picture compared with XRoar's within 10 s of the capture.
xargs -0 -n 4 -P "$jobs" "$bin" -f "$fields" -s "$shots" -ref "$snaps" \
    <"$out/titles.lst" >"$out/disks.raw"
LC_ALL=C sort "$out/disks.raw" >"$out/disks.tsv"
rm -f "$out/titles.lst" "$out/entry.raw" "$out/disks.raw"

# The report written first, so a failure stops before the pictures go.
python3 -I "$here/disk-compare.py" "$out/disks.tsv" "$shots" >"$out/report.txt"
cat "$out/report.txt"

if command -v sips >/dev/null 2>&1; then
    for f in "$shots"/*.ppm; do
        [ -e "$f" ] || continue
        sips -s format png "$f" --out "${f%.ppm}.png" >/dev/null 2>&1 && rm -f "$f"
    done
fi
