# Milestone log

What each milestone verified, on which board, on what date, and what was
not checked, newest first. `design.md` §17 holds each milestone's scope
and done-when criteria; this file keeps the full record (EL §14.3).

## M3: the machine on the host

On branch `m3`, 2026-11-14 (`design.md` §8, §13, §15.2, §17). Host
only, besides the bench re-measured on the board.

**Built.** `src/core/coco.c`, the machine: the page table built from the
SAM's map, with RAM reached through the SAM's row and column and the
chips' organisation, a page taking the fast path only where its 256
bytes lie in order; the I/O page's decode; the PIAs' wiring; the
keyboard matrix; and the field loop in master-oscillator ticks, split
where FS falls. `mc6821.c`, the generic PIA, with tRS3 on its IRQ
outputs. `sam6883.h`, the SAM's register. `sha1` and `romset` from
pico-oric, with the owner's five ROMs. The Dragon 32 as the second
wiring: its 16 KB ROM, its PAL field, PIA1 port B and the PIA decode. In the CPU: `io_at`, the cycle
of an instruction a slow-path access falls on, and
`mc6809_lines_late`, for a change made in an instruction's last cycle.
On the host: `test/host/guest.c`, the harness, from pico-oric's;
`tools/trace/coco-trace.c`, `trace-diff.py` and `trace-coco.sh`, the
trace diff against XRoar. The bench now uses the machine's RAM.

**Checked on the workstation** (macOS, Apple clang 21.0.0; XRoar 1.13
headless, `af501b71`; the owner's `bas13.rom`, `extbas11.rom`,
`disk11.rom`, `bas11.rom` and `d32.rom`, whose CRC-32s are in XRoar's
list):

- **The CoCo boots its real ROMs**: Color BASIC 1.3 alone and with
  Extended Color BASIC 1.1, in 16, 32 and 64 KB, each to its banner and
  `OK` in 0.63–1.28 guest seconds, read from video RAM. Each answers
  `PRINT 2+3` with 5, typed through the matrix. Each takes every letter,
  digit and shifted symbol through the matrix and prints it back. Each
  gives `PRINT MEM`: 14631, 31015 and 31015 alone; 8487, 24871 and 24871
  with Extended (`test_boot`).
- **A 4 KB CoCo boots Color BASIC 1.1**, and BASIC 1.1 boots in 16 KB,
  each answering the same typed lines, with `PRINT MEM` 2343 and 14631
  (`test_boot`).
- **4 KB with BASIC 1.3 does not boot, and should not.** The ROM drives
  PIA0 PB6 low and reads the strap on PIA1 PB2, which a 4K machine ties
  low, and takes 64K mode, which 4K chips cannot serve. It hangs sizing
  memory. XRoar hangs the same way, and the trace diff holds the two
  together for ten seconds. 4K machines shipped with BASIC 1.0 and 1.1.
- **The Dragon 32 boots on the host**, the second wiring: Dragon BASIC
  to its banner and `OK` in 1.29 guest seconds in 32 KB, and expanded to
  64 KB. It answers `PRINT 2+3`, takes every letter, digit and shifted
  symbol through its own matrix and prints them back, and gives
  `PRINT MEM` 24871 (`test_boot`). Its field is XRoar's PAL one, 312
  lines with the first 287 (`design.md` §13.1).
- **The trace diff against XRoar runs through boot and typing.** Every
  configuration, 4, 16, 32 and 64 KB, alone and with Extended, agrees
  instruction for instruction, every register, for ten guest seconds:
  1.9–2.2 million instructions each, through `PRINT 2+3` and `PRINT MEM`
  typed. So do BASIC 1.1 in 4, 16, 32 and 64 KB, and the Dragon 32 in
  16, 32 and 64 KB, and all twelve again for 1,500 fields through a typed
  program (5.3–6.4 million instructions each). Fifty guest seconds of a
  program counting to 20,000 also agree on the Dragon 32 in 32 KB (10.7
  million instructions) and on BASIC 1.1 in 4 KB (10.9 million). Fifty guest seconds of a typed program printing `I;I*I;TIMER`
  300 times also agree: 10.9 million instructions on 64 KB with Extended,
  11.0 million on 16 KB. Typing goes in on both sides as XRoar's `-type`
  puts it in: POLCAT returns each character and the 1 ms delay returns at
  once, in no time. Each ROM's addresses are XRoar's for that image:
  POLCAT is `$A1C1` in BASIC 1.1 and `$A1CB` in 1.3, and the Dragon's
  are `$BBE5`, `$BBC5` and its key scan at `$851B`.
