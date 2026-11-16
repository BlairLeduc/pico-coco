#!/usr/bin/env bash
# soak.sh — design.md §15.5's soak: 30 minutes with a BASIC program
# that prints on the text screen, draws in PMODE 4 with both colour sets,
# sounds SOUND and PLAY on the DAC, and reads the keyboard with INKEY$,
# while keys are typed over the UART; the UART captured throughout, then
# soak-check.py over the log (§17 M11). From pico-oric's, with the
# CoCo's program.
#
#   tools/soak.sh build/uart0/pico-coco.elf              # 30 minutes
#   tools/soak.sh build/uart0/pico-coco.elf 45 out/m11/soak
#
# Before running: the Debug Probe's SWD and UART connected, the Mac's
# display kept awake (caffeinate -d, hardware-notes.md §2.7), and the
# power switch on. The power source is not a condition (§15.5).
#
# The program is tools/soak.bas, run first on the host by test_soak. It
# prints "C <count> K <key>" at the screen's top twice a pass, the key
# the last INKEY$ read (72 for H, 74 for J). This script types H and J at
# it every few seconds through the firmware's key path (keymatrix), and
# asks for the screen now and then, so the log shows the program reading
# them; INKEY$ sees a key only while it is held, so some are not read.
# The southbridge is polled for the real keyboard all the while, which
# is what the I2C error count covers; press a few keys on it during the
# run as well, but not BREAK, which stops the program.
#
# The machine is the build's boot machine, a 64 KB CoCo with Extended
# Color BASIC unless the card's settings say otherwise; soak.bas needs
# Extended.
set -euo pipefail

elf="${1:?usage: soak.sh ELF [MINUTES] [OUTDIR]}"
minutes="${2:-30}"
outdir="${3:-out/soak}"
here="$(cd "$(dirname "$0")" && pwd)"
every="${SOAK_KEY_EVERY:-5}"     # seconds between typed keys

mkdir -p "$outdir"
stamp="$(date +%Y%m%d-%H%M%S)"
log="$outdir/soak-$stamp.log"

logger=
stop_logger() {
    [ -n "$logger" ] || return 0
    kill "$logger" 2>/dev/null || true
    wait "$logger" 2>/dev/null || true
    logger=
}
trap stop_logger EXIT

# Capture first, so the banner is in the log (CLAUDE.md).
"$here/uart-log.sh" 0 "$log" &
logger=$!
for _ in $(seq 1 20); do
    sleep 0.5
    [ -e "$log" ] && break
    kill -0 "$logger" 2>/dev/null || { echo "soak.sh: the capture did not start" >&2; exit 1; }
done

"$here/flash.sh" "$elf" >"$outdir/soak-$stamp.flash.log" 2>&1
sleep 5                          # the card, then the ROM's boot to OK
# One line at a time, a second after each, as test_soak types them.
while IFS= read -r line; do
    [ -n "$line" ] || continue
    "$here/uart-type.sh" "$line\r"
    sleep 1
done <"$here/soak.bas"
"$here/uart-type.sh" 'RUN\r'
sleep 2
# Where the program started, for soak-check.py: heartbeats before this
# are boot and typing.
grep -ac 'heartbeat' "$log" >"$outdir/soak-$stamp.start" || true
start=$(date +%s)
echo "soak.sh: running $minutes minutes from $(date +%H:%M:%S) -> $log"

end=$((start + minutes * 60))
n=0
while [ "$(date +%s)" -lt "$end" ]; do
    sleep "$every"
    if [ $((n % 2)) -eq 0 ]; then "$here/uart-type.sh" 'h'; else "$here/uart-type.sh" 'j'; fi
    n=$((n + 1))
    # The screen about once a minute, a few seconds after a key, so that
    # a pass has printed it; every 11th key, an odd count, so the dumps
    # follow H and J in turn. Letters go in lower case, which types the
    # CoCo's capitals without Shift (design.md §11.3).
    if [ $((n % 11)) -eq 0 ]; then
        sleep 3
        "$here/uart-screen.sh"
    fi
done
sleep 10                         # a last heartbeat after the last key
stop_logger

echo "soak.sh: $n keys typed"
"$here/soak-check.py" --minutes "$minutes" "$log" | tee "$outdir/soak-$stamp.txt"
