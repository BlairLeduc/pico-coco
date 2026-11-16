#!/usr/bin/env bash
# perf-summary.sh — reduce perf-run.sh's logs to one line per workload
# (design.md §15.5, §17 M7).
#
#   tools/perf-summary.sh out/m7/t2 ...
#
# Averages the `perf` heartbeats taken after the program was started
# (perf-run.sh records where that was), dropping the first, which
# straddles the start. Core 0 busy is the share that counts: the guest,
# the keys and the snapshot, without the pacing wait. rt is the control
# until M8: it must read 1.000 in every row. Presents and dropped
# snapshots are their growth over the averaged windows, from the last
# heartbeat before them; the longest present is since boot.
#
# Adapted from pico-oric's.
set -euo pipefail

# The capture opens with line noise from the reset, which is not UTF-8.
export LC_ALL=C

for dir in "$@"; do
    for log in "$dir"/*.log; do
        case "$log" in *.flash.log) continue ;; esac
        w="$(basename "$log" .log)"
        start="$(cat "$dir/$w.start" 2>/dev/null || echo 0)"
        awk -v dir="$dir" -v w="$w" -v start="$start" '
            function num(re, skip_l, skip_r) {
                if (!match($0, re)) return ""
                return substr($0, RSTART + skip_l, RLENGTH - skip_l - skip_r) + 0
            }
            / heartbeat / {
                p = num("[0-9]+ presents", 0, 9)
                d = num("[0-9]+ dropped", 0, 8)
                f = num("[0-9]+ full", 0, 5)
                if (p == "") next
                # Each heartbeat comes just before the perf line of its window,
                # so n is one behind here: n <= start is a window dropped.
                if (n <= start) { p0 = p; d0 = d; f0 = f; next }
                r = num("rt [0-9.]+", 3, 0)
                sr += r; kr++
                if (kr == 1 || r < rmin) rmin = r
                if (kr == 1 || r > rmax) rmax = r
                p1 = p; d1 = d; f1 = f
                mx = num("max [0-9]+ us", 4, 3)
            }
            / perf / {
                n++
                if (n <= start + 1) next
                b = num("busy [0-9.]+%", 5, 1)
                g = num("guest [0-9.]+%", 6, 1)
                c = num("[0-9.]+ host cycles", 0, 12)
                t = num("[0-9.]+ cycles/insn", 0, 11)
                i = num("[0-9]+ insns", 0, 6)
                s = num("snapshot [0-9.]+%", 9, 1)
                if (b == "") next
                sb += b; sg += g; sc += c; st += t; si += i; ss += s; k++
                if (k == 1 || b < bmin) bmin = b
                if (k == 1 || b > bmax) bmax = b
            }
            END {
                if (!k) { printf "%-14s %-9s no steady heartbeats\n", dir, w; exit }
                printf "%-14s %-9s n=%d  busy %5.1f%% (%.1f-%.1f)  guest %5.1f%%  %6.1f host cyc/insn  %4.2f cyc/insn  %7.0f insns  snap %.2f%%  rt %.3f-%.3f  presents +%d (full +%d, dropped +%d), max %d us\n",
                       dir, w, k, sb / k, bmin, bmax, sg / k, sc / k, st / k, si / k, ss / k,
                       rmin, rmax, p1 - p0, f1 - f0, d1 - d0, mx
            }' "$log"
    done
done