- **What the trace diff settled**, each by a divergence and its cure
  (`design.md` §7.1, §8.1, §8.3, §13.1, §18):

  | First divergence | Cause | Now |
  |---|---|---|
  | `LDA 2,X` reading `$8000`, no Extended: `$27` against `$FF` | An empty socket reads the floating bus | The byte at `$FFFF`, as a dead cycle leaves it |
  | Extended reading `$FF22`: PB0 high | Port B has no pull-ups | Undriven port B inputs read low |
  | 4K reading `$FF20`: PA0 high | The cassette input | Low with no tape |
  | Extended reading `$FF22`: PB1 low | The single-bit sound's feedback | High while silent (M8) |
  | Dragon reading `$FF22`, 23 instructions in: PB0 and PB1 high | XRoar's absent printer is never busy; its sound feeds PB1 back only once it has updated | PB0 low; PB1 low until the first FS rise or the sound enabled, then high (M8) |
  | The field interrupt an instruction early, then late | The CPU's latch at its last cycle, and tRS3 | Edges and rises seen a cycle late; tRS3 14 ticks |
  | `TST 2,U` and `ROL 2,U` on `$FF02` as FS falls | A read in the edge's instruction clears its flag | PIA accesses brought up to their cycle (`io_at`) |
  | Long runs, a read in an instruction's last cycle | The CPU has already latched the line | `mc6809_lines_late` |

  Along the way, a one-cycle phase fitted to the field matched the
  ten-second runs. It was tRS3 misread, and is gone. A filter in the
  diff tool skipped XRoar's lines containing `[`, so indirect
  instructions looked like divergences until it was dropped.
- **Bugs planted one at a time**, each against the trace diff (16 KB
  Extended, ten seconds, or the 50-second program) and the host tests:

  | Bug | Trace diff | Host tests |
  |---|---|---|
  | FS a line early | parts at 125,092 | pass |
  | No tRS3 | parts at 175,886 | pass |
  | No last-cycle rule | agrees ten seconds; parts at 1,037,379 in the 50-second run | pass |
  | Port B pulled up | parts at 124,839 | pass |
  | V and W swapped in the harness's matrix | (not its concern) | `test_boot` fails, every size |
  | The Dragon typed through the CoCo's matrix | (not its concern) | `test_boot` fails, the Dragon |
  | The Dragon's first field full length | parts at 258,247 | `test_machine` fails |
  | The Dragon on NTSC's field | parts at 258,173 | `test_machine` fails |
  | PB1 fed back from power-on | parts at 23 on the Dragon; the CoCo agrees | pass |
  | The Dragon's PB0 pulled up | parts at 23 | pass |
  | The expanded Dragon's PIAs repeating | (BASIC does not look) | `test_machine` fails |

  The PB1 plant shows why the Dragon earned its place in M3: no CoCo
  ROM reads the pin before the first field ends.

  Before the every-key line was added, the V and W swap passed
  `test_boot`, since `PRINT 2+3` and `PRINT MEM` type neither.
- **The host tests pass**, eight of eight: `test_machine`, with a
  dozen-instruction test ROM, needing no real ROM, covers the SAM's 16
  bit pairs, RAM through the SAM in 16K and 64K modes with the page bit
  and map type, the slow path where no chip answers, sixty FS interrupts
  in sixty fields with CB1 disabled as the control, 262 HS interrupts a
  field with CA1 disabled as the control, the Dragon's 312-line field and
  287-line first, its 50 FS and 3,120 HS interrupts, its PIA decode with
  and without expansion, the keyboard's rows and columns through PIA0,
  the copy and the run contract. `test_mc6821` covers the
  PIA from its datasheet, each rule beside a control. `test_romset`
  covers SHA-1, the table against `roms/README.md`, and each image found.
  The oracle, the behaviour tests and the bench still pass with the CPU's
  changes, and the bench's counts are unchanged.
- **Power-on**: the warm-start flag is `$71` against `$55`, which zeros
  fail; `RND`'s seed comes from the ROM. No seed is taken from RAM
  (`design.md` §13.3).
- Both firmware builds are clean with the machine in the core library.

