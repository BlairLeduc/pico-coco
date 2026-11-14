# Milestone log

What each milestone verified, on which board, on what date, and what was
not checked, newest first. `design.md` §17 holds each milestone's scope
and done-when criteria; this file keeps the full record (EL §14.3).

## M1: the 6809 on the host, the XRoar oracle

On branch `m1`, 2026-11-14. Host only: no board step belongs to M1
(`design.md` §17).

**Built.** `src/core/mc6809.c` and `.h`, a new MC6809E core in the
family's style (`design.md` §7.2): one switch per opcode page, the cycle
table from the datasheet, the indexed postbyte decoded by one inlined
function, flags in small helpers; IRQ and FIRQ as source masks, NMI as a
latched edge armed by any load of S; SYNC and CWAI polled through a
one-cycle latch, and skipped in one go when no poll could change
anything; HALT; undocumented opcodes trapped and counted, consumed as a
NOP (D12); `mc6809_run` as the seam's contract (§6). `src/core/coco.h`
and `bus.h` hold the page table and its fast path; the slow path is the
caller's until M3. `tools/trace/build-xroar.sh` clones XRoar at the 1.13
tag (`af501b71`) into `reference/` and builds it headless in `out/xroar`.
`test_mc6809_oracle` holds the core to XRoar's `mc6809.c` through
`test/host/xroar_cpu.c`; `test_mc6809_behaviour` covers what the oracle
cannot see. CI fetches the checkout and fails if the oracle skips.

**Checked on the workstation** (macOS, Apple clang 21.0.0, CMake 4.4.5;
Arm GNU 15.2.Rel1 and Pico SDK 2.3.1 for the firmware):

- **The oracle passes**, 3,424,027 comparisons: every one of the 266
  documented opcodes, 2,048 trials each over random registers, with every
  indexed postbyte and every `TFR`/`EXG` register pair eight times; 8,512
  more on the page table's fast path over a real 64 KB, memory compared
  whole; 39,680 interrupt trials (20,059 taken) against masks and NMI's
  arming; 27,007 SYNC and 26,958 CWAI trials, more than half with the
  lines changed at a chosen cycle of the wait, every instruction boundary
  compared to a horizon of 160 cycles; 3,999 resets. Compared: every
  register, NMI's arming, the cycle count, every write in order, and our
  reads as a subsequence of XRoar's. About 0.6 s.
- **No datasheet cycle count or flag rule was changed to pass.** The first
  run agreed on every instruction trial. What did not agree at first was
  the waits: a line raised during an interrupt entry, or as SYNC ended,
  was taken one instruction early. XRoar decides the next boundary on the
  lines latched before the entry; the core now does too (`design.md`
  §7.1). The rest of the early differences were the harness's own (code
  past the planted NOPs, a NOP run wrapping over the vectors).
- **The behaviour tests pass**: the run contract (never less than asked,
  never more than one instruction over, the returns summing to the CPU's
  count, `run(1)` over a 4-cycle instruction returning 4); reset; NMI
  held until S is loaded, not retaken while the level stays high, retaken
  on a second edge; FIRQ over IRQ, 3 bytes and E clear against 12 and E
  set, RTI by the stacked E, with E cleared by hand as the control; SYNC
  with IRQ masked and not, with a line already asserted (4 cycles), and
  not ended by an unarmed NMI; CWAI kept waiting by a masked FIRQ, ended
  by IRQ without stacking again, and by NMI through `CWAI #$FF`; HALT;
  the trap on each page, an undocumented postbyte and a mixed `TFR`, with
  a documented control beside each; `CLR` reading before it writes, with
  `STA` writing only as the control.
- **The skip is held to a stepping machine.** A program idling in SYNC
  with IRQ masked and in CWAI for FIRQ, over 400 line changes at random
  cycles, run once in slices with the skip and once one step or one poll
  at a time: registers, latch, cycle count and its counters equal at
  every change, after 100 or more wake-ups. The control, the stepping
  machine one cycle late, parts from it.
