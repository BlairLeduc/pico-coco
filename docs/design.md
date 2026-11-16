# pico-coco: design

A **Tandy Color Computer 1/2** emulator for the **ClockworkPi PicoCalc**,
written in C against the Raspberry Pi Pico SDK, which also runs the
CoCo's close relatives, the **Dragon 32 and 64**. It is a member of the
pico-atom family: pico-atom's architecture, its MC6847, its port layer and
its menu, with the 6502 replaced by a new **MC6809**, and later an
**HD6309**.

**The CoCo comes first** (D6). It is the machine the owner started on and
the one this project is for: it is the default machine, the first to boot
at every milestone, and the one whose behaviour settles a question when
the two families differ. The Dragon is included, and built second. XRoar
grew the other way round, from a Dragon, and where its source or its
defaults lean that way this design does not follow.

Companion documents in this directory:

- `hardware-notes.md`: the authority on the PicoCalc. Cited as **HW §N**.
- `emulator-lessons.md`: what the Atom, the Ace and the Oric taught. Cited
  as **EL §N**. This design follows it, and says so where it departs.
- `design-xroar-port.md`: the design this one replaces, a port of XRoar's
  own source. Superseded on 2026-11-14 and kept for its survey of XRoar and
  for the cost estimate that led here (D1).

The sibling projects are cited as **Atom** (`pico-atom`) and **Oric**
(`pico-oric`), with their design sections where they help. **XRoar** means
Ciaran Anscomb's emulator, which this project uses as its reference and
test oracle and never as source (§15.2).

Plain **§N** points within this document. Guest addresses are `$XXXX`, the
6809 world's convention, and host values `0x…` (EL §14.1). Dates are
written in full. **Estimates are labelled as estimates.** Guest hardware
facts in §3 are written from secondary knowledge and from a reading of
XRoar's source; each one that becomes a constant is first settled from a
primary source and recorded in §18 (EL §14.2).

**Status, 2026-11-14.** M0 and M1 are built: the skeleton, and the MC6809
core on the host, held to XRoar's (`milestones.md`). No machine yet.

**Contents**