**On the board**, 2026-11-14, the owner's Pico Plus 2 W: the bench, with
this milestone's CPU and the machine's RAM, 87.6 host cycles an
instruction on basic and 113.9 on the mix (M2: 88.1 and 113.3), 12.4 %
and 13.3 % of core 0, counts equal to the host's. So `io_at` and the
last-cycle rule cost nothing measurable. Capture in `out/m3/`, not
committed.

**CI** on GitHub, 2026-11-14, PR #4: green on both builds. The host job
(Ubuntu, GCC) ran all eight tests: `test_machine` passed on its test ROM,
`test_romset` passed what it can check without images, the oracle ran
against XRoar 1.13, and `test_boot` skipped, as it must with no ROMs.
So the boot and the trace diff are checked on the workstation only. The
firmware job built both images, with and without the UART. Green again
after the Dragon and 4 KB work (`a9cfc32`), with the Dragon's field and
decode in `test_machine`.

**Review**, 2026-11-15: the owner's review of PR #4, ten findings.

| Finding | Outcome |
|---|---|
| A SAM rate change partway through a slice overshoots it | Fixed: the instruction ends the slice (`mc6809_stop`). `test_machine` runs fast for 150 lines, then slow; unfixed, the first field ends 282,336 ticks in, not 238,944 |
| A PIA control write within a slice does not shorten it | Fixed likewise, as is an edge that schedules a rise. `test_machine` enables CA1 partway through a slice; unfixed, 33 line interrupts in that field, not 260 |
| The IRQ output ignores C2 once C1 is enabled | Taken from the datasheet: each flag gated by its enable. XRoar's `write_cr` behaves as the old code did; no trace tells them apart. `test_mc6821` covers both cases |
| A rise due after its interrupt is disabled still comes | Fixed: it stays down, as the datasheet gates it. XRoar's rise comes regardless; no trace tells them apart. `test_mc6821` |
| P1 applied in map type 1 | Open: XRoar applies it in both map types, as this core does; recorded in `design.md` §18, to settle from the MC6883 datasheet |
| An empty or short trace reports agreement | Fixed: either side empty, or XRoar's shorter than ours, fails |
| The trace driver's breakpoint judged interrupts from the lines now, not the latched ones | Fixed: `mc6809_fetches_next`, the core's own boundary decision |
| Fixed capacities written as literals | Moved to `config.h`: ROM, cartridge, PIA ports, the matrix |
| `map()` translates every byte of every page | Nine translations a page: the translation is a bit map. `test_machine` holds it to the byte by byte check in every size, mode, page bit and map type; seven bits checked instead of eight fails 768 pages |
| `romset_identify` reads as a per-image loop | Rewritten as a size check, one hash, one lookup |

After the fixes, all twelve configurations agree with XRoar for 1,500
fields with typing and a program, and the 50-second programs on 64 KB
Extended (10.9 million instructions), the Dragon 32 (12.9 million) and
4 KB BASIC 1.1 (10.9 million).

The stop costs on the board, measured on the owner's Pico Plus 2 W
against a control in the same sitting: a flag checked each step gave
90.1 and 116.0 host cycles an instruction on basic and the mix, the
control 87.6 and 113.2; the run's limit as a field the machine zeroes
gives 88.1 and 114.8, which is kept. Captures in `out/review/`, not
committed.

**Not checked:**

- Whether P1 applies in map type 1 (above).
- The Dragon's PAL field against its schematic: it is XRoar's
  placement, held by the trace diff (M14). Likewise PIA1 PB0 and the
  start of PB1's feedback, which are XRoar's behaviour.
- The Dragon's arrows, CLEAR and BREAK, as the CoCo's (M5).
- BASIC 1.0, and `dplus49b.rom` (DOSplus 4.9B, in XRoar's list), which
  is in `roms/` but not in the table until DragonDOS (M14).
- The SAM's fast rates (R0's address-dependent one runs at rate 0; M11)
  and its video counter (M4); open bus after the indexed modes whose last
  cycle before the access is not a dead one; PIA C2 pulse modes.
- Keyboard ghosting and the column lines driven back by rows; the
  arrows, CLEAR, BREAK, hold and gap (M5).
- HALT released with a line raised while halted (from M1).

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

**CI** on GitHub, 2026-11-14, PR #3: green on both builds. The host job
(Ubuntu, GCC) ran all four tests, `test_bench` among them, so the counts
in `bench.h` hold under GCC as under Apple clang, and the oracle ran
against XRoar 1.13, not skipped. The firmware job built the bench image
beside the emulator's, with and without the UART.

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
