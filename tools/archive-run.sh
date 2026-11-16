#!/bin/sh
# M11's archive run (design.md §15.4): every cartridge and tape fetched by
# tools/fetch-archive.sh, run on the host on a 64 KB CoCo with Extended
# Color BASIC 1.1 on Color BASIC 1.3, then summarised by
# tools/archive-summary.py.
#
#   tools/archive-run.sh [CORPUS] [OUT] [FIELDS]
#       CORPUS  default out/corpus (its ccc/ and cas/ are read)
#       OUT     default out/m11/archive
#       FIELDS  default 3600 (a minute of guest time at 60 Hz)
#
# Builds coco-corpus optimised in build/corpus, so that the run takes
# minutes. Leaves OUT/archive.tsv, a line per image, OUT/shots/ with each
# image's last frame (.png if sips is there, else .ppm) and text screen,
# and OUT/report.txt.
set -eu

corpus="${1:-out/corpus}"
out="${2:-out/m11/archive}"
fields="${3:-3600}"
here="$(cd "$(dirname "$0")" && pwd)"
jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

cmake -S "$here/.." -B build/corpus -DPICO_COCO_HOST=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/corpus -j --target coco-corpus >/dev/null
bin="$(pwd)/build/corpus/coco-corpus"

shots="$out/shots"
rm -rf "$shots"
mkdir -p "$shots"
n="$(find "$corpus/ccc" "$corpus/cas" -mindepth 1 -maxdepth 1 | wc -l | tr -d ' ')"
echo "archive: $n images, 64 KB, Extended 1.1, $fields fields, $jobs at a time"
find "$corpus/ccc" "$corpus/cas" -mindepth 1 -maxdepth 1 \
        \( -iname '*.ccc' -o -iname '*.rom' -o -iname '*.cas' -o -type d \) -print0 |
    LC_ALL=C sort -z |
    xargs -0 -n 8 -P "$jobs" "$bin" -f "$fields" -s "$shots" |
    LC_ALL=C sort >"$out/archive.tsv"
if command -v sips >/dev/null 2>&1; then
    for f in "$shots"/*.ppm; do
        sips -s format png "$f" --out "${f%.ppm}.png" >/dev/null 2>&1 && rm -f "$f"
    done
fi

python3 -I "$here/archive-summary.py" "$corpus" "$out/archive.tsv" | tee "$out/report.txt"
