# Milestone log

What each milestone verified, on which board, on what date, and what was
not checked, newest first. `design.md` §17 holds each milestone's scope
and done-when criteria; this file keeps the full record (EL §14.3).

## M2: the gate, the CPU alone on the board

On branch `m2`, 2026-11-14 (`design.md` §7.5, §17).

**Built.** `src/bench/`, adapted from pico-oric's: two 6809 workloads on a
flat bus, run by `test_bench` on the host and by a bench image,
`pico-coco-bench`, on the board. **basic** is Color BASIC's character
fetch in the direct page where the ROM copies it, walking a line of sums,
with a five-byte float add: written to the ROM's shape, not copied from
it. **mix** is 4,000 random documented instructions in a loop (254
distinct opcodes, every addressing mode, indexed postbytes drawn from all
the documented ones; the code ends at $65F0), with its code's pages
sending writes to the slow path so a random store cannot rewrite it. Each
run is ten guest seconds (600 NTSC fields), in slices of one field with
the overshoot carried, through `mc6809_run`. The CPU now counts the
instructions it executes (`mc6809_t.instructions`). A build option,
`PICO_COCO_IDX_OUTLINE`, calls the indexed decoder instead of inlining
it, for the measurement M1 carried.

**Checked on the workstation** (macOS, Apple clang 21.0.0; Arm GNU
15.2.Rel1 and Pico SDK 2.3.1 for the firmware):

- `test_bench` passes: basic stores 45, 45 and 65 as packed floats and
  makes 820 passes of its line in ten guest seconds; the mix runs no
  undocumented opcode and two runs from a fresh load end identical,
  registers and memory; each run ends within one instruction of its
  length; and the counts equal `bench.h`'s, which the firmware checks the
  board's against. With the decoder inlined and out of line, all four
  host tests pass, the oracle included.
- **Bugs planted one at a time** in a scratch copy: the digit 9 in the
  float table made 9.5 fails the sums (45.5 for statements 1 and 2); the
  mix's seed changed by one fails the counts (1,759,768 instructions for
  1,760,687). So equal counts on the board are evidence that the same
  code ran.
- Every firmware build is clean, with and without the UART and with
  UART0 for the owner's board. `pico-coco.elf` does not link the CPU yet,
  so its size is M0's.

**On the board**, 2026-11-14: the owner's Pimoroni Pico Plus 2 W
(RP2350B, chip revision 2, `7458DC82A89AAC12`) at 150 MHz, core rail
~1100 mV, die 18–20 °C, one core and the XIP cache to itself; images
built with `-DPICO_COCO_UART_PORT=0`, flashed over SWD, read over UART0.
Each image ran at least two passes of both workloads, and **every pass
of every image counted the host's cycles and instructions** (8,960,403
cycles; 2,118,076 instructions for basic at 4.230 cycles each, 1,760,687
for the mix at 5.089). Host cycles per guest instruction, and the share
of core 0 a guest at 0.895 MHz would take:

| Image | text | basic | | mix | |
|---|---:|---:|---:|---:|---:|
| tier 0, decoder inlined (the control) | 115,108 | 99.5 | 14.0 % | 863.7 | 101.3 % |
| tier 1, decoder inlined | 114,956 | 99.2 | 14.0 % | 866.0 | 101.5 % |
| tier 2, decoder inlined | 114,964 | 83.3 | 11.7 % | 104.9 | 12.3 % |
| tier 0, decoder out of line | 62,956 | 88.5 | 12.5 % | 537.9 | 63.1 % |
| tier 2, decoder out of line | 62,796 | 88.1 | 12.4 % | 113.3 | 13.3 % |

Pass to pass, each figure held to 0.02 %. `arm-none-eabi-nm` showed each
tier's functions where they belong: tier 1 moves `attention` (1.9 KB);
tier 2 also moves `mc6809_run` (71.0 KB inlined, 26.7 KB out of line),
`page2`, `page3` and, out of line, `ea_indexed`. The run loop's
disassembly at the default: the step inlined, one load and test of
`attn`, the fetch, the cycle table, a `tbh` dispatch. Captures in
`out/m2/`, not committed.

**The gate passes with the first lever applied** (`design.md` §7.5).
Against the estimate of 200–350 host cycles an instruction and 27–47 %
of core 0, the BASIC-shaped loop took 83–100 cycles. The mix, from flash,
took 864 and failed at 101 %: a 71 KB interpreter thrashes a 16 KB cache
when the program reaches every opcode. From SRAM both pass at 12–13 %.
So **the firmware's default tier is now 2**, and **the decoder is out of
line by default**: 34 KiB in SRAM, within `design.md` §16's 30–45 KB,
against 88 KiB for 6–8 % fewer cycles; and 63 % on the mix at tier 0
against 101 % should SRAM ever be short. A fresh build directory with no
options gave tier 2 and the decoder out of line, and on the board
repeated that row: 88.1 and 113.3. Neither 300 MHz nor the loop's shape
was needed. The finding is in `hardware-notes.md` §9.8 and
`emulator-lessons.md` §3.4.

**Not checked:**

- A second board: the Pico 2 W, which the release must also run on
  (`design.md` D3), and the UART1 build on a board without a clock.
- The shared cache: core 1 presenting, the audio IRQ and the card all
  fetch through it. M11 measures the tiers again with the whole emulator,
  as the Z80's did (`hardware-notes.md` §9.8).
- The real ROM's interpreter, whose opcode spread decides where between
  the two workloads a CoCo falls: M3 on the host, M7 on the device.
- The loop's bounds check on the opcode (`cmp #255` before the `tbh`),
  left because the opcode is held in 16 bits for the trap's record: two
  instructions per instruction, for M11.
- From M1, still: HALT released with a line raised while halted; the
  cycle within an instruction of a slow-path access (M3).

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