1. [Goals and non-goals](#1-goals-and-non-goals) ·
2. [Decisions](#2-decisions) ·
3. [The guest machines](#3-the-guest-machines) ·
4. [Host target](#4-host-target) ·
5. [What comes from the siblings](#5-what-comes-from-the-siblings) ·
6. [Architecture](#6-architecture) ·
7. [CPU: MC6809, then HD6309](#7-cpu-mc6809-then-hd6309) ·
8. [Bus, SAM and PIAs](#8-bus-sam-and-pias) ·
9. [Video](#9-video) ·
10. [Audio](#10-audio) ·
11. [Keyboard and joysticks](#11-keyboard-and-joysticks) ·
12. [Media: ROMs, cartridges, tape, disc, snapshots, settings](#12-media-roms-cartridges-tape-disc-snapshots-settings) ·
13. [Timing](#13-timing) ·
14. [User interface](#14-user-interface) ·
15. [Testing](#15-testing) ·
16. [Memory budget](#16-memory-budget) ·
17. [Milestones](#17-milestones) ·
18. [Constants to confirm](#18-constants-to-confirm) ·
19. [Risks](#19-risks) ·
20. [Dropped and deferred](#20-dropped-and-deferred)

---

## 1. Goals and non-goals

**Goals**

- Run the stock ROMs of the **Tandy CoCo 1 and 2** (Color BASIC, Extended
  Color BASIC, Disk BASIC) well enough that BASIC, RS-DOS and the common
  body of CoCo software behave as on the real machine, artefact colours
  included; and the **Dragon 32 and 64** with their BASIC and DragonDOS
  to the same standard.
- A **6809 that is cycle-correct at instruction granularity** (EL §3.1),
  with interrupts, `SYNC`, `CWAI` and HALT as software depends on them, and
  the **6309** as a fitted option afterwards.
- **Real time with headroom at the stock 150 MHz clock, without PSRAM**, so
  one image runs on every RP2350 board the family supports (D3).
- Tape, cartridges, floppy discs (RS-DOS and DragonDOS), snapshots and
  settings from the SD card; the family's menu (D5).
- A **host-testable core** with no SDK header and no allocation, built
  under CTest on a workstation and in CI (EL §2.1).

**Non-goals for the first release**

- The CoCo 3, the MC-10, the Dragon 200-E, Alpha and Professional, and
  RAM expansions (§20).
- Bus accuracy inside an instruction. Where a title is found to need it,
  it is modelled at instruction granularity as the Ace's wait states were
  (EL §4.5), or recorded as not supported.
- A composite-video simulation. Artefact colours come from a table (§9.5).
- Shipping ROM images (§12.1).

**The engineering problem in one line.** The first design measured
nothing but estimated XRoar's per-cycle structure at 70–140 % of a core at
300 MHz. This design drops that structure for the one the family has
measured three times: a whole-instruction CPU on a page-table bus, devices
brought up to date lazily, and no framebuffer. The Atom's 6502 took
35–46 % of a core at 150 MHz (EL §1). The 6809 runs slower in instructions
a second, and should cost less (§7.5, an estimate, gated in M2).

---

## 2. Decisions

Numbered and dated (EL §14.7). **Settled** decisions are the owner's.
**Proposed** ones are this document's recommendation and need the owner's
word before the milestone that depends on them.

| # | Date | Decision | State |
|---|---|---|---|
| D1 | 2026-11-14 | A native emulator based on pico-atom, using its MC6847 code, with the 6502 replaced by a 6809/6309. Replaces the XRoar port (`design-xroar-port.md`). | Settled |
| D2 | 2026-11-14 | A **new CPU core** in pico-atom's style. XRoar's `mc6809.c` and `hd6309.c` are compiled only into a host test that holds the new core to them, instruction by instruction (§15.2). | Settled |
| D3 | 2026-11-14 | Target all RP2350 boards as pico-atom does: one `pico2` image at **150 MHz**, no PSRAM, for Pico 2, Pico 2 W, Plus 2 and Plus 2 W. 300 MHz only if the M2 gate asks. | Settled |
| D4 | 2026-11-14 | Start from pico-atom, and take pico-oric's later parts: the WD1793 and MFM track model, the split port files, loading a state as its machine, the corpus tools (§5). | Settled |
| D5 | 2026-11-14 | The family's menu, item for item and key for key, less what these machines lack and plus what they add (cartridges, joysticks) (EL §10). The family's `key = value` settings file on the card. This replaces the port design's D5. | Settled by the owner |
| D6 | 2026-11-14 | **The Tandy Color Computer comes first, and the Dragon is included.** Machines in order: **CoCo 1/2** (one machine with different ROMs and RAM), then **Dragon 32**, then **Dragon 64**. The default machine is a CoCo with Extended Color BASIC; a Dragon is the default only when the card has no CoCo ROMs. Where the families differ, the CoCo's behaviour is built and tested first. | Settled by the owner |
| D7 | 2026-11-14 | Standalone, as the family: boots straight into the guest; a menu item returns to the PicoCalc shell when UF2 Loader started the image (`picocalc_shell.h`, as Atom and Oric). Development build on UART1, GP4/GP5; release build with no UART. | Settled by the owner |
| D8 | 2026-11-14 | **XRoar is the reference emulator** (EL §11.4): built by a script from its own checkout at a pinned release, headless, never copied into the tree. Its source is also a secondary source for guest constants, each cross-checked against a datasheet, a schematic or the ROM executed (§18). | Settled by the owner |
| D9 | 2026-11-14 | Licence **GPL-3.0**, as pico-atom and pico-oric, whose code this starts from. XRoar is GPL-3.0-or-later, so compiling its CPU into a test tool is compatible. | Settled by the owner |
| D10 | 2026-11-14 | No ROMs shipped; identified on the card by SHA-1 (§12.1). No internal flash writes (EL §8.7). | Settled by the owner |
| D11 | 2026-11-14 | The picture is 256×192 at 1:1 at (32, 64), as the Atom's (EL §5.4). | Settled by the owner |
| D12 | 2026-11-14 | 6809 undocumented opcodes are **trapped and counted** first; the archive run decides whether any is implemented (EL §3.1, §11.6). | Settled by the owner |
| D13 | 2026-11-14 | The project is named **pico-coco**, after its first machine as the siblings are, with the card folder `/coco/`. The owner renamed the repository directory from `picocalc-xroar` when its GitHub origin was set up. | Settled by the owner |
| D14 | 2026-11-14 | PicoCalc findings go into `hardware-notes.md`, and what generalises into `emulator-lessons.md`, as learned, each written to stand alone (EL §14.1). One branch and pull request per milestone, CI green on both builds. | Settled by the owner |

---

## 3. The guest machines

Written from secondary knowledge and XRoar's source, to fix the shape of
the code. **Every figure here is a row in §18 until settled.**

### 3.1 What they are

The CoCo 1 and 2 are Motorola's reference design for these chips, and the
Dragon 32 is built from the same one:

| Part | Role |
|---|---|
| MC6809E | CPU, E clock about 0.89 MHz |
| MC6883 SAM | Clock generation, address decode, DRAM refresh, **the video address counter** |
| MC6847 VDG | Video: the chip pico-atom already models |
| 2 × MC6821 PIA | Keyboard, joystick comparator, cassette, 6-bit DAC, single-bit sound, VDG mode pins, cartridge interrupt, printer |
| ROM | CoCo: Color BASIC at `$A000` and, when fitted, Extended Color BASIC at `$8000`, 8 KB each. Dragon: one 16 KB BASIC at `$8000` |
| Cartridge port | ROM at `$C000` (Disk BASIC, a game), I/O at `$FF40`, lines to FIRQ, NMI and HALT |

A **CoCo** is 4, 16, 32 or 64 KB of RAM, with Color BASIC alone or with
Extended Color BASIC; the CoCo 2 is the same machine on a smaller board,
and its last revisions carry the MC6847T1 (§20). It was sold as an NTSC
machine, at 60 Hz, and its software uses NTSC artefact colours (§9.5).

The **Dragon 32** differs in its ROM, in the arrangement of the keyboard
matrix, in the printer port (parallel, where the CoCo's is bit-banged
serial) and in being a PAL machine at 50 Hz. The **Dragon 64** adds 64 KB
of RAM used as such, a second BASIC ROM selected by a PIA line, and a
6551 serial chip.

### 3.2 Address map

| Range | Contents |
|---|---|
| `$0000`–`$7FFF` | RAM (16 or 32 KB fitted on small machines; the rest reads as §8.1 says) |
| `$8000`–`$BFFF` | BASIC ROMs; RAM when the SAM's map type is 1 (64 KB machines) |
| `$C000`–`$FEFF` | Cartridge ROM; RAM in map type 1 |
| `$FF00`–`$FF1F` | PIA0, four registers mirrored |
| `$FF20`–`$FF3F` | PIA1, mirrored |
| `$FF40`–`$FF5F` | Cartridge I/O (the disc controllers) |
| `$FF60`–`$FFBF` | Unused on these machines |
| `$FFC0`–`$FFDF` | SAM control bits: each a pair of addresses, write one to clear and the next to set |
| `$FFE0`–`$FFFF` | Vectors, read from the BASIC ROM whatever the map type |

### 3.3 The PIAs, as wired

| Line | Use |
|---|---|
| PIA0 PA0–PA6 | Keyboard rows in (and joystick buttons) |
| PIA0 PA7 | Joystick comparator in |
| PIA0 PB0–PB7 | Keyboard columns out |
| PIA0 CA1, CB1 | **HS** and **FS** from the VDG: the line and field interrupts (IRQ) |
| PIA0 CA2, CB2 | Sound and joystick multiplexer select |
| PIA1 PA0 | Cassette in |
| PIA1 PA1 | Serial out (CoCo) or printer strobe (Dragon) |
| PIA1 PA2–PA7 | The 6-bit DAC: sound, cassette out, joystick reference |
| PIA1 CA2 | Cassette motor |
| PIA1 PB1 | Single-bit sound |
| PIA1 PB2 | RAM size strap (CoCo); ROM select (Dragon 64) |
| PIA1 PB3, PB4–PB6, PB7 | VDG **CSS**, **GM0–GM2**, **A/G** |
| PIA1 CB1 | Cartridge line: FIRQ |
| PIA1 CB2 | Sound enable |

### 3.4 Video, as wired

The VDG's data comes from RAM at an address **the SAM generates**, not the
VDG: the SAM holds a display offset (seven bits, in units of 512 bytes)
and a mode (three bits) that sets how its counter divides, so the picture
may start anywhere in memory and the SAM's mode need not match the VDG's.
Mismatched pairs are how the "semigraphics 8, 12 and 24" modes exist.

On the data bus, bit 7 drives the VDG's A/S pin and bit 6 its INV pin;
INT/EXT is tied to GM0. So in text mode a byte with bit 7 set is a
semigraphics-4 cell, or semigraphics-6 with GM0 high. This is not the
Atom's wiring (EL §4.2: model the chip, then wire it).

A field is 262 lines at 60 Hz on NTSC machines and 312 at 50 Hz on PAL
ones, of 57 CPU cycles each; 192 are active.

---

## 4. Host target

As pico-atom (Atom §3), by D3:

- **One image**, built for `pico2`, at 150 MHz, no PSRAM, running on a
  Pico 2, Pico 2 W, Plus 2 and Plus 2 W. The banner reports the physical
  board apart from the build target, and the die temperature is read from
  the channel the package has (HW §2.1, §8.1).
- LCD on `spi1` at 75 MHz, southbridge on `i2c1`, SD on `spi0`, PWM audio
  on GP26/GP27, with the family's drivers (§5).
- The rail is set explicitly at 150 MHz (HW §3). The 300 MHz path exists
  in the Atom's `board.c` and stays dormant unless the gate needs it.
- An RP2040 build is not planned (§20).

---

## 5. What comes from the siblings

By copy, rename (`atom_`/`PICO_ATOM_` and `oric_`/`PICO_ORIC_` →
`coco_`/`PICO_COCO_`), re-verify, tests brought along, attribution
kept, comments' document references changed to this document's sections
(EL §14.6). The siblings are read, never edited. Where both have a file,
the Oric's is the later version and is the one taken, unless noted.

**Copy** (no guest part in them)

| From | Files |
|---|---|
| Oric `src/port/` | `board`, `lcd`, `southbridge`, `kbd`, `audio`, `log`, `sd`, `card`, `diskio`, `storage`, FatFs configuration, `core0`, `core1`, `park`, `handoff`, `textpage` |
| Oric `src/core/` | `snappool`, `pcm`, `sha1`, `fattime`, `romset`, `hot.h`, `status`, the settings parser and rewriter, `keylayout` |
| Atom `src/core/` | `inflate` (gzip for tape images, if kept, §12.3) |
| Oric `tools/`, CI, `test/host/` scaffolding | build, flash, UART capture and typing, perf and soak scripts, `swd-counters.py`, `trace-diff.py`, the corpus runners, `mkfont.py` |
| `third_party/picocalc-shell/` | `picocalc_shell.h` |

**Adapt** (structure kept, guest parts replaced)

| From | File | What changes |
|---|---|---|
| Atom | `mc6847.c`, `mc6847.h`, `mc6847_font.c`, golden-image tests and `vdg-ppm.c` | The CoCo's wiring, which the Dragon shares (§3.4): SG4, INV on bit 6, the T1 variant's lower case later; rows addressed through the SAM (§9.2); artefact colours (§9.5) |
| Atom | `beeper.c` | From one bit to a level: the DAC, the single-bit output and the multiplexer (§10) |
| Atom | `bus.c`, `bus.h`, `atom.c`, `atom.h` | The machine: `coco.c`, with the SAM's map and the PIAs (§8) |
| Oric | `wd1793.c`, `mfmdisk.c`, `microdisc.c`, `discio.c` | The WD179x family for RS-DOS and DragonDOS, their latches and interrupt wiring, JVC and VDK images (§12.4) |
| Oric | `cassette.c`, `tape.c`, `tap.c`, `tapeio.c` | The CoCo's cassette format, which the Dragon shares, CAS files, the ROMs' block routines (§12.3) |
| Oric | `keymatrix.c`, `keymap_picocalc.c`, `keymapio.c` | The 8×7 matrix in its two arrangements (§11) |
| Oric | `snapshot.c`, `snapio.c`, `shot.c` | The fields (§12.5) |
| Oric | `menu.c`, `roms.c`, `settingsio.c`, `display.c`, `main.c` | Machines, ROM slots, cartridges, joysticks (§14) |
| Oric | `tools/trace/` | A driver for XRoar in place of Oricutron (§15.2) |

**Not used**: `m6502.*`, `via6522.*`, `i8255.*`, `i8271.*`, `uef.*`,
`ula.*`, `vsync.*`, `ay8912.*` (until a cartridge wants one), `portb.*`.

**New**: `mc6809.c` (§7), `hd6309.c` later, `sam6883.c` (§8.2),
`mc6821.c` (§8.3), `cart.c` (§12.2), `joystick.c` (§11.4), `cas.c`
(§12.3), and the XRoar oracle harness (§15.2).

Anything that changes core in the move is searched for `sleep_`
(EL §2.3).

**Sorted in M0** (2026-11-14): every file tracked in both siblings was
checked against the lists above, at pico-atom's and pico-oric's heads
of that date. Those the lists did not name are sorted here.

| List | From | Files | Why |
|---|---|---|---|
| Copy | Both | `cmake/pico_sdk_import.cmake`, `cmake/version.cmake`, `LICENSE`, `test_util.h` | No guest part. Taken in M0 |
| Copy | Oric | `shotio` | Screenshots of the panel, no guest part; the folder becomes `/coco/shots/` |
| Copy | Atom | `tools/test_mkfont.py` | Comes with `mkfont.py` |
| Adapt | Oric | `src/bench/bench.*`, `bench_main.c` | The gate's bench (§7.5): 6809 workloads on a flat bus, run by a host test and the bench image. Taken in M2 |
| Adapt | Oric | `shellargs` (in the Oric's working tree, not yet committed) | The shell's command line: its tape and disc switches take this machine's media. Taken once it is committed there |
| Adapt | Oric | `guest.c`, `sdk_sim/`, `snap_util.h`, `disc_util.h` | The host harness (§15.3); `sdk_sim` has no guest part and is copied whole. `guest.c` taken in M3 |
| Adapt | Oric | `render-diff.sh`, `layout-survey.*`, `uart-screen.sh`, `soak.bas`, `fetch-corpus.sh`, `disc-corpus.sh`, `corpus-summary.py`, `tools/corpus/`, `fetch-test-suites.sh` | Each reads the guest's screen, keys, media or test suite; `uart-hold.sh` and `uart-type.sh` are copied whole |
| Adapt | Atom | `mc6847_font.h`, `test_mc6847.c`, `test_mc6847_golden.c`, `vdg_scenes.*`, `test_present.c` | Go with `mc6847.c` (§9) |
| Not used | Oric | `font.*`, `font_fallback.c` | The emulator's pages use the VDG's internal set (§9.6), which is in the firmware, so no ROM font and no fallback |
| Not used | Oric | `bench_dormann.S`, `test/asm/` | 6502 suites and the Oric's test ROM; this project's test ROM is a 6809 one (§12.1) |
| Not used | Atom | `tools/trace/allegro/`, `atomulator-trace.c`, `build-atomulator.sh` | Atomulator's driver; XRoar's replaces it (§15.2) |

---

## 6. Architecture

The family's, unchanged. This section states it briefly and cites the
lessons for the reasons.

**Core and port** (EL §2.1). `src/core/` is portable C with no SDK header
and no dynamic allocation, all state in one `coco_t` or statically sized
buffers; `src/port/` is the only place SDK headers appear. Both build
under `-Wall -Wextra -Werror`, the core with the workstation's compiler
under CTest and the firmware with `arm-none-eabi-gcc`, in CI. Every fixed
capacity is in `src/core/config.h`, and `arm-none-eabi-size` is printed on
every build.

**The seam** (EL §2.2):

```c
void     coco_init(coco_t *m, const coco_config_t *cfg);
void     coco_power_on(coco_t *m);            /* after ROMs are loaded (EL §9.2) */
void     coco_reset(coco_t *m);
uint32_t coco_run(coco_t *m, uint32_t ticks); /* whole instructions; returns the true count */
uint32_t coco_run_field(coco_t *m);           /* one field, split at FS's edges (§13.1) */
void     coco_key_set(coco_t *m, int row, int col, bool down);
size_t   coco_audio_drain(coco_t *m, int16_t *dst, size_t max);
void     coco_video_snapshot(const coco_t *m, coco_frame_t *out);   /* §9.3 */
void     coco_copy(coco_t *dst, const coco_t *src);
```

`run` executes whole instructions until at least the time asked has
elapsed and returns the true count; the caller carries the debt. There is
no "exactly N" (EL §2.2). The machine is copied by function, which
rebuilds the page table (EL §2.2).

**Which core owns what** (EL §2.3):

| Core 0 | Core 1 |
|---|---|
| The guest: CPU and every chip | LCD presenting |
| Audio synthesis and the audio DMA IRQ | Southbridge I²C: keyboard, battery, backlight |
| Pacing on the audio queue | SD card work, menus, the log's UART end |

Core 1 never reads guest memory while the guest runs: everything it draws
arrives in a snapshot through the **three-buffer pool** (EL §2.4). Core 1
never sleeps, and core 0 never prints (EL §2.3). Card work, the menu,
pause and restart use the **park**: core 0 stops at a field boundary,
feeds the audio queue silence, and hands the machine to core 1 (EL §2.5).

The port's files are split from the first day as the Oric's are: `core0`,
`core1`, `park`, `handoff`, and one file per menu concern (EL §2.1).

---

## 7. CPU: MC6809, then HD6309

### 7.1 Accuracy

**Cycle-correct at instruction granularity** (EL §3.1): every documented
opcode in every addressing mode takes its datasheet cycle count, including
the indexed postbyte's extra cycles and long branches taken or not; the
condition codes are exact, half-carry and `DAA` included; and the
behaviour software leans on is modelled:

- **IRQ, FIRQ and NMI** as levels and an edge: IRQ and FIRQ as bitmasks of
  asserted sources, NMI latched on its edge and **not taken until S has
  been loaded** since reset. FIRQ stacks only PC and CC and clears E; IRQ
  stacks everything and sets E; `RTI` pulls by E.
- **`SYNC`** and **`CWAI`**: the CPU waits for an interrupt, and `CWAI`
  has already stacked its state. The run loop skips the wait as the Ace's
  HALT skip does (EL §3.4), which matters here: BASIC and games idle on
  `SYNC` waiting for the field interrupt.
- **HALT**, the cartridge line the RS-DOS controller uses to pace a
  transfer: a stall, as the tape trap's (EL §8.2), until the device's next
  event (§12.4).
- **Interrupts sampled at the instruction boundary**, with the last
  cycle's rule taken from the datasheet and checked against XRoar
  (EL §4.3: poll where the CPU does). **Settled in M3** by the trace
  diff: the CPU acts at a boundary on the lines as they stood at the
  start of the instruction's last cycle. A line that changes later,
  whether an edge or a PIA access made in that last cycle, counts from
  the boundary after (`mc6809_lines_late`), and the machine ends a run
  slice a cycle after each edge so this falls out.
- **Undocumented opcodes trapped and counted**, with the count and the
  last PC on the heartbeat (D12).
- **The interrupt latch, as XRoar models it** (settled in M1 by the
  oracle, 2026-11-14). In SYNC and CWAI the CPU polls once a cycle through
  a one-cycle latch: a line that rises during the wait is latched by the
  next cycle and acted on at the one after, so SYNC ends three cycles after
  the rise and CWAI vectors six after. Straight after an interrupt entry
  or the end of SYNC, the next boundary decides on the lines latched
  before it, not on the lines then: a line raised during an entry waits
  for the handler's first instruction. Elsewhere the lines at a boundary
  are taken as they are, which is what the chip's latch holds when a line
  changed during the previous instruction (§13.1, M3).

Per-cycle bus behaviour (dead cycles' addresses, the order of a 16-bit
write's two halves within the instruction) is not modelled except where
it reaches a device: a read-modify-write on a PIA does its read and its
write, in order, `CLR` included, which reads first as the chip does.

Where the datasheet calls a flag undefined, the core does what XRoar
does, since XRoar is the authority it is held to (EL §3.1): H after a
subtraction, a compare, `ASL` or `ASR` is left alone; `DAA`'s V is bit 7
of the result xor the new C (hoglet67's measurement, which XRoar cites);
`SEX` leaves V alone. `TFR` and `EXG` between registers of different
sizes, and with unused register codes, do what tlindner measured and XRoar
implements, and are counted as undocumented. So are the undocumented
indexed postbytes, which are decoded as the chip decodes them.

### 7.2 Implementation

A `switch`-dispatched interpreter with explicit cycle accounting
(EL §3.2), state in a struct, the bus's fast path inlined:

- three opcode pages (`$10` and `$11` prefixes), one `switch` each;
- **the indexed postbyte decoded by one function** returning the
  effective address and adding its cycles, shared by every indexed
  opcode. Written to be inlined; called out of line since M2's gate,
  which measured that the smaller interpreter is worth more than the
  call it saves (§7.5);
- 8-bit and 16-bit arithmetic through small `always_inline` helpers that
  compute the flags, so each rule is written once;
- the step function `always_inline` into the run loop, and the loop's
  disassembly read after any change to it (EL §3.4);
- the cycle of an instruction on which its operand access falls recorded
  on the bus's slow path only, so a PIA access can be placed in time
  (EL §4.3). RAM pays nothing. Built in M3 (`mc6809_t.io_at`): counted
  back from the instruction's end in the datasheet's cycle sequences, as
  XRoar models them, with a flag for the last cycle. The bench measured
  no cost (87.6 and 113.9 host cycles against M2's 88.1 and 113.3).

### 7.3 The HD6309

A second `switch` set, selected at power-on by the machine page (a 6309
fitted is a hardware choice, and a state records it, EL §8.5): the extra
registers (E, F, W, V, MD), the new instructions (`TFM`, the inter-register
group, bit operations, `MULD`, `DIVD`), **native mode**'s shorter cycle
counts and longer interrupt stack frame, and the illegal-instruction and
divide-by-zero traps. In 6809 emulation mode it runs 6809 code with the
6309's differences from the NMOS part, as the real chip does.

It is a milestone of its own, after the first release's machines work on
the 6809 (§17). The 6809 core is written so that the 6309 shares its
helpers and bus, not its dispatch.

### 7.4 Tests

- **The oracle** (D2, §15.2): every opcode in every addressing mode,
  over randomised registers, operands and memory, run on this core and on
  XRoar's `mc6809.c` in one host program, comparing registers, every
  memory write, and the **cycle count**, which XRoar's per-cycle bus
  callback gives exactly. This is the cycle table asserted by execution
  (EL §3.3). The same for `hd6309.c`, in both modes.
- **Behaviour tests** of the project's own for what an instruction-level
  comparison cannot see: NMI arming, FIRQ against IRQ, E and `RTI`,
  `SYNC` and `CWAI` with each interrupt masked and not, HALT, the run
  contract, the skip held to a stepping machine register for register
  (EL §3.4). Each rule beside a control that must fail.
- **Planted bugs**, one at a time in a scratch copy, to show the oracle
  bites: a wrong half-carry, a long branch one cycle short, an indexed
  mode's cycle count, a flag `MUL` leaves (EL §3.3).
- **A published 6809 test program**, if one of standing exists (§18):
  fetched by script, never committed, and a skip is not a pass.

### 7.5 Budget: an estimate, and the gate

The E clock is about 0.89 MHz and a 6809 instruction averages perhaps
four to five cycles, so about **200,000 instructions a second**, a third
of what the Atom's 6502 runs. A 6809 instruction does more (16-bit
operations, the postbyte), so allow 200–350 host cycles each against the
6502's measured 158–218 (EL §1). That gives **40–70 M host cycles a
second: 27–47 % of core 0 at 150 MHz. This is an estimate**, and EL §1
records such estimates wrong by 3× both ways.

**The gate (M2)** is the Ace's (EL §14.3): the CPU alone on the board, on
a flat bus, running a slice of the oracle's instruction mix and a loop
shaped like BASIC's interpreter, reporting host cycles per instruction,
with the board's cycle and instruction counts equal to the host's. The
threshold is a **projected 60 % of core 0 at 150 MHz**; the Atom shipped
at up to 46 % with zero underruns. If it fails, the levers in order
(EL §3.4): SRAM placement in measured tiers, the run loop's shape, then
300 MHz as a user's option (D3).

**The interpreter's size, measured in M1** (2026-11-14, Arm GNU 15.2.Rel1,
`-O3`, Cortex-M33): 88,208 bytes of text, `mc6809_run` 71,000 of it,
with the indexed postbyte's decoder inlined at every indexed opcode as
§7.2 has it. With that decoder out of line it is 36,160. Both are well
over the 16 KiB XIP cache and over the Oric's whole interpreter (EL
§3.4). No estimate was written for this. The gate measures speed with
the decoder inlined and out of line before choosing, and the SRAM tiers
are sized from whichever wins.

**Measured at the gate, M2** (2026-11-14, the owner's Pimoroni Pico Plus
2 W at 150 MHz, Arm GNU 15.2.Rel1 `-O3`, one core, the XIP cache to
itself; `src/bench/`, `docs/milestones.md`). Two workloads ran ten guest
seconds each, every pass with cycle and instruction counts equal to the
host's: **basic**, Color BASIC's character fetch and a float add, at
4.23 E cycles an instruction; and **mix**, 4,000 random documented
instructions in a loop, at 5.09. Host cycles per instruction, and the
share of core 0 a guest at 0.895 MHz would take:

| Build | basic | | mix | |
|---|---:|---:|---:|---:|
| Tier 0, decoder inlined | 99.5 | 14.0 % | 863.7 | 101.3 % |
| Tier 1, decoder inlined | 99.2 | 14.0 % | 866.0 | 101.5 % |
| Tier 2, decoder inlined | 83.3 | 11.7 % | 104.9 | 12.3 % |
| Tier 0, decoder out of line | 88.5 | 12.5 % | 537.9 | 63.1 % |
| **Tier 2, decoder out of line** | **88.1** | **12.4 %** | **113.3** | **13.3 %** |

Against the estimate of 200–350 host cycles and 27–47 % of core 0, the
BASIC-shaped loop took 83–100 cycles, at a third of the estimate's
share. The mix found what the estimate did not consider: from flash, an
interpreter several times the XIP cache's size thrashes it when the
program reaches every opcode, and 864 cycles an instruction fails the
gate. **With the interpreter in SRAM, both workloads pass at 12–13 %.**
The gate passes with the first lever applied: **tier 2 is the firmware's
default**, and **the decoder is out of line**, which puts 34 KiB in SRAM,
within §16's budget, against 88 KiB for 6–8 % fewer cycles. Out of line
is also the better build if SRAM is ever short: 63 % on the mix at tier
0 against 101 %. Neither 300 MHz nor the loop's shape was needed. M11
measures again with core 1 presenting, which shares the cache (HW §9.8).

---

## 8. Bus, SAM and PIAs

### 8.1 The page table

The Atom's (EL §4.1): 256 pages of two pointers, NULL for the slow path,
flags in a separate byte array. RAM and ROM are one indexed load. The
I/O page `$FF` is slow-path and decoded there: the PIAs **by mask**, so
their mirrors work; the SAM's bit pairs; cartridge I/O; and the vectors
at `$FFE0`–`$FFFF`, which read from ROM in either map type.

The table is rebuilt only when the bits that choose the map change
(EL §4.1): the SAM's map type and page bit, the Dragon 64's ROM select,
a cartridge's presence. Each page's flags say which array it points into,
so a copy can rebuild the table.

What an unpopulated address reads (a 16 KB machine above its RAM, the
unused I/O ranges, a cartridge port with nothing in it) is taken from the
schematic, not assumed `$FF` (EL §4.1), and matters at once: both ROMs
size memory by writing and reading back.

**Modelled in M3 from XRoar, and held by the trace diff** (2026-11-14):
RAM space that no chip answers reads `$FF`. An empty ROM socket, the
empty cartridge slot and unused I/O read the data bus as the last cycle
left it. This core does not model dead cycles, but the cycle before a
direct, extended or 5-bit-offset indexed access is one, with `$FFFF` on
the address bus, so those read the ROM's byte there, `$27` for BASIC 1.3.
Color BASIC's sizing read of `$8000` with no Extended BASIC fitted is
where the trace first needed it. The modes whose last cycle before the
access is another are not modelled. The schematic remains the primary
source (§18).

### 8.2 The SAM

`sam6883.c` models the chip's register as the generic part (EL §4.2): 16
bits set and cleared through address pairs, giving the VDG mode bits, the
display offset, the page bit, the two rate bits, the memory size and the
map type. From it the machine derives three things:

- **the memory map** (§8.1);
- **the video address for each scanline** (§9.2);
- **the CPU's rate.** At rate 0 every cycle is 16 ticks of the master
  oscillator. With R0 set (the "high-speed poke" CoCo software uses) the
  rate is **address-dependent**: accesses to ROM and I/O take 8 ticks and
  RAM accesses 16. With R1 set every cycle is 8 ticks and the video is
  no longer refreshed correctly.

Machine time is therefore counted in **oscillator ticks**, not CPU
cycles. At rate 0 an instruction's time is its cycles × 16, and the run
loop is the plain one. While a faster rate is selected, a **second copy
of the run loop** is used, as the Ace's is while a trap is set
(EL §3.2), in which the bus adds each access's ticks from a per-page
cost; the CPU's internal cycles are charged at the rate of the
instruction's own address. How close that comes to the SAM's real rule,
including the stretched cycle on each change of rate, is settled against
XRoar's traces and recorded (§18); the display in the all-fast rate shows
what the snapshot finds and is not modelled as noise.

### 8.3 The PIAs

`mc6821.c` is the generic MC6821 (EL §4.2): two ports, each with a data
direction register and a control register selecting it; **output latches
apart from input pins**, so a read returns pins on input bits and the
latch on output bits; CA1/CB1 edge flags with their polarity and enable;
CA2/CB2 as inputs or in each output mode; the flags cleared by a read of
the data register; IRQA and IRQB out. Its datasheet is its test plan.

The wiring of §3.3 is a table in `coco.c` on top. Three points:

- **HS and FS are brought up to date lazily** (EL §4.3). FS's edges are
  where the field is split (§13.1). HS falls 262 or 312 times a field; its
  flag is computed from the tick count **when PIA0's control or data
  register A is read**, and only while software has enabled its interrupt
  does the run slice stop at each line. BASIC leaves it disabled. As
  built in M3, any PIA access first brings HS, FS and the outputs up to
  the cycle it falls on (§7.2). So a read of `$FF02` in the instruction
  that FS falls during clears a flag set before it, as XRoar has it.
- **The IRQ output rises tRS3 after an enabled edge** (the datasheet's
  figure 13, 1 µs; XRoar's 14 ticks), and a data read lowers it. In
  XRoar, and so here, the delayed rise comes even if a read cleared the
  flag first. Settled in M3 by the trace diff: the field interrupt's
  timing under Extended BASIC is the edge plus tRS3 plus the CPU's latch
  (§7.1). A one-cycle phase fitted first matched the same runs and was
  this delay, misread. The output itself is each flag gated by its
  enable, as the datasheet's IRQ logic has it: a control write sets it to
  (IRQ1 and CR0) or (IRQ2 and CR3, C2 an input), and a rise due after an
  interrupt was disabled does not come. XRoar's `write_cr` keeps the
  output up while C1's enable is set and leaves C2 unexamined, and its
  rise comes regardless; no ROM run so far tells the two apart, and the
  datasheet is taken (review of M3, 2026-11-15).
- **A slice ends early when its stop points move.** A slice is sized
  from the SAM's rate, CA1's enable and the next rise due; an
  instruction that changes any of them (a rate change, a PIA control
  write, an edge that schedules a rise) ends it, and the run sizes the
  next (`mc6809_stop`, §6). Measured on the board against a control in
  the same sitting: 88.1 and 114.8 host cycles an instruction on the
  bench, against 87.6 and 113.2 without it (2026-11-15).
- **One register, several meanings** (EL §4.2): PIA1's port B carries
  the VDG mode with the single-bit sound and a strap. The machine compares
  the bits that matter to each consumer.
- **Reset** resets both PIAs and the SAM with the CPU; which chips are on
  the reset line is read from the schematic (EL §3.2).

### 8.4 Hooks

The printer port and the serial bit are modelled to the extent the ROMs
need to see them idle or busy; output is discarded and counted (§20). The
Dragon 64's 6551 is present as registers that read as an idle chip, to
the extent its ROM touches it at boot (EL §4.2: fit what the ROM
touches).

---

## 9. Video

### 9.1 No framebuffer

As the Atom (EL §5.1): the picture is a function of video RAM and a few
mode bits, so core 0 publishes a small snapshot and core 1 generates rows
from it straight into two RGB565 line buffers. What changes from the Atom
is that **the SAM chooses the bytes** and the mode can disagree between
the two chips.

### 9.2 Rows through the SAM

`mc6847.c` keeps its job: decode a row of bytes into pixels for a VDG
mode, through a lookup table per mode (EL §5.2). It gains a caller-supplied
**row source**: for scanline *y* of 192, which bytes. `sam6883.c` supplies
it from the SAM's mode: how many bytes a row has, and after how many
scanlines the address advances (every 12, 3, 2 or 1). When the two chips
agree this reduces to the Atom's nine modes. When they do not, the result
is what the hardware shows: the semigraphics 8, 12 and 24 modes are a
text-mode VDG fed a new row of bytes every 3, 2 or 1 scanlines (SAM
modes 2, 4 and 6; this read "4, 3 or 1" until M4 executed it).

The text generator gains the CoCo's wiring (§3.4): semigraphics 4 as
well as 6, chosen by GM0; inverse on bit 6; the character set from the
Atom's verified table, with its layout asserted against the data
(EL §5.5). The MC6847T1's lower case, for later CoCo 2s, is a variant
table and a machine option (§20).

**As built in M4** (2026-11-15). A row source of "bytes a row, and lines
before it advances" cannot describe the mismatched pairs exactly, so the
SAM's **video address counter** is modelled instead (`sam6883.c`): B15–B0
as a ripple counter, loaded from F at FS's rise, clocked by the VDG's
fetches, with the X divider between B3 and B4 and the Y divider between
B4 and B5 chosen by V, and B1–B4 or B1–B3 cleared at each fall of HS. The
divisions and clears are the datasheet's table; the edges are XRoar's
model, from Stewart Orchard's research for the SAMx4 (§18). The VDG
fetches 32 bytes a line or 16 by its own mode, and in XRoar it goes on
fetching through the right border, 42 or 22 in all, which the counter
counts: in some mismatched pairs that moves the X divider (§18). The
renderer then decodes **one line of fetched bytes** at a time, the row
within a text cell being the line modulo 12; every pair of modes renders
through the one path. A change of V while the counter runs, and the
glitches XRoar models for it, is not modelled (§9.4). The VDG's address
takes neither the page bit nor the second bank, as XRoar's fetch has it
(§18).

The wiring of a byte (§3.4) is as listed: bit 7 to A/S, bit 6 to INV, and
INT/EXT from GM0. With INT/EXT high and A/S low there is no external
character ROM on these machines, and the VDG shows the byte's own bits
as the row, as XRoar has it. The alphanumeric ink with CSS high is the
datasheet's "bright orange", a twelfth colour index; drawn as orange.

### 9.3 The snapshot

Published at the end of each field, **where the next active line begins**
(EL §5.6):

- the VDG mode byte (A/G, GM0–GM2, CSS) and the SAM's video mode;
- **the bytes the SAM would fetch this field**, copied from guest RAM
  starting at the display offset, wrapped at 64 KB: at most 6,144;
- a few status bytes (tape, drive, the machine).

The pool is three such buffers, about 19 KB. Copying 6 KB costs core 0
tens of microseconds a field. **Measured in M7** (2026-11-15, the
board, tier 2): about 565 µs a field, 3.4 % of core 0, walking the SAM's
counter run by run; 3.17 % with that path moved to SRAM, so the cost is
the work. M11's perf pass takes it up.

**As built in M4** (2026-11-15): `coco_frame_t` holds the VDG's mode, the
SAM's mode and display offset, the field number, and **for each of the
192 active lines the 32 bytes the VDG fetched on it** (16 used in the
16-byte modes, the rest zero): 6,144 bytes, filled by
`coco_video_snapshot` walking the counter of §9.2 a 16-byte run at a
time, from RAM as it is where the next active line begins. The status
bytes come with the status line (M7). `present.c` is the presenter's
plan, in the core so the host can test it: a change of the VDG's mode,
or `present_invalidate` from the caller when the palette, dark
background or artefacts change, repaints every band; otherwise a band
is sent across the columns whose bytes changed. `snappool.c` is
pico-oric's pool. The port's half, core 1 drawing into the panel, is
M7's. Before BASIC sets PIA1 port B's direction the VDG reads its mode
pins low, as XRoar's PIA has an undriven input, so the first field is
alphanumeric.

**The mode belongs in the shadow** (EL §5.3): the presenter compares the
snapshot's modes with the presented ones first, and a change marks every
band dirty and rebuilds the table. Otherwise it diffs bytes against the
shadow in bands of 8 rows with a column span, and sends each dirty band
as one LCD window.

### 9.4 Changes of mode within a field

Some titles change CSS or the mode part-way down the screen. A snapshot
with one mode cannot show that. The machine therefore **records each
change of the video bits with the scanline it fell on**, computed from
the tick count at the write, into a short list in the snapshot. Nothing
is clocked per line to do it (EL §4.3).

In the first version the presenter uses the mode in force at the first
active line and **counts fields that had more than one**, on the
heartbeat and in the archive run. Whether to render split fields, with a
table per region, is decided by that count, not by expectation
(EL §4.2).

**As built in M4** (2026-11-15): the count, not yet the list. A write to
PIA1 port B or to the SAM's V or F that changes the video bits while the
VDG is on an active line, by the tick of the write, marks the field, and
`coco_t.split_fields` counts the fields so marked. The list of changes is
built if the archive run's count asks for split rendering.

### 9.5 Colour, artefacts and the border

- The palette is the Atom's `mc6847_palette`, with the mono palette from
  the datasheet's luminance levels (EL §5.2), chosen by a setting.
- **Artefact colours.** On NTSC machines the two-colour 256-pixel mode
  with the black-and-white colour set shows pairs of pixels as colours,
  and CoCo software draws with them. This fits the row table directly: a
  byte is four pixel pairs, and each pair maps to black, white or one of
  two colours. A setting chooses off, or one of the two phases (which
  colour is which depends on how the machine powered on, and titles
  offered a "press reset until red" screen). **It is on by default on a
  CoCo**, since that is how the machine looked, and off on PAL machines,
  which do not show it.
  XRoar's finer table, which looks across byte boundaries, is not built
  (§20).
  **As built in M4** (2026-11-15): XRoar's simple renderer's rule. Each
  pair of pixels from the line's start, both black or white, is black,
  blue, red or white by the pattern 00, 01, 10, 11 (left then right),
  and the two phases are XRoar's two composite inputs, KBRW (that order,
  XRoar's default for NTSC machines) and KRBW. Only RG6 with CSS high has
  single pixels to pair, so only its table takes them; the mono palette
  shows none. The colours are XRoar's simple table's, a sky blue and an
  orange. The setting itself comes with the menu (M9).
- **The backgrounds.** XRoar's VDG, and so this one, gives dark green
  behind alphanumeric cells with CSS low, dark orange with it high, and
  dark green behind the two-colour graphics modes with CSS low; black
  with CSS high and behind semigraphics. A setting draws the dark colours
  as such or as black (the Atom's), black by default.
- **The border** is drawn only when its colour changes (EL §5.4).

### 9.6 Geometry, status, pages

The 256×192 picture at (32, 64) on the 320×320 panel, a full repaint
11.5 ms (HW §4.7). A perf line above and a status line below, each drawn
when its text changes and each hidden by a setting (EL §5.4). Tearing is
accepted (EL §5.4).

**As built in M7** (2026-11-15): pico-atom's layout, which this section
had the other way round until then; the siblings agree, and a user of
more than one should find the lines where they were. The border is
pico-atom's frame, 32 pixels at the sides and 48 rows above and below,
so it stops 16 rows short of the panel's top and bottom; each text line
is 40 cells of the VDG's own 8×12 glyphs in those 16 rows, at rows 2 and
306, the screen's green on black. The border is drawn, and the perf
line on, until the settings of M9 can turn either off; the status line
is blank until it has something to say (M9). The perf line reads in
capitals, the VDG having no lower case.

The missing-ROM page (§12.1) is the first page: a `coco_frame_t` in the
alphanumeric mode, written by `textpage.c` with the bytes Color BASIC
writes (bit 6 set, clear for inverse), which `test_boot` holds to a
booted ROM's own screen, line byte for line byte through the snapshot.

The menu and the other pages are guest text pages through the guest's own
renderer, in the VDG's own character set (EL §5.8, §10), as the Atom's.
The VDG's internal set has no lower case, and neither does the Atom's
menu.

Golden images in every mode and colour set, and the mismatched pairs,
are committed only after they have been looked at, and compared with
XRoar's rendering of the same memory (§15.3).

---

## 10. Audio

**Sources.** A 6-bit DAC on PIA1's port A; a single-bit output on PIA1
PB1; the cassette's signal; a cartridge's sound line. PIA0's CA2 and CB2
select which analogue source reaches the speaker and PIA1's CB2 enables
it. The DAC is also the joystick's reference, so software scanning a
joystick moves it with the sound source switched away.

**Synthesis.** The Atom's beeper is a box filter over a one-bit level at
the exact rational sample period (EL §6.1). It already multiplies a
level by a time, so it becomes a filter over **the speaker's level**: the
machine computes that level from the DAC value, the single bit, the
multiplexer and the enable **on every write that can change it**, stamped
with the tick count at the start of the writing instruction, and the
filter integrates between changes. The cost stays per change, not per
sample. A one-pole DC blocker follows; silence is 0 (EL §6.1).

The sample period is a reduced rational of oscillator ticks, recomputed
at power-on for the machine's crystal (§13.2). Fixed point throughout.

**Plumbing and pacing** are the family's without change (EL §6.2, §6.3,
HW §5.3–5.4): 36,621 Hz at 150 MHz, two chained DMA channels over an
aligned ring, the refill path in SRAM at priority `0x40`, an SPSC queue of
compare words, **core 0 paced on the queue**, underrun samples and late
refills counted apart, mute still consuming, the late path forced once
(EL §6.4). Bring-up runs on timer pacing first (EL §6.3).

**Tests** (EL §6.1): every sample against an independent model built from
the edges a guest program made, to 1 LSB; the pitch of the ROM's `SOUND`
and of a `PLAY` note against the cycle count of their loops; and whether
the idle ROM makes any sound at all, since its keyboard and joystick scans
write the DAC's port.

---

## 11. Keyboard and joysticks

### 11.1 The path

The family's (EL §7.1): held-key state from press and release events,
canonical codes, the binding fixed at press time, the whole FIFO drained
each poll on core 1, and key events **replayed into the matrix at the
guest's pace**. The hold and gap for each ROM are settled by executing
it, with a control that loses keys, and by typing a long program and
counting the lines stored (EL §7.1). Modifier presses go a field ahead of
their key, and releases wait the same minimum.

**Settled in M5** (2026-11-15, `test_keyboard`). Color BASIC scans the
keyboard in the foreground, while it waits for a key, not on an
interrupt, and does not repeat a held key. Typed straight into the
matrix from 240 phases across the field, Color BASIC 1.3 takes a key
held 2 fields, and 1.1 and Extended Color BASIC 1.1 need 3; all three
need a field with the key up before it is typed again. Of the phases,
only a few thousand ticks lose a key held a field too few: 48 phases
missed Extended's. The replay holds a key 4 fields and leaves 2 up, a
field more of each than the slowest ROM needs (`config.h`), about 10
keys a second. A 60-line program typed through it is stored whole in
each ROM; typed at each ROM's bare minimum it was stored whole too (59
of 60 in Extended at 2 fields), so BASIC storing a line and scrolling
costs no key here, unlike the Oric's; at 1 field held, the control, 7 to
10 lines were stored. While a program runs, BASIC's BREAK check keeps
the last key it saw for `INKEY$`, and nothing reaches the prompt after.

### 11.2 The matrix

Eight columns driven from PIA0 port B, seven rows read on port A. The
CoCo and the Dragon use the same keys in **two arrangements** of the
rows; the machine selects the table. Both are **settled by executing each
ROM**: press every cell at the prompt, with and without SHIFT, and read
what the ROM writes to the screen (EL §7.2), kept as a regression test
that types every table entry through the real ROM.

### 11.3 The mapping

Data, one row per PicoCalc code: row, column and flags (EL §7.2), checked
by a host test. The points particular to these machines, as built in
M5 (`keymap_picocalc.c` holds the table and its reasons):

- **Typing is by character.** The PicoCalc sends `"` and the guest types
  it as SHIFT+2: each such entry asserts or removes the guest's SHIFT
  while the key is down. `:` and `@`, which the PicoCalc shifts and the
  CoCo does not, take SHIFT away; `=`, `*` and `+` add it. The latest key
  down that says anything about SHIFT decides it, and a key that changes
  it waits a field behind the change. `^` and `_` are UP and SHIFT+UP,
  the codes BASIC reads; `[`, `]` and `\` are SHIFT with DOWN, RIGHT and
  CLEAR. `{`, `}`, `|`, `` ` `` and `~` have no key.
- **Lower case.** These machines have upper case only, shown inverse when
  "lower case" is toggled (SHIFT+0). PicoCalc letters type the guest's
  letters whatever their case.
- **BREAK** is Esc, and **CLEAR** is Tab, plain keys that no swallowed
  Shift chord can take (HW §6.3). Backspace and Del are ←.
- **The guest's arrows** are the PicoCalc's. The guest's shifted arrows
  (SHIFT+← erases a line on the CoCo) cannot come from Shift+arrow, which
  sends nothing for Left and Right, so **Alt with an arrow** is SHIFT
  with it; Alt+0 is SHIFT+0 (lower case) and Alt+`@` SHIFT+@ (pause a
  listing). The keys the PicoCalc sends only with Shift are their base
  key's cell with SHIFT: End is SHIFT+←, PgUp and PgDn SHIFT+↑ and ↓,
  Home SHIFT+CLEAR, Insert SHIFT+ENTER, Break SHIFT+BREAK. Either Shift
  alone is SHIFT, for games. Ctrl reaches nothing, and a key pressed
  with it types nothing: the CoCo 1 and 2 have no CTRL. That Alt+0
  arrives as `0` with Alt held is read from the MCU's source, not yet
  seen on a device (M7).
- **The emulator's keys** are on Alt and the function keys, which the
  guest lacks, and the Alt layer cannot be taken by a game layout
  (EL §7.2).
- **Game layouts** are overlays, built-in ones generic and named by
  shape, per-title ones text files on the card (EL §7.3). The built-ins
  are chosen by surveying the archive, not guessed (EL §7.3). They come
  with the keyboard joystick in M12; M5 copied no layout code.
- **The Dragon's arrangement** reaches the mapping in M14. Until then
  the host harness types into a Dragon by M3's table, straight into the
  matrix (§17: the Dragon is frozen).

### 11.4 Joysticks

Two analogue sticks, each two axes read by comparing against the DAC
through PIA0 PA7, and a button each on a keyboard row line. The PicoCalc
has none, so a **keyboard joystick**: the arrows drive one stick's axes
to their ends or centre, and a key the southbridge delivers alongside the
arrows is fire (`]`, EL §7.3). It is a layout, chosen in the menu's KEYS
item like any other: off, right stick, left stick. The comparator is
brought up to date when PIA0 is read, from the DAC's value then.

Since the button shares a matrix row, the joystick layout and the
keyboard interact as on the real machine; the sweep test of §11.2 is run
with a button held.

---

## 12. Media: ROMs, cartridges, tape, disc, snapshots, settings

### 12.1 ROMs

No permission covers them, so none ships (EL §8.1, D10). The user puts
them in `/coco/roms/`; the README names each file, its size and its
SHA-1. The emulator **identifies ROMs by SHA-1** and knows the common
alternate packagings (the CoCo's two 8 KB ROMs as one 16 KB file, the
Dragon 64's two ROMs). A missing ROM gives a page naming the files, never
a blank screen, and the About page shows every slot's file, status and
hash (EL §8.1).

**As built in M7** (2026-11-15): core 1's ROM job hashes every file in
`/coco/roms/` and reads again the ones the CoCo boots from: Color
BASIC 1.3, or 1.1 without it, for $A000, and Extended Color BASIC 1.1
for $8000. With both of Extended's, the machine starts; otherwise the
page names the two files and says which are missing, and, if the card
has a Color BASIC alone, ENTER starts that. A card put in while the page
is up is read again and the machine starts, as pico-oric's M7 found it
must. Only an image whose SHA-1 is known loads: no file is taken by its
name alone. The combined 16 KB file and the About page wait for the
Machine page (M9).

The hashes are taken from the owner's images and cross-checked with
XRoar's own list of known ROMs (its CRC-32s) before they are constants
(§18). Host tests that need the ROMs find them in a gitignored staging
directory and skip without them; CI runs what needs no ROM, including the
whole CPU suite and a small test ROM the project assembles (EL §11.2).

### 12.2 Cartridges

`cart.c`: a ROM image mapped at `$C000`, and the cartridge line that
makes the machine start it (on the real port, the Q clock wired to the
cartridge-detect pin, which reaches FIRQ through PIA1 CB1). The menu
inserts and removes one with a power-on (EL §10's machine page). The DOS
controllers are cartridges with a chip behind them (§12.4). Sound and
other active cartridges are deferred (§20).

### 12.3 Tape

Both families of machine use one cassette format: a leader, then blocks
with a sync byte, type, length, data and checksum, as two tones a bit.
The image format is **CAS**, the bytes of that stream.

**Phase 1: the trap** (EL §8.2). On each ROM's **block-read and
block-write routines**, found by reading the ROMs and guarded by their
first bytes, so another ROM is never trapped. The CPU stalls there while
the port serves the request with the guest parked; the trap leaves the
machine where the routine is as it finishes its last byte and **lets the
ROM run its own epilogue**, so what the routine leaves is the ROM's work
(EL §8.2). A request no file answers is declined, or given up to the
prompt where the ROM would otherwise wait in silence; which, is settled
by what each ROM does when declined (EL §8.2). The file the user meant is
found by the name in its first block when no file of that name exists.

**Phase 2: the signal** (EL §8.3). The CAS bytes become half-cycles
clocked in oscillator ticks and presented on PIA1 PA0, brought up to date
only when that port is read, with the run slice ending at the deck's next
edge. How the ROM times a bit is read from the ROM first. The motor
follows PIA1 CA2, the machine's own relay line. Turbo while the tape
plays (EL §9.3). Recording decodes what the ROM drives onto the DAC back
into whole blocks appended to the image (EL §8.3); the player's edges are
held to a recording of the ROM's own save, edge for edge.

The Atom's gzip reader comes along only if the archive's CAS files are
found gzipped (§15.4).

### 12.4 Discs

Two controller cartridges, each a Western Digital FD179x-family chip
behind a latch, wired differently:

| | RS-DOS (CoCo), built first | DragonDOS |
|---|---|---|
| Chip | WD1793 at `$FF48`–`$FF4B` | WD2797 at `$FF40`–`$FF43` |
| Latch (drive, motor, density, side, precompensation) | `$FF40` | `$FF48` |
| INTRQ | NMI, gated by the latch | NMI, gated by the latch |
| DRQ | **HALT**: the CPU is held until the next byte is ready | The cartridge line: FIRQ through PIA1 CB1 |

The decodes are as XRoar's source has them; the registers are read off
each DOS ROM before they are constants (EL §8.4, §18).

The chip is the Oric's `wd1793.c`, made the generic part with the
variants' differences (the 2797's side-select output, the status bits
that differ), and its design is kept whole (EL §8.4): **the disc turns in
oscillator ticks**, a search waits for its ID to come round, a transfer
moves a byte a byte-time and loses one the CPU missed; every timed step
is an event the run loop stops at; the chip holds one raw track and
**asks the port for a track at a time**, served with the guest parked.
RS-DOS's HALT pacing is the CPU's stall (§7.1) ended by the chip's
next-byte event.

Images: **JVC/DSK** (CoCo) and **VDK** (Dragon), sector images from which
`mfmdisk.c` builds tracks; a read-only file is a protected disc; a short
image reads zeros past its end and grows when written (EL §8.4). DMK,
which stores raw tracks, is deferred (§20).

What READY and the motor mean to each DOS, and how each notices a disc
change, are read from the DOS ROMs (EL §8.4). The archive's images are
surveyed before any check the chip makes is trusted, and the whole
archive is held to XRoar by screen, since XRoar's disc timing differs
(EL §8.4).

### 12.5 Snapshots

The Oric's container and rules (EL §8.5): fields written explicitly,
little-endian, with a header, version, lengths and CRC; zero is every
field's reset state; ROM hashes, not bytes; everything a power-on chooses
recorded (machine, RAM, CPU type, cartridge, television standard); the
media's names carried; a **two-pass load into the machine the state
names**; every field checked against the range a running machine keeps;
a busy device never blocks a load; compared straight after the load as
well as 150 fields on.

Importing XRoar's snapshot format is a separate decision (EL §8.8), and
deferred (§20).

### 12.6 Settings and the card

```
/coco/
  coco.ini         settings: key = value (EL §8.7)
  roms/  tapes/  discs/  carts/  snaps/  keymaps/
```

The family's settings file and rules without change (EL §8.7): every
default in one function, a wrong line changes nothing, saving is a
deliberate menu action that **edits the user's text in place**, the
output parsed back before it is written. Every write to the card goes
through `name.new` and a rename with recovery on load (EL §8.6). No
flash writes.

---

## 13. Timing

### 13.1 The field

`coco_run_field` splits the field at FS's edges, so guest instructions
run while FS is in its flyback state and the field interrupt is a level
with a duration (EL §5.6). FS falls as line 230 begins, counted from
power-on (13 lines of blanking, 25 of top border, 192 active), and rises
as the next field's line 0 begins, as XRoar counts them. The trace diff
holds this core to XRoar's field interrupts for 50 guest seconds (M3).
The line counts are the MC6847 datasheet's for NTSC, and the PAL machines' extra lines are placed as the circuit
places them, **read from the schematic and not from XRoar's numbering**
(EL §5.6). The snapshot is taken where the next active line begins.

The Dragon 32 runs, from M3, on XRoar's PAL field so that the trace
diff can hold it: 312 lines, every one with HS, the VDG held for 25
lines at its line 254 and again at its line 0, so FS falls 255 lines
into the field; the first field after power-on lacks the leading 25
and is 287 lines. That is XRoar's numbering, held to the boot trace and
not yet to the schematic; M14 settles it (§18).

A guest loop on the host must see FS low and escape once a field, and a
redraw begun at the field interrupt must be complete in every snapshot,
with the wrong field shape as a control that fails (EL §5.6).

**As built in M4** (2026-11-15): `coco_run_field` runs on past FS's rise
to the next field's first active line (line 38; on the Dragon's PAL field
63, 25 held and 38 of the VDG's), and the snapshot is taken there; the
first field after power-on runs from tick 0. `test_video` holds the
rules above with a guest loop and a 58-line redraw, and snapshots taken
where FS rises as the control. The trace diff agrees as before, since the
split adds a boundary and moves no instruction.

### 13.2 Clocks

One master oscillator per machine: 14.31818 MHz on NTSC machines, and
whatever the PAL machines carry (§18). Everything is counted in its ticks
(§8.2), and **the tick count is the only clock** (EL §4.3). The values
that depend on it (ticks a line, lines a field, the audio sample period,
tape half-cycles, the disc's byte time) are computed once at power-on and
kept where they are used (EL §9.3). The field rate is 50 or 60 Hz by
machine and is a constant once settled (EL §9.1).

No faster guest clock is offered beyond what the SAM's own rate bits give
software (§8.2, §20).

### 13.3 Power-on state

RAM is zero-filled, with the two exceptions looked for (EL §9.2): state
the ROM uses as a seed, and a check that zeros pass. Both BASICs' `RND`
seeds and their cold- and warm-start flags are read from the ROMs before
M3 and the finding recorded either way. **Found in M3** (2026-11-14,
Color BASIC 1.3 and Extended 1.1, executed): the warm-start test is
`$71` against `$55`, which zeros fail, so the machine cold-starts. The
cold start copies `RND`'s seed from the ROM (`$A132`) to `$0115`, so
no seed is taken from RAM, and both give 50 and 32 for their first two
`RND(100)`. ROMs are loaded, then the machine
is powered on, in one documented order the host harness shares
(EL §9.2).

---

## 14. User interface

The family's (EL §10), by D5:

- **Boots straight into the guest.** One Alt chord opens the menu, which
  parks the guest; function keys open its pages directly.
- **Pages**: Tapes, Discs, Snapshots, Keys (layouts and the keyboard
  joystick), Machine, Settings, About. Where the Atom and the Oric have
  an item, it is in the same place under the same key.
- **The Machine page** stages changes and applies them by a restart that
  behaves as a power-on: machine (CoCo, Dragon 32, Dragon 64), BASIC on
  a CoCo (Color BASIC alone, or with Extended), RAM (4, 16, 32, 64 KB),
  CPU (6809; 6309 from its milestone), television standard where the
  machine was sold in both, cartridge (none, a ROM file, RS-DOS,
  DragonDOS). ROMs are checked on the card first, and it
  refuses while an unsaved recording exists.
- **Pause**, **New tape**, a **status row** naming the first problem, the
  **About page** with the version from `git describe` at build time
  (EL §10).
- **Return to the shell**, when UF2 Loader started the image (D7).
- The firmware names no title (EL §10).

---

## 15. Testing

### 15.1 Shape

No framework; one binary per area under CTest; exit 77 is a skip and a
skipped CPU test is an unverified CPU; behaviour asserted by execution;
controls that must fail; bugs planted one at a time and recorded
(EL §11.1). Hardware-independent logic lives in the core where the tests
reach it.

### 15.2 XRoar as oracle and reference

Built by `tools/trace/build-xroar.sh` from its own checkout at a pinned
release, outside the tree, with no GUI (EL §11.4). Used three ways:

1. **The CPU oracle** (§7.4): `mc6809.c` and `hd6309.c` linked into a
   host test with a flat memory behind their bus callback. A tool, with
   its own CTest label, run in CI, which clones XRoar.
2. **Trace diff of the whole machine**: the same ROMs and key script
   under this core and under XRoar, registers before each instruction,
   diffed; the first divergence is almost always the bug. XRoar is exact
   per cycle and this core per instruction, so interrupts can land an
   instruction apart: the driver resyncs and classifies what lay between,
   or makes the two keep the same time, whichever the first runs show is
   needed (EL §11.4). Errata found in either are carried by name.
3. **Pictures and archives**: XRoar's rendering of the same video memory
   beside the golden images (§9.6), and the disc archive held to it by
   screen (§12.4).

The reference is got building in M1, the milestone that first needs it,
and kept in use (EL §11.4).

### 15.3 The real software on the host

A harness builds the real machine on the workstation, finds ROMs by
SHA-1, types through the same matrix path as the firmware, and reads
results as text decoded from video RAM (EL §11.2): boot each machine,
type a sum, sound a note and measure it, save and load through the ROM's
own routines, round-trip a tape through an independent decoder
(EL §11.3), run each DOS against an in-memory disc, restore a snapshot
mid-session.

### 15.4 The archive

Every tape, cartridge and disc image of the CoCo archive, and then the
Dragon's, run
on the host for a fixed guest time, each with its last frame and a
one-line report (EL §11.6): undocumented opcodes reached and the
instructions before the first (D12); fields with more than one video mode
(§9.4); which keys titles test and which joystick they read (§11.3);
whether CAS files come gzipped; the largest file, which sizes the
buffers. Failures are classified before any is fixed.

### 15.5 On the board

The gate (§7.5); the registers and tick count at the end of each of the
first 150 fields, equal to the host's (EL §13.3); scripted workloads
typed over the UART, each feature measured against a control build in
one sitting, the previous release measured beside the new (EL §12); a
30-minute soak whose check shows the workload kept running, on the
development build and, with counters read over SWD, on the release build
(EL §11.5).

---

## 16. Memory budget

**Estimates**, replaced by the linker map as each milestone lands
(EL §14.4). The Atom's whole image came to about 255 KB of 520 KB
(EL §1); this machine is the same shape.

| Region | Estimate |
|---|---:|
| Guest RAM | 64 KB |
| ROMs: BASIC 16 KB (two on a Dragon 64), cartridge or DOS up to 16 KB | 48 KB |
| Snapshot pool, 3 × ~6.3 KB, and the presenter's shadow | 26 KB |
| Renderer's table (8 KB worst case) and line buffers | 10 KB |
| Tape buffer | 64 KB |
| One raw disc track and its build buffer | 16 KB |
| Interpreter and hot callees in SRAM, if the tiers earn it; **34 KB at tier 2, measured in M2** (§7.5) | 30–45 KB |
| Audio ring and queue, log ring, key queues | 16 KB |
| SDK, FatFs, drivers, stacks | 30 KB |
| **Total** | **about 305–320 KB of 520 KB** |

No PSRAM is used on boards that have it (D3). Both stacks are painted
and their high-water marks reported from bring-up (HW §2.3).

---

## 17. Milestones

Each ends with something that runs and something that is measured, and
is recorded in `docs/milestones.md` with the date, the board, the
compiler, what was checked and by whom, and a not-checked list carried
forward (EL §14.3). One branch and pull request each (D14).

```
 host only                                 device
 M0 skeleton, both builds, CI ───────────────────────────────┐
 M1 6809 on host, XRoar oracle ──► M2 CPU on board (GATE) ─┐ │
 M3 machine on host (SAM, PIAs)     M6 bring-up ◄──────────┼─┘
 M4 video   M5 keyboard               │                    │
   └──────────┴─────────────────► M7 GUEST BOOTS ON DEVICE ◄┘
 M8 audio → M9 card, menu, cartridges, tape trap → M10 snapshots
 M11 perf pass, archive run, soak → M12 tape signal and recording
 M13 RS-DOS → M14 Dragon 32/64 and DragonDOS → M15 HD6309 → M16 release
```

The CoCo is the machine of every milestone to M13. The Dragon 32 is
booted on the host from M3, because its different ROM and matrix are a
cheap check that the wiring is data and not assumption, but nothing waits
for it, and it reaches the device in M14.

**The Dragon is frozen until M14** (the owner, 2026-11-15): no milestone
before it adds to the Dragon 32 or 64. What M3 built stays, and its tests
keep passing.

| M | What | Done when |
|---|---|---|
| **M0** | Repository, the two-target build, `config.h`, CI, `CLAUDE.md`, `milestones.md`; the siblings' files sorted into §5's lists | CI green on both builds; an SDK include in a core header shown to fail the host build (EL §2.1) |
| **M1** | `mc6809.c`; XRoar built by script; the oracle and the behaviour tests | The oracle passes over every opcode and mode; each planted bug fails it; the skip is held to a stepping machine |
| **M2** | **The gate**: the CPU alone on the board (§7.5) | Host cycles per instruction measured; board counts equal the host's; a projected share of core 0 under 60 %, or the levers applied and the result taken to the owner |
| **M3** | The machine on the host: bus, SAM, PIAs, field loop; a CoCo with Color BASIC alone and with Extended | The CoCo boots its real ROMs to `OK` in each RAM size and answers a typed sum, read from video RAM; the trace diff against XRoar runs through boot and typing; a Dragon 32 boots on the host as the second wiring |
| **M4** | Video: the VDG through the SAM, every mode and the mismatched pairs, artefact colours, the presenter | Golden images looked at and agreeing with XRoar's rendering; the dirty-band test with its control |
| **M5** | Keyboard: the CoCo's matrix, the mapping table, hold and gap; the Dragon's matrix as a second table (moved to M14 by the freeze) | Every table entry typed through Color BASIC; the long-program test stores every line |
| **M6** | Board bring-up from the copied drivers, each stage its smoke test (EL §13.3) | Test pattern, keys logged, card read, on the owner's Plus 2 W (the owner, 2026-11-15: the one board; the second board is M16's) |
| **M7** | **The CoCo boots on the device**, timer-paced | Extended Color BASIC's banner and `OK` on the panel, a typed program run; the 150-field list equals the host's; the owner has typed on it |
| **M8** | Audio: the level filter, the multiplexer, pacing on the queue | Every sample to 1 LSB on the host; `SOUND` and `PLAY` at pitch on the device; zero underruns; the late path forced |
| **M9** | Card, settings, the menu, the Machine page, ROM cartridges, the tape trap | A game loaded from CAS and from a cartridge image and played; `CSAVE` and `CLOAD` round-trip; the trap's state held to the ROM's own routine |
| **M10** | Snapshots | Restored mid-program into a machine doing something else, identical straight after and 150 fields on |
| **M11** | The perf pass with SRAM tiers; the archive run; the soak | Each feature's cost against a control; D12 and §9.4 decided from the archive; a clean 30-minute soak on both builds |
| **M12** | The tape signal, turbo, recording, the keyboard joystick and game layouts | Edge-for-edge against the ROM's own save; a recording loads in XRoar; a joystick game played by the owner |
| **M13** | Discs for the CoCo: the WD1793, the RS-DOS controller with its HALT pacing, Disk BASIC, JVC/DSK images | Disk BASIC runs `DIR`, `LOAD`, `SAVE`, `DSKINI` and `BACKUP` on the host and the device; the CoCo disc archive held to XRoar by screen |
| **M14** | The Dragons on the device: Dragon 32; Dragon 64 with its ROM switch, 64 KB map and the serial chip's idle registers; DragonDOS on the WD2797, VDK images | Both boot on the panel, the 64 in both modes; DragonDOS runs its commands; the Dragon archive run |
| **M15** | HD6309 | Its oracle passes in both modes; a 6309 program runs on the device |
| **M16** | Finish: README, the release build on a second board, attribution | A fresh card and board set up from the README alone |

---

## 18. Constants to confirm

Written from secondary knowledge or XRoar's source. Each is transcribed
from its primary source before it becomes a `#define`, and how it was
settled is recorded here (EL §14.2). While unverified, a constant is
configuration; once settled, it is a constant.

| What | Assumed | Confidence | Primary source |
|---|---|---|---|
| Host cycles per 6809 instruction (§7.5) | 200–350 | **Settled** 2026-11-14 (M2): 88.1 on a BASIC-shaped loop and 113.3 on a random mix, at tier 2 with the decoder out of line; 99.5 and 863.7 from flash | M2, on the board |
| 6809 cycle counts, flags, interrupt sequencing | Datasheet; **settled in M1** | High | MC6809E datasheet; the oracle; see below |
| The NMOS 6809's undocumented opcodes and what software reaches them | Trapped | Low | The archive run (D12) |
| A published 6809 functional test of standing exists | **None found** (M1, 2026-11-14) | — | Searched in M1; see below |
| The master oscillator on PAL Dragons and PAL CoCos | 14.218 MHz against NTSC's 14.31818 | Low | Schematics; XRoar counts every machine in 14.31818 MHz ticks |
| Lines a field and their layout on PAL machines; where FS and the field interrupt fall | 312, the extra lines inserted by a circuit outside the VDG. **M3, the Dragon 32**: XRoar's placement (§13.1), which the trace diff holds through boot, typing and 50 guest seconds of a program; the schematic not yet read | Low | Dragon 32 schematic and service manual |
| Ticks a line and the active line's start | 57 CPU cycles; the datasheet's. **M3**: 912 ticks a line, 262 a field, FS falling as line 230 begins, held to XRoar's field interrupts by the trace diff | Medium | MC6847 datasheet |
| PIA IRQ response time tRS3 (§8.3) | 1 µs, 14 ticks as XRoar counts it; **M3**: the trace diff needs it | Medium | MC6821 datasheet figure 13 |
| The SAM's bit pairs, its address-counter rule for each video mode, its rate rules and the stretched cycle (§8.2) | As §3.2, §8.2. **M3**: the bit pairs, and RAM's row and column in 4K, 16K and 64K modes for 4K, 16K, 32K and 64K chips as XRoar models them, held through boot in every size by the trace diff; rates (M11) not yet. **M4**: the video counter (§9.2), XRoar's model of its dividers and HS clears, held pixel for pixel to XRoar's MC6883 feeding its MC6847 in every pairing of the eight SAM modes with the ten VDG modes, in both colour sets (`test_vdg_oracle`), and the matched pairs to the addresses the datasheet's modes imply (`test_video`). P1 in map type 1: XRoar's model, and so this core, applies it in both map types, so in all-RAM mode with P1 set the two halves of $0000-$7FFF alias; the M3 review read the datasheet as map type 0 only. The Color Computer 2 service manual says P1 "controls a feature which is not used in the Color Computer 2" and "should be cleared at all times" (2026-11-15), so no software for the machine relies on it either way; XRoar's behaviour is kept. The datasheet would settle the chip's behaviour | Medium | MC6883 datasheet; XRoar's traces |
| What unpopulated memory and unused I/O read (§8.1) | **M3**: absent RAM `$FF`; empty sockets and unused I/O the byte at `$FFFF` (§8.1), as XRoar's trace needs | Medium | Schematics; neither assumed `$FF` |
| The PIA wiring of §3.3, bit for bit, on each machine | As listed. **M3, the CoCo**: IRQ from PIA0, FIRQ from PIA1; the RAM strap on PIA1 PB2 (low on 4K, high on 16K, linked to PIA0 PB6 on 32K and 64K); port B's undriven inputs low; PB1 reading the silent sound high; PA0 the cassette, low; PA7 the comparator; PIAs repeating over 32 bytes. Each one the boot trace needs (§15.2). **M3, the Dragon 32**: PIA1 PB0, the printer's BUSY, read low, as XRoar's absent printer is never busy, over the pull-up; PB2 high with 4K or 16K chips and low with larger; the PIAs repeating with 32 KB and answering only with A2 low above it. PB1 reads low until XRoar's sound first updates (the first FS rise, or PB1 or CB2 enabling the sound) and the silent sound high after: the Dragon reads it 23 instructions after reset, the CoCos only after the first field. That and PB0 are XRoar's behaviour, not yet the hardware's | Medium | Schematics; the ROMs executed |
| VDG data wiring: bit 7 A/S, bit 6 INV, INT/EXT from GM0 (§3.4); the mode on PIA1 PB3–PB7 (§3.3) | As listed. **Settled in M4** (2026-11-15), the ROMs executed: PB7 A/G, PB6–PB4 GM2–GM0 and PB3 CSS by Extended Color BASIC's PMODE 0–4 under SCREEN 1,0 and 1,1, each giving the datasheet's matched VDG and SAM modes (`test_screen`); bit 7 A/S and bit 6 INV by BASIC's own screen, read back from the snapshot as the text guest_row reads from RAM, and SET drawing semigraphics 4. INT/EXT from GM0 is XRoar's wiring, held by the VDG oracle; no ROM run selects it | High; INT/EXT medium | The ROMs executed; schematics for INT/EXT |
| Both keyboard matrices (§11.2) | **M3, the CoCo**: every letter, digit and shifted symbol typed through the matrix and printed back by Color BASIC 1.3 and Extended 1.1 (`test_boot`). **Settled in M5** for the CoCo (2026-11-15): all 56 cells pressed at the prompt alone and with SHIFT in Color BASIC 1.1, 1.3 and Extended 1.1, and what POLCAT returned read back; the three agree in every cell, the four empty cells of row 6 decoding whatever follows the ROM's table (`test_keyboard`, `keymap_picocalc.c`). **M3, the Dragon 32**: the same keys, by the same test through Dragon BASIC, on XRoar's table (the CoCo's rows turned by two, the digits first) | — | Each ROM executed |
| Key hold and gap for each ROM (§11.1) | **Settled in M5** for the CoCo (2026-11-15): 2 fields down in Color BASIC 1.3, 3 in 1.1 and Extended 1.1, 1 up in all, no repeat; the replay's 4 and 2 (§11.1). The Dragon's: M14 | High (CoCo) | Each ROM executed |
| The ROMs' tape block routines, their entry bytes and what they leave (§12.3) | Unwritten | — | The ROMs read and executed |
| Tape bit timing as each ROM reads and writes it | Two tones, about 1200 and 2400 Hz | Low | The ROMs read; a save recorded |
| ROM file names, sizes and SHA-1s (§12.1) | **Settled** 2026-11-14 (M3) for `bas13.rom`, `extbas11.rom`, `disk11.rom`, `bas11.rom` and `d32.rom`, the owner's images, each matching XRoar 1.13's CRC-32 (`roms/README.md`); the Dragon 64's and other CoCo versions to come | High | The owner's images; XRoar's ROM list |
| Disc controller registers, latch bits, interrupt and HALT wiring (§12.4) | As the table, from XRoar's decodes | Medium | Each DOS ROM read; controller schematics |
| WD2797 against WD1793: which differences each DOS sees | Side select, some status bits | Low | Datasheets |
| VDK and JVC header rules | Community descriptions | Low | A corpus of real files (EL §8.8) |
| Seeds and flags in RAM at power-on (§13.3) | **M3, the CoCo**: none taken from RAM; the warm-start flag `$71`/`$55` fails on zeros (§13.3) | High | Both ROMs read |
| What the Dragon 64's ROM needs of the 6551 at boot (§8.4) | Idle registers | Low | The ROM executed |
| Artefact colours: which pair gives which colour in each phase (§9.5) | Blue and red, swapped by phase. **M4**: XRoar's simple renderer's rule, its colours, and its two phases, KBRW its NTSC default; held to that rule applied to XRoar's VDG output by the oracle; no photograph compared | Low | XRoar's table; photographs of a real set |
| The VDG's fetches on a line (§9.2) | **M4**: 42 bytes in the 32-byte modes and 22 in the 16-byte ones, on to the right border, each counted by the SAM; XRoar's VDG | Medium | MC6847 datasheet's timing; a mismatched pair on a real machine |
| The VDG's address through the multiplexer (§9.2) | **M4**: no page bit P, and the first bank, as XRoar's fetch; only a machine with two banks or P set could tell | Low | MC6883 datasheet |
| The VDG's mode pins while PIA1 port B is an input (§9.3) | **M4**: low, as XRoar's PIA has an undriven pin; seen only before BASIC sets the port's direction | Low | Schematics |

**Settled in M1**, 2026-11-14:

- **6809 cycle counts, flags and interrupt sequencing.** The cycle table
  in `mc6809.c` was written from the MC6809E datasheet, and the oracle
  holds every documented opcode in every addressing mode to XRoar 1.13's
  `mc6809.c`, which counts bus cycles one by one: 266 opcodes, 2,048
  trials each, every indexed postbyte and `TFR`/`EXG` pair eight times,
  with interrupts, SYNC, CWAI and reset. They agreed on every register,
  cycle and write, with no datasheet count changed, and no erratum of
  XRoar's was found. Flags the datasheet leaves undefined follow XRoar
  (§7.1).
- **A published 6809 test program of standing.** None was found. A search
  on 2026-11-14 turned up hand-written suites inside other emulators
  (MCL6809's, the `mc6809-core` crate's test binary), a 2003 instruction
  exerciser by W. Schwotzer for FlexEmu whose availability was not
  checked, and an open request on r/EmuDev for a ZEXDOC-like 6809 test.
  None is a common standard, as Dormann's is for the 6502 and ZEXALL for
  the Z80. The oracle stands in its place; Schwotzer's exerciser is the
  lead if one is wanted.

---

## 19. Risks

1. **The CPU's cost.** An estimate (§7.5). The gate is the second
   milestone, on the family's measured method, with ordered levers.
2. **A new CPU core is the largest new code.** Its correctness rests on
   the oracle, so the oracle is built first and shown to fail (§7.4).
   The oracle shares XRoar's mistakes, if it has any: the datasheet
   decides a disagreement about cycles (EL §11.4).
3. **Instruction granularity against software that counts cycles.**
   The SAM's rate changes and titles that race the beam mid-line are
   where a whole-instruction CPU can show. The archive run counts them
   (§15.4), and what cannot be modelled is recorded, not hidden.
4. **Mid-field mode changes** (§9.4) may prove common enough to need
   split rendering, which is more work in the presenter.
5. **Two DOSes, two wirings, one chip model** (§12.4): the HALT-paced
   transfer is new to the family.
6. **The keyboard.** The PicoCalc resolves Shift itself and swallows
   some chords (HW §6.3). Typing by character suits it; games that read
   SHIFT with the arrows do not, and need layouts.
7. **No ROM can be shipped**, so first use needs a card prepared from
   the README, and CI cannot run the real ROMs.
8. **Secondary sources.** §3 is written from memory and from another
   emulator. §18 exists so that none of it reaches a header unchecked.

---

## 20. Dropped and deferred

**Dropped**, with the reason (EL §14.5):

- **Porting XRoar's source** (`design-xroar-port.md`): its per-cycle
  structure was estimated at 70–140 % of a core at 300 MHz, and its heap
  needed PSRAM (D1).
- **Scaling the picture**: uneven doubling for 56 % more wire time
  (EL §5.4).
- **Composite simulation and XRoar's finer artefact table**: the
  byte-pair table gives the colours software drew with (§9.5).
- **Video noise** in the SAM's all-fast rate, and bus snow.
- **A faster guest clock** as an emulator option: no common modification
  beyond the SAM's own rate bits (EL §9.3).
- **RP2040 boards**: a memory question first, and unmeasured (Atom §3.1).
- **Internal flash writes; ROMs in the firmware** (D10).

**Deferred**, with what would bring each back:

- **CoCo 3** (GIME, 512 KB, 640-sample modes) and **MC-10** (a 6803):
  machines of their own, after release.
- **Dragon 200-E, Alpha, Professional; RAM expansions**.
- **The MC6847T1** and its lower case, for later CoCo 2s: a variant
  table, when a ROM set that expects it is supported.
- **DMK disc images; double-sided and 80-track drive types** beyond what
  the first DOS tests need.
- **Sound and other active cartridges** (Orchestra 90, the Games Master
  Cartridge), the multi-pak: the Oric's AY-3-8912 is there to take.
- **Printer output to a file; the serial ports**.
- **WAV tapes**; **importing XRoar snapshots** (EL §8.8).
- **Split-field rendering** (§9.4) and **implemented undocumented
  opcodes** (D12): each decided by the archive run.
- **300 MHz**: if the gate asks, or as a user's opt-in later (EL §9.4).
