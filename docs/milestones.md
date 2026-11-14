# Milestone log

What each milestone verified, on which board, on what date, and what was
not checked, newest first. `design.md` §17 holds each milestone's scope
and done-when criteria; this file keeps the full record (EL §14.3).

## M0: skeleton, both builds, CI

On branch `m0`, 2026-11-14. The layout of `design.md` §6 with what M0
needs and no emulation: CMake with a host build (`-DPICO_COCO_HOST=ON`,
CTest) and a `pico2` firmware build, both under `-Wall -Wextra -Werror`;
`src/core/config.h` and `hot.h`; `test/host/test_util.h` and
`test_skeleton`; the version header from `git describe` at build time;
`arm-none-eabi-size` printed on every build; CI building the host core
and its tests, the firmware, and the firmware without the UART;
`CLAUDE.md`, `THIRD-PARTY.md`, `roms/README.md`, `README.md`.

**Taken from pico-oric**, at its head of 2026-11-14 (`92fe8aa`) and
renamed `ORIC` → `COCO`: `cmake/`, `board.*`, `hot.h`, `test_util.h`,
`tools/build.sh`, `flash.sh`, `uart-log.sh`, `ci.yml`, `LICENSE`. `main.c`
is the Oric's M0 banner and heartbeat. Document references in their
comments changed to this project's sections.

**The siblings' files sorted.** Every tracked file in pico-atom and
pico-oric checked against `design.md` §5's lists; the ones the lists did
not name are sorted in a new table at the end of §5.

**Checked on the workstation** (macOS, Apple clang for the host; Arm GNU
toolchain 15.2.Rel1 and Pico SDK 2.3.1 from `~/.pico-sdk` for the
firmware):

- the host build is clean and `test_skeleton` passes, 1 of 1;
- **an SDK include in a core header fails the host build**:
  `#include "pico/stdlib.h"` planted in `src/core/config.h` stopped
  `test_skeleton.c` with `'pico/stdlib.h' file not found`, and the build
  was green again with it removed (EL §2.1);
- both firmware builds are clean. Sizes, `arm-none-eabi-size`:

  | Build | text | data | bss |
  |---|---:|---:|---:|
  | `build/pico`, UART on | 22,764 | 0 | 860 |
  | `build/pico-release`, UART off | 20,932 | 0 | 844 |

**On the board**, 2026-11-14: an RP2350B board (QFN-80, chip revision 2,
module id `7458DC82A89AAC12`) in the owner's PicoCalc, with a DS3231 on
`i2c0` at GP4/GP5. That clock takes UART1's pins, so a build option was
added, `PICO_COCO_UART_PORT`: 1, the default (D7), is UART1 on GP4/GP5;
0 is UART0 on GP2/GP3 through `UART_AUX` (hardware-notes.md §1.1). The
`-DPICO_COCO_UART_PORT=0` image (22,772 text, 860 bss), flashed over SWD
with `tools/flash.sh`, printed its banner and a heartbeat a second through
the Debug Probe: `build target : board=pico2 platform=rp2350`, `package :
QFN-80 (RP2350B), temperature on ADC input 8`, `clk_sys=150000000 Hz
clk_peri=150000000 Hz`, core rail ~1100 mV, die 18 °C, firmware
`ed8a9fa-dirty` (the uncommitted M0 tree). Capture in `out/m0/banner.log`,
not committed.

**Not checked:**

- CI on GitHub, which done-when needs green on both builds; recorded on
  the PR.
- The default UART1 build's banner on a board: this board's GP4/GP5 carry
  the clock. Over SWD, that image's UART1 was enabled at 115,200 baud on
  GP4/GP5 with its FIFO draining, but no capture was made.
- Which RP2350B board (Plus 2 or Plus 2 W): the banner cannot tell them
  apart.
- The host build with GCC on Linux, which only CI runs.
