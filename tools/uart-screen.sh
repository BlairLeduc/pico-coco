#!/usr/bin/env bash
# uart-screen.sh — ask the firmware for the guest's screen as text
# (design.md §17 M7).
#
#   tools/uart-log.sh 10 out/screen.log &
#   tools/uart-screen.sh
#
# Sends FS (0x1C), which no key sends; core 0 logs the 16 rows of the
# text screen from RAM at the display offset (dump_screen in
# src/port/core0.c), so the capture shows what the panel should. Start
# uart-log.sh first: this only writes. Copied from pico-oric.
set -euo pipefail
UART_TYPE_DELAY=0 exec "$(dirname "$0")/uart-type.sh" '\034'