- **Bugs planted one at a time** in a scratch copy, each run against both
  tests:

  | Bug | Oracle | Behaviour |
  |---|---|---|
  | Half-carry from bit 3 instead of bit 4 | fails: 16 opcodes, 7,175 trials | passes |
  | A taken long branch a cycle short | fails: 14 opcodes, 14,647 | passes |
  | `n16,R` indexed a cycle short | fails: 56 opcodes, 3,641 | passes |
  | `MUL`'s C from bit 15 instead of bit 7 | fails: 1 opcode, 1,079 | passes |
  | NMI taken before S is loaded | fails: 12,026 | fails |
  | No post-entry latch (lines now, not latched) | fails: 89 | passes |
  | `CLR` writes without reading | **passes** | fails |
  | SYNC's skip ignoring the lines | **passes** | fails: the skip parts |
  | CWAI's skip ignoring the lines | **passes** | fails: the skip parts |
  | `mc6809_run` stopping 4 cycles short | passes | fails |

  The oracle steps one instruction at a time, so it never reaches the
  skip; and its read check is a subsequence, so a missing read passes it.
  Those are the behaviour tests' to catch, and they do.
- **The skip, without XRoar**: configured with no checkout, the oracle
  builds as a stand-in that CTest reports **skipped**, with the script to
  run in its output. Fetched fresh with `--checkout-only` into a scratch
  directory, the oracle built and passed.
- **The headless XRoar builds**: `tools/trace/build-xroar.sh` configured
  1.13 with no GUI, audio or OpenGL, built it, and ran `xroar -help`.
- **Both firmware builds are clean** with the core library compiled for
  the RP2350 under `-Werror`. The firmware does not link the CPU yet
  (M2), so its sizes are M0's: 22,772 text (UART on) and 20,940 (off).
- **The interpreter's size on the RP2350** (`-O3`, `arm-none-eabi-size`
  of the core library): 151,072 bytes at first, because `mc6809_step` and
  `mc6809_run` each inlined their own copy of the step. `mc6809_step` is
  now `mc6809_run(m, 1)`, which is the same thing, and the size is
  88,208, `mc6809_run` 71,000 of it. With the indexed decoder out of line,
  measured in a scratch copy, 36,160. Carried to M2 (`design.md` §7.5).
- **A published 6809 test of standing**: none found (`design.md` §18).

**CI** on GitHub, 2026-11-14, PR #2: green on both builds once the host
build's one GCC-only error was fixed (`test_util.h`'s counter unused in
the oracle; Apple clang does not warn). The host job fetched XRoar 1.13
(`af501b71`), and `test_mc6809_oracle` ran and passed in 0.74 s on
Ubuntu's GCC, not skipped; `test_mc6809_behaviour` passed beside it.

**Not checked:**

- Anything on a board: the CPU's speed and its counts against the host's
  are M2's gate.
- The HD6309: M15.
- What the CPU does after HALT is released with a line raised while
  halted: by a reading of its source, XRoar decides on the lines latched
  before the HALT, and the core decides on the lines then. Not run, since
  the oracle does not drive HALT;
  RS-DOS's HALT pacing in M13 is where it would show.
- The cycle within an instruction on which a slow-path access falls, for
  the PIAs (`design.md` §7.2): M3.
- The indexed decoder inlined against out of line, for speed: M2.

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

**On the board**, 2026-11-14: a Pimoroni Pico Plus 2 W, as the owner
identified it (RP2350B, QFN-80, chip revision 2, module id
`7458DC82A89AAC12`), in the owner's PicoCalc, with a DS3231 on
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

**CI** on GitHub, 2026-11-14, PR #1: green on both builds, the host
core and its tests (Ubuntu, GCC) and the `pico2` firmware with and without
the UART (Arm GNU 13.2.Rel1, Pico SDK 2.3.1).

**Not checked:**

- The default UART1 build's banner on a board: this board's GP4/GP5 carry
  the clock. Over SWD, that image's UART1 was enabled at 115,200 baud on
  GP4/GP5 with its FIFO draining, but no capture was made.
