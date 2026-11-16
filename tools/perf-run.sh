#!/usr/bin/env bash
# perf-run.sh — workloads on the board, one boot each (design.md §15.5,
# §17 M7).
#
#   tools/perf-run.sh build/uart0/pico-coco.elf out/m7/t2         # every workload
#   tools/perf-run.sh build/uart0/pico-coco.elf out/m7/t2 idle scroll
#
# Each workload is its own boot: capture UART1, flash, type a BASIC
# program at the guest over the same UART and run it, let it run, keep
# the log. The heartbeat's `perf` line is the measurement, its `heartbeat`
# line the presents; perf-summary.sh reduces the logs. The card must hold
# Extended Color BASIC's ROMs.
#
# Measured in the mode that ships, with core 1 presenting while the guest
# runs. From M8 the guest is paced on the audio queue, and the
# heartbeat's audio line is the control: samples consumed a second at
# the PWM's 36,621 and no underrun; with PICO_COCO_AUDIO=OFF it is paced
# on the microsecond timer, and rt must read 1.000 in every row. Every
# program was run on the host first (test/host's guest harness), and none
# returns to the prompt.
#
# Adapted from pico-oric's. Each line is typed at uart-type.sh's pace, a
# character every 0.25 s.
set -euo pipefail

elf="${1:?usage: perf-run.sh ELF OUTDIR [WORKLOAD...]}"
outdir="${2:?usage: perf-run.sh ELF OUTDIR [WORKLOAD...]}"
shift 2
workloads=("$@")
[ ${#workloads[@]} -gt 0 ] || workloads=(idle compute scroll pmode4 set sound)

here="$(cd "$(dirname "$0")" && pwd)"
dwell="${PERF_DWELL:-45}"   # seconds each program runs: ~9 heartbeats

# One line per argument, each typed and entered on its own.
# scroll prints without a newline, so the screen scrolls a row every
# line printed; pmode4 draws lines in PMODE 4, the 256-pixel mode with
# artefact colours, clearing between passes; set plots random
# semigraphics 4 blocks over the text screen; sound plays a PLAY scale
# and a sweep of SOUND tones, over and over (M8).
program() {
    case "$1" in
        idle)    ;;
        compute) printf '%s\n' '10 A=0:FOR I=1 TO 1000:A=A+I*2.5:NEXT:GOTO 10' RUN ;;
        scroll)  printf '%s\n' '10 PRINT "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";:GOTO 10' RUN ;;
        pmode4)  printf '%s\n' '10 PMODE 4,1:SCREEN 1,1:PCLS' \
                     '20 FOR I=0 TO 191 STEP 8:LINE(0,I)-(255,191-I),PSET:NEXT:PCLS:GOTO 20' RUN ;;
        set)     printf '%s\n' '10 SET(RND(64)-1,RND(32)-1,RND(8)):GOTO 10' RUN ;;
        sound)   printf '%s\n' '10 PLAY "T8O2CDEFGAB;O3C"' \
                     '20 FOR T=1 TO 255 STEP 16:SOUND T,1:NEXT:GOTO 10' RUN ;;
        *) echo "perf-run.sh: unknown workload $1" >&2; exit 2 ;;
    esac
}

mkdir -p "$outdir"

# A failed flash or type exits under set -e; the capture must not outlive
# it, or it holds the port and every later run finds it busy.
logger=
stop_logger() {
    [ -n "$logger" ] || return 0
    kill "$logger" 2>/dev/null || true
    wait "$logger" 2>/dev/null || true
    logger=
}
trap stop_logger EXIT

for w in "${workloads[@]}"; do
    program "$w" >/dev/null           # an unknown name fails before the flash
    log="$outdir/$w.log"
    # The last capture's reader can outlive its kill by a moment, and
    # uart-log.sh refuses a busy port: retry until one is running.
    rm -f "$log"
    for try in 1 2 3 4 5 6 7 8 9 10; do
        "$here/uart-log.sh" 0 "$log" 2>/dev/null &
        logger=$!
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            sleep 0.5
            [ -e "$log" ] && break
            kill -0 "$logger" 2>/dev/null || break
        done
        if kill -0 "$logger" 2>/dev/null && [ -e "$log" ]; then break; fi
        kill "$logger" 2>/dev/null || true
        wait "$logger" 2>/dev/null || true
        logger=
        [ "$try" -lt 10 ] || { echo "perf-run.sh: no capture for $w" >&2; exit 1; }
    done
    "$here/flash.sh" "$elf" >"$outdir/$w.flash.log" 2>&1
    sleep 4                           # the card, then 1.3 s of the ROM's own boot
    while IFS= read -r line; do
        "$here/uart-type.sh" "$line\r"
        sleep 1
    done < <(program "$w")
    # Mark where the program started: heartbeats before this are typing.
    grep -ac ' perf ' "$log" >"$outdir/$w.start" || true
    sleep "$dwell"
    "$here/uart-screen.sh" >/dev/null 2>&1 || true
    sleep 1
    stop_logger
    echo "perf-run.sh: $w -> $log"
done
