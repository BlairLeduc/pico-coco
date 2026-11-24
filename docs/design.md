# pico-coco: design

A **Tandy Color Computer 1/2** emulator for the **ClockworkPi PicoCalc**,
written in C against the Raspberry Pi Pico SDK, and for that machine
only (D6). It is a member of the
pico-atom family: pico-atom's architecture, its MC6847, its port layer and
its menu, with the 6502 replaced by a new **MC6809**, and later an
**HD6309**.

**The CoCo is the only machine** (D6, revised 2026-11-16). It is the
machine the owner started on and the one this project is for. The Dragon
32 and 64, which M3 booted on the host as a second wiring, and the PAL
field at 50 Hz with them, were removed so that the code serves the one
machine and its NTSC field alone. XRoar grew the other way round, from a
Dragon, and where its source or its defaults lean that way this design
does not follow.

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
20. [Dropped and deferred](#20-dropped-and-deferred) ·
21. [Proposal: the MOOH's 512 KB](#21-proposal-the-moohs-512-kb)

---

## 1. Goals and non-goals

**Goals**

- Run the stock ROMs of the **Tandy CoCo 1 and 2** (Color BASIC, Extended
  Color BASIC, Disk BASIC) well enough that BASIC, RS-DOS and the common
  body of CoCo software behave as on the real machine, artefact colours
  included.
- A **6809 that is cycle-correct at instruction granularity** (EL §3.1),
  with interrupts, `SYNC`, `CWAI` and HALT as software depends on them, and
  the **6309** as a fitted option afterwards.
- **Real time with headroom at the stock 150 MHz clock, without PSRAM**, so
  one image runs on every RP2350 board the family supports (D3).
- Tape, cartridges, floppy discs (RS-DOS), snapshots and
  settings from the SD card; the family's menu (D5).
- A **host-testable core** with no SDK header and no allocation, built
  under CTest on a workstation and in CI (EL §2.1).

**Non-goals for the first release**

- The CoCo 3, the MC-10, every Dragon, PAL machines at 50 Hz, and RAM
  expansions (§20).
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
| D6 | 2026-11-14, revised 2026-11-16 | **The Tandy Color Computer 1/2 only.** One machine with different ROMs and RAM; the default is a CoCo with Extended Color BASIC. As first settled the Dragon 32 and 64 followed it; the owner removed them on 2026-11-16, with the PAL field, to make this a CoCo emulator and optimise for it: the field's shape is a set of constants (§13.1), and the machine field, the Dragon's wiring, ROM and tests are gone. | Settled by the owner |
| D7 | 2026-11-14 | Standalone, as the family: boots straight into the guest; a menu item returns to the PicoCalc shell when UF2 Loader started the image (`picocalc_shell.h`, as Atom and Oric). Development build on UART1, GP4/GP5; release build with no UART. | Settled by the owner |
| D8 | 2026-11-14 | **XRoar is the reference emulator** (EL §11.4): built by a script from its own checkout at a pinned release, headless, never copied into the tree. Its source is also a secondary source for guest constants, each cross-checked against a datasheet, a schematic or the ROM executed (§18). | Settled by the owner |
| D9 | 2026-11-14 | Licence **GPL-3.0**, as pico-atom and pico-oric, whose code this starts from. XRoar is GPL-3.0-or-later, so compiling its CPU into a test tool is compatible. | Settled by the owner |
| D10 | 2026-11-14 | No ROMs shipped; identified on the card by SHA-1 (§12.1). No internal flash writes (EL §8.7). | Settled by the owner |
| D11 | 2026-11-14 | The picture is 256×192 at 1:1 at (32, 64), as the Atom's (EL §5.4). | Settled by the owner |
| D12 | 2026-11-14 | 6809 undocumented opcodes are **trapped and counted** first; the archive run decides whether any is implemented (EL §3.1, §11.6). **Decided 2026-11-16 from M11's archive run**: every undocumented opcode is executed as the NMOS chip executes it, XRoar's model held by the oracle, and still counted (§7.1, §15.4). | Settled by the owner |
| D13 | 2026-11-14 | The project is named **pico-coco**, after its first machine as the siblings are, with the card folder `/coco/`. The owner renamed the repository directory from `picocalc-xroar` when its GitHub origin was set up. | Settled by the owner |
| D14 | 2026-11-14 | PicoCalc findings go into `hardware-notes.md`, and what generalises into `emulator-lessons.md`, as learned, each written to stand alone (EL §14.1). One branch and pull request per milestone, CI green on both builds. | Settled by the owner |
| D15 | 2026-11-21 | **The MOOH** (512 KB and its MMU, a CoCo 1/2 cartridge), its memory in the Plus 2 W's PSRAM, revising D3's *no PSRAM*; other boards run without it. Not before PSRAM's cost is measured against SRAM (§21). A 4 KB-page variant after the 8 KB is held (§21.6). **Deferred by the owner, 2026-11-22**: moved to a separate, aspirational CoCo build for OS-9; not built here, and D3 stands. | Deferred |

---

## 3. The guest machines

Written from secondary knowledge and XRoar's source, to fix the shape of
the code. **Every figure here is a row in §18 until settled.**

### 3.1 What they are

The CoCo 1 and 2 are Motorola's reference design for these chips:

| Part | Role |
|---|---|
| MC6809E | CPU, E clock about 0.89 MHz |
| MC6883 SAM | Clock generation, address decode, DRAM refresh, **the video address counter** |
| MC6847 VDG | Video: the chip pico-atom already models |
| 2 × MC6821 PIA | Keyboard, joystick comparator, cassette, 6-bit DAC, single-bit sound, VDG mode pins, cartridge interrupt, printer |
| ROM | Color BASIC at `$A000` and, when fitted, Extended Color BASIC at `$8000`, 8 KB each |
| Cartridge port | ROM at `$C000` (Disk BASIC, a game), I/O at `$FF40`, lines to FIRQ, NMI and HALT |

A **CoCo** is 4, 16, 32 or 64 KB of RAM, with Color BASIC alone or with
Extended Color BASIC; the CoCo 2 is the same machine on a smaller board,
and its last revisions carry the MC6847T1 (§20). It was sold as an NTSC
machine, at 60 Hz, and its software uses NTSC artefact colours (§9.5).

The Dragon 32 and 64, built from the same reference design, are not
emulated (D6).

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
| PIA1 PA1 | Serial out |
| PIA1 PA2–PA7 | The 6-bit DAC: sound, cassette out, joystick reference |
| PIA1 CA2 | Cassette motor |
| PIA1 PB1 | Single-bit sound |
| PIA1 PB2 | RAM size strap |
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

A field is 262 lines at 60 Hz, of 57 CPU cycles each; 192 are active.
PAL machines are not emulated (D6).

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
| Atom | `mc6847.c`, `mc6847.h`, `mc6847_font.c`, golden-image tests and `vdg-ppm.c` | The CoCo's wiring (§3.4): SG4, INV on bit 6, the T1 variant's lower case later; rows addressed through the SAM (§9.2); artefact colours (§9.5) |
| Atom | `beeper.c` | From one bit to a level: the DAC, the single-bit output and the multiplexer (§10) |
| Atom | `bus.c`, `bus.h`, `atom.c`, `atom.h` | The machine: `coco.c`, with the SAM's map and the PIAs (§8) |
| Oric | `wd1793.c`, `mfmdisk.c`, `microdisc.c`, `discio.c` | The WD1793 for RS-DOS, its latch and interrupt wiring, JVC and VDK images (§12.4) |
| Oric | `cassette.c`, `tape.c`, `tap.c`, `tapeio.c` | The CoCo's cassette format, CAS files, the ROMs' block routines (§12.3) |
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
- **Undocumented opcodes executed as the NMOS chip executes them** and
  counted, with the count and the last PC on the heartbeat (D12). Until
  M11 they were consumed as NOPs; the archive run found titles that rely
  on the chip's behaviour in normal play (§15.4), and the owner decided
  on 2026-11-16 to implement them all as XRoar does. The oracle holds
  every byte of pages 1, 2 and 3, the prefixes chained, and HCF (M11).
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

**The undocumented opcodes, as built in M11** (2026-11-16), each XRoar's,
whose sources are hoglet67's measurements and Ciaran Anscomb's 1994
notes: `$x1` is NEG, `$x5` LSR; `$x2` is NGC, NEG with the carry in;
`$xB` a DEC that sets C when it borrows; `$4E` and `$5E` a CLR that leaves
C; `$1B` a NOP; `$38` an ANDCC a cycle longer; `$18` shifts CC left under
a mask from the next byte, which it does not consume; `$3E` stacks every
register with E as it stands and takes the RESET vector, masking nothing;
`$87` and `$C7` take an immediate byte and set N, clearing Z and V; `$8F`
and `$CF` store X's or U's low byte over the byte after their operand;
`$14`, `$15` and `$CD` lock the CPU until RESET (HCF), interrupts and HALT
ignored. After `$10` or `$11` the first prefix chooses the page and each
further prefix is a cycle; an opcode the page lacks runs as page 1's, a
cycle later for each prefix, except `$1020` (LBRA), `$108D` and `$118D`
(LBRA), `$103E` and `$113E` (`$3E` with the SWI2 and FIRQ vectors),
`$10C3`-`$10F3` (ADDD setting only the flags) and `$11C3`-`$11F3` (the
same on U with its high byte set), and a list of each page's that is a
dead cycle (`mc6809.c`).

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

**A loop polling a PIA** (2026-11-23, the owner's Plus 2 W, tier 2):
The Interbank Incident waits for the fire button in five instructions,
a write to PIA0 B and a read of PIA0 A, 870 times a field. Each access
brings HS, FS and the PIAs' rises up to its tick (§8.3), and that cost
about 1,000 host cycles: 541 an instruction, core 0 95 % busy on the
loop alone, and over 100 % in the game, which underran the audio for a
second. Sampled by PC over SWD (HW §9.10): two of the path's functions
in flash, 64-bit divides for HS's place in its line, and every rise
looked at on every access. Each fix measured in turn on the same loop,
typed in from BASIC: in SRAM, 81 %; HS's place from the field's start
in 32 bits, 74 %; HS's next change of level kept, 65 %; the first rise
due kept, **62 %, 352 cycles an instruction**. The trace diff agrees
with XRoar after it.

---

## 8. Bus, SAM and PIAs

### 8.1 The page table

The Atom's (EL §4.1): 256 pages of two pointers, NULL for the slow path,
flags in a separate byte array. RAM and ROM are one indexed load. The
I/O page `$FF` is slow-path and decoded there: the PIAs **by mask**, so
their mirrors work; the SAM's bit pairs; cartridge I/O; and the vectors
at `$FFE0`–`$FFFF`, which read from ROM in either map type.

The table is rebuilt only when the bits that choose the map change
(EL §4.1): the SAM's map type and page bit and a cartridge's presence. Each page's flags say which array it points into,
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
  where the field is split (§13.1). HS falls 262 times a field; its
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
need to see them idle or busy; output is discarded and counted (§20).

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
table and an option, built in M13 (below).

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

**The MC6847T1, as built in M13** (2026-11-17). The CoCo 2B's VDG, for
OS-9's lower case, which the owner asked for. It is a setting, not a
machine: `vdg = 6847 | 6847t1` in `coco.ini` and the Setup page's VDG
row, applied by the display at once, the MC6847 by default; the core
does not know which is fitted, as nothing but the picture differs. Its
character ROM is XRoar's `font-6847t1.c`, taken verbatim as the
MC6847's was (`mc6847_font_t1.c`, THIRD-PARTY.md; the owner's choice),
128 glyphs with lower case at $00–$1F and the glyphs a cell higher than
the MC6847's. What a byte shows is XRoar 1.13's model (§18): in the
alphanumeric modes GM0 high picks the glyph by seven bits and inverts
every cell, so $01–$1A, the MC6847's inverse capitals that Color BASIC
and OS-9 write for lower case, become lower case, dark on green; GM1
inverts every cell; GM2 with GM1 low draws the border in the ink's
colour; there is no SG6; the ink with CSS high is orange. The graphics
modes are the MC6847's. `test_vdg_oracle t1` holds every alphanumeric
mode, GM2–GM0 all eight, and the graphics modes pixel for pixel to
XRoar's MC6847T1, and `test_mc6847` the border, which the oracle does
not see.

Tandy's OS-9 Level One 2.00 (CCIO) and NitrOS-9 3.3.0's Level 1 (VTIO)
set GM0 as their console starts when bit 0 of the TERM descriptor's PAR
is set, and at no other time: their TMODE changes PAR but does not make
the SS.ComSt call that reaches the bit (both drivers read on the host,
2026-11-17). As shipped, PAR is $00 and $02, so neither shows lower
case; `tools/os9-lowercase.py` writes a copy of a boot disk with the
bit set in the boot file's TERM, and `test_os9` boots both systems so
(the control: as shipped, GM0 stays low), with NitrOS-9's `DIR` heading
drawn from the T1's lower case.

### 9.3 The snapshot

Published at the end of each field, **where the next active line begins**
(EL §5.6); since 2026-11-22, where FS falls, each line having been
fetched as it was shown (below, EL §5.11):

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

**Each line fetched as it is shown** (2026-11-22). Dragonfire's room
draws its moving figures in step with the beam, erasing each just after
the beam has passed it and drawing it again before the beam comes back,
so a snapshot taken from RAM at one moment showed the room and its
items but only slivers of the dragon and the player, and no fireball.
Now the machine fetches each active line's bytes through the SAM's
counter as the line's first byte is fetched, with the VDG's mode the
picture shows then (a PIA write's 1 µs late, §9.4) and the SAM's V,
into `coco_t.vid_lines`; the counter loads F as FS rises. A field's run
ends where FS falls, the line after its 192 active ones (§13.1), and the
snapshot copies the field just shown; a field not fetched from its
first line, after a power-on or a restored state, is walked from RAM as
before. Taken a byte at a time at each byte's tick instead, Dragonfire's
room was drawn the same in every field a probe could check (66 of 66;
in the other ten the game shows CG6 at SAM V0, which the probe did not
model).

The run does not stop at each line. A line is fetched at the first of:
a slice's boundary after its first byte is due, a write to RAM the VDG
may fetch from this field, or a write to PIA1 or the SAM, each at its
own cycle. The CPU pages over the 8 KB from the field's base, 192 lines
of at most 42 bytes, write by the slow path, whose first test is for
such a page; the page table is rebuilt only when the base or the RAM's
organisation changes. First built stopping the run at each line, 192
stops a field, which cost core 0 about 6 points on the board; then as
above. Measured on the owner's board against M13's image in one
sitting, eight windows each (`out/beam/`, not committed):

| Workload | M13 | Stopping each line | As built |
|---|---:|---:|---:|
| Idle prompt | 22.7 % | 28.5 % | 23.5 % |
| Scrolling `PRINT` | 23.1 % | 29.6 % | 27.4 % |
| `compute` | 18.7 % | — | 19.5 % |
| `pmode4` | 18.5 % | — | 19.7 % |
| `set` | 18.6 % | — | 19.4 % |
| `sound` | 23.7 % | — | 24.9 % |

The last four were measured in a second sitting the same day, the two
images one after the other. A program that writes the screen pays for
its writes' slow path; one that does not pays about a point. The
snapshot's share of core 0 fell from 3.6 % to 0.3 %, since it copies the
fetched lines; the guest's share rose by about 4 points. `test_video`
holds it: a byte drawn ahead of the beam and erased behind it is shown
on its row's 12 lines, with the SAM at 4K, every write on the general
slow path, and at 64K, the screen's writes on the short one; RAM walked
where FS falls, the control, shows it on none. Planted, one at a time:
the snapshot never taking the fetched lines, no fetch before a write on
either path, and none before a PIA1 write; each failed it. The trace
diff agrees with XRoar as before.

The disk archive (§15.4), rerun against XRoar on 2026-11-22: 1,015
titles show XRoar's picture at the capture and 486 at another moment
within 10 s, against M13's 1,031 and 468; 1,501 either way, against
1,499. The titles that moved are mostly text screens one cursor apart
that match 0.1 s away, both ways. Most likely the typed command lands at
another point in the cursor's blink, since keys are pressed between
fields and the field's run now ends where FS falls; not traced title by
title. The comparison itself is taken at XRoar's moment, the end of the
field, from a copy of the machine run on to it.

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

**Decided from M11's archive run** (the owner, 2026-11-16): split
rendering is built in M12. Of 1,059 titles run for a minute each, 655
changed the video bits on an active line at least once, nearly all for
a field or two at a change of screen, and 17 did so in 60 fields or
more once the undocumented opcodes ran as the chip runs them (D12):
Maths Invaders (the display offset, nearly every field), Dragon Fire
(the cartridge and both tapes), Dark Star and Rommel's Revenge (every
field, near lines 9-17), Defend, Desert Rider, Glaxxons, Model
Rocketry, Superkid, Kids on Keys (cartridge and tape), Cave Fighter and
Eliminator (§15.4; `docs/milestones.md`, M11).

**As built in M12** (2026-11-16). The first cut drew regions of whole
lines, and drew the three titles that change every field exactly as
before: Dark Star, Rommel's Revenge and Dragon Fire set CSS a pixel
into a line and clear it 240 pixels on, on each of some lines, so that
the last change on a line left it as it began. The changes are
therefore kept by **line and pixel**. `coco.c` notes each change of the
VDG's mode or the SAM's V on an active line with the tick of the write:
before the line's 256 pixels (254 ticks after HS falls, XRoar's MC6847
figures, §18) it is the line's from its first pixel; among them, from
its pixel, two ticks a pixel; after them, the next line's. A change of
the display offset alone is counted as before but not kept: the SAM
loads F only as the field begins. Up to eight changes a line are kept
for a field (`COCO_SPLIT_MAX`), more dropped and counted; Dragon Fire
makes 284.

The snapshot was taken where the next active line begins, before the
changes of the field it shows, so it was drawn by **the last whole
field's**: a title that splits the screen does so every field at the
same places. Since each line is fetched as it is shown (§9.3, 2026-11-22),
the snapshot is the field just shown, drawn by its own changes. `coco_frame_t.how` says, for each line, the mode it begins
in and up to eight changes along it (two until 2026-11-22: Dragonfire
makes up to six, on its moat's lower edge); a line's pixels last 32 E
cycles and the quickest store to the PIA takes 4, so a ninth is not
expected, and folds into the eighth. A line is fetched by the mode it begins in, with the SAM's
dividers chosen by V as it then was and the counter carried on from
the line before; a change along a line is drawn from its pixel in the
new mode from the bytes the line fetched, and its V is the next
line's. `present.c` keeps how each line was drawn in its shadow: a band
is repainted whole when that changes, and a band whose lines are not
all drawn one way is sent whole on any change of a byte. The border is
the mode's that most lines begin in. On the device core 1 keeps two
renderers, each with a mode's table, since Dragon Fire alternates two
modes on every line.

The snapshot, the SAM's counter and its stages run from SRAM from tier
1: from flash, grown to draw the line table, they cost core 0 2.4
points at the idle prompt, most of it the interpreter missing in the
shared XIP cache (HW §9.9; `docs/milestones.md`, M12).

Not modelled: a change of the VDG's bytes a line (16 or 32) or of V
part-way along a line, which is the next line's; the glitches XRoar
models when V changes; and the regions after a state is loaded, which a
field brings back (§12.5). A field's own changes in that field, which
showed a field late, are modelled since each line is fetched as it is
shown (§9.3).

**A PIA write reaches the picture 1 µs late** (2026-11-22): 14 ticks,
7 pixels after the write's cycle ends (`COCO_PIA_VIDEO_TICKS`), the
MC6821's output delay (tPDW, up to 1 µs from E), as `TRS3_TICKS`
(`coco.c`) is its interrupt's. Settled from two pictures of
Dragonfire's title the owner found. The first, a photograph of a real
CoCo off a video, showed the real machine changing colour set along a
line about 8 pixels later than this model did, which had drawn a strip
of yellow inside the left wall and of magenta inside the right; with an
E cycle added (16 ticks) the walls ran clean to blue edges. The second,
sharper, of a slightly later build, put every edge on a byte's
boundary: the blue edges at pixels 24–32 and 224–232, the bridge,
pillar and moat at 32, 112, 144 and 224. At 16 ticks every change fell
a pixel late, leaving a pixel of the other colour set at each edge; at
14 they fall on those boundaries exactly. Dark Star's and Rommel's
Revenge's heading bars, at pixels 1–240 before, are at 8–247, centred
on the screen. The SAM's writes are unchanged: nothing has shown their
delay. `test_video`'s change along a line includes the delay, and
planted delays of 0 and of 16 fail it. Dragonfire's moat, blue both sides of the pillar in the photograph and
magenta on the right here, was the limit of two changes a line, not
timing: the game sets the green set again after the pillar and the
buff one at the right wall, and those folded into its change at the
pillar; and on the moat's lower edge, lines 144–146, it makes six,
the last two of which folded into the fourth with room for four. With
eight a line, as above, the moat and the strip under the bridge are
drawn alike on both sides, the moat's lower edge as the sharper
picture has it, and every one of the field's 284 changes is drawn;
`test_video` makes six changes on a line, and a planted limit of four
fails it.

**A change undone within a line is drawn** (2026-11-23). Sea Dragon
flips its screen page every three or four fields as its playing field
scrolls in, and its routine at $5874 clears the VDG's mode before
setting it again: `LDA #$07`, `ANDA $FF22`, `STA $FF22`, then `LDA
#$FE`, `ANDA #$F8`, `ORA $FF22`, `STA $FF22`. For the 14 cycles between
the two writes the VDG is alphanumeric, so about 112 pixels of the line
the beam is on are drawn as text: a blank byte is an inverse `@`, a
row of green dots, and the game's striped $AA a semigraphics block, a
short blue bar. One such segment, on a different line each flip, is
the stray dots and lines the owner saw on the panel. It is the chip's
behaviour as modelled here and in XRoar (the mode pins are not
latched, and Dragonfire's photographs show changes taking effect along
a line), and the owner chose to keep it. The flip itself is drawn
right: the SAM loads F as FS rises, so the rest of the field stays on
the old page. Whether a real CoCo shows the segments is in §18.

### 9.5 Colour, artefacts and the border

- The palette is the Atom's `mc6847_palette`, with the mono palette from
  the datasheet's luminance levels (EL §5.2), chosen by a setting.
  **What an NTSC set showed** (the owner, 2026-11-23, who found orange
  too red and cyan too light a blue, and asked for an accurate
  recreation). The Atom's colours are the idealised primaries, a
  rendering choice never checked; pico-atom confirmed only which colour
  a byte picks, and its mono levels. The VDG puts out no RGB: it gives
  luminance Y and colour differences φB and φA at the levels of its
  datasheet's figure 10 (typical values). The model, each step from a
  primary source:
  - *The MC1372* (Motorola's datasheet): two modulators of equal gain,
    0.6 V p-p a volt; B leads A by 100°, 85° to 115° from chip to chip;
    B is (B−Y), since the burst is φB at 1.25 V, below the 1.5 V
    reference, and A approximates (R−Y), "offset by 10° from the
    nominal 90°". The datasheet's own table of recommended levels is
    figure 10's.
  - *The set's colour control* holds the burst at NTSC's 20 IRE, so the
    chroma is 20 IRE for each 0.25 V of colour difference, whatever
    level the board gives the chroma; luminance has blanking (0.77 V) at
    0 IRE and the VDG's white (0.42 V) at 100, so its black (0.72 V) is
    14.3 IRE.
  - *The decode*: on axes 90° apart, with NTSC's weights, U = 0.492
    (B−Y) and V = 0.877 (R−Y). A first build fed the colour differences
    in as equal Pb and Pr with the modulator's 0.6 as the saturation,
    which turned the hues and left them pale.
  - *The display*: the VDG's black as black, and white where the set's
    contrast put it, each channel clipped as a CRT's guns saturate.

  Held to a measurement: MAME's palette (PR #4980, 2019) is a capture of
  a **PAL Dragon**, whose LM1889 has its axes at 90°. The model at 90°,
  fitting only the capture card's own black and white (−8 and 140 IRE),
  gives green, yellow, blue, magenta, orange and dark green within 3° of
  the capture's hues, the text screen's orange at 37° against 43°, dark
  orange 13° against 22°, and red and cyan within 15°, inside the
  chips' spread; 15.7 of 255 r.m.s. A free saturation fits it only to
  14.3, so the burst's saturation is the capture's. At 100° it fits
  less well, as it should for a Dragon.

  What it says of a CoCo: graphics orange is about 30°, an orange, not
  an amber; cyan is a teal leaning to blue (174°), bluer than the
  Dragon's; **the text screen's orange is another colour**, graphics
  orange's chroma at white's luminance (in figure 10, and 0.43 V in a
  Dragon 64's, measured), brighter than white, an amber only where the
  set left room above white; and the dark backgrounds are the chip's
  own, black's luminance carrying the ink's chroma, a dark green and a
  dark red-brown. The one thing the chips do not fix is **where white
  sits**, the set's contrast: at 100 IRE buff is white and the text
  screen's orange clips to a pale yellow ($FFF200); at 130 the orange is
  amber ($FFB300, MAME's capture $FFB700) and buff a grey ($BDBDBD).
  `mc6847_palette_tv` had the three, white at 100, 115 and 130 IRE,
  generated by `tools/vdg-palette.py` and derived again in `test_mc6847`
  (commit `ca86999` has both). The artefact colours stayed XRoar's
  simple table's. Not modelled: the set's phosphors against the panel's.

  **The panel's own curve** (2026-11-23). Every palette was being seen
  through a panel far darker in the mid-tones than a computer display
  (HW §4.12): measured by eye with a temporary test page, half of full
  code gives about 4% of the light. `mc6847_curve_t` maps each channel's
  code to the one giving the light a gamma-2.2 display would, built from
  the three measured points per channel; the VDG applies it where its
  drawn palette (`col`) is built and to its own copy of the full
  artefacts' table, so the border, the text lines and the menu go
  through it too. Full and none are unchanged; orange's half green, the
  dark backgrounds' quarters and the artefacts' halves are lifted to the
  light they were written for. Unset, as on the host and in every test
  but the curve's own, it changes nothing, so the oracle and the goldens
  stand; the board sets it at start-up. With it on, the owner matched
  all twelve test patches at the gamma-2.2 codes, 2026-11-23.
  `test_mc6847` holds the curve to the measured points, its slope below
  the quarter, and a curved VDG to the curve of an uncurved one.

  **The primaries kept** (the owner, 2026-11-24). On the corrected
  panel the owner preferred the primaries for their saturation and found
  the TV palettes short of contrast, so the TV tables, their setting and
  the test page went. Kept from them: the text screen's orange in mono
  at white's luminance, and, as pico-atom has it, the choice of the
  **dark backgrounds** for the text screen and the two-colour screens
  with CSS low: `background = dark | black` and Setup's BACKGROUND, dark
  by default, the datasheet's backgrounds. And since sets of the time
  differed from one another more than any one palette can say, the
  settings file may give any colour as RGB, `color_green = 00FF00` and
  so on for all fourteen (§12.6), drawn through the curve like the rest;
  the menu never shows them. The full artefacts mix their colours from
  their own table, which these keys do not reach. The Setup page has no
  row left for the layout's bindings with BACKGROUND on it (§14).
  Three colours are the owner's, judged on the corrected panel
  (2026-11-24): cyan, which $00FFFF showed too light a blue, from a
  swatch ($41AF71, a sea green in keeping with the model's teal) then
  saturated to $12AF56; graphics orange, deepened to $FF6000; and the
  text screen's dark orange, which a quarter of the ink ($402000)
  showed too green, a dark red ($440400), as the chip's own is. The
  text's orange stays $FF8000. Each is drawn through the curve like the
  rest; the goldens changed in those pixels alone, and were looked at.
- **Artefact colours.** On NTSC machines the two-colour 256-pixel mode
  with the black-and-white colour set shows pairs of pixels as colours,
  and CoCo software draws with them. This fits the row table directly: a
  byte is four pixel pairs, and each pair maps to black, white or one of
  two colours. A setting chooses off, or one of the two phases (which
  colour is which depends on how the machine powered on, and titles
  offered a "press reset until red" screen). **It is on by default**,
  since that is how the machine looked.
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
  **Settings and the full artefacts** (the owner, 2026-11-22). Three
  ways to draw them, `artifacts = off | simple | full`, and the phase,
  `artifact_phase = blue | red | random`; simple and blue by default.
  *Full* is XRoar 1.13's default renderer, its 5-bit table: a pixel
  that is black or buff, with both neighbours black or buff, takes its
  colour from the five pixels centred on it (which are buff) and from
  its phase, which alternates pixel by pixel from the active line's
  first (XRoar's active area starts at sample 254 of its line, so the
  first pixel takes KBRW's first table). Runs of lit pixels come out
  white with coloured fringes, wherever they start. It applies in every
  mode, as XRoar's does: a two-pixel pattern in RG3 colours too. It runs
  over each line once composed (`mc6847_artefact_line`, from
  `present_render_line`), so a change of mode along a line is seen as
  the signal shows it; the border either side counts as a neighbour.
  The 64 entries are XRoar's, data, generated from its file by script.
  A pixel's colour reaches two pixels beside a changed byte, so a dirty
  band's span widens by two (`present_t.reach`). With the dark
  backgrounds drawn black, those colours are drawn a shade apart
  (the lowest bit of green or red), since XRoar's renderer does not
  take them for black. *Random* chooses the phase at each RESET and
  power-on, as a real CoCo 1 or 2's VDG locks to either clock edge
  ("press reset until the colours are right"); blue and red take effect
  at once from the menu, which no CoCo 1 or 2 could do. Held by
  `test_vdg_oracle` to XRoar's own `vo_render.c`, its 2-bit and 5-bit
  renderers linked into the test with its palette code and fed its
  VDG's output line by line (`xroar_render.c`): 160 fields, every mode,
  both colour sets, both phases, the dark backgrounds both ways, every
  pixel equal. XRoar's palette, with its start-up saturation of 50,
  takes only black for black and buff for white, which the simple
  rule's test had assumed. `test_present` holds the widened spans to
  a full render; with a reach of 0 or 1, presents are left stale.
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

**M8** (2026-11-15): the perf line takes pico-oric's `UR` figures, the
underrun samples and late refills since boot, and drops the field rate,
for which its 40 cells have no room: the CoCo's is always 60, and paced
on the audio queue a guest behind real time shows as underruns. The
heartbeat keeps the rate.

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

**As built in M8** (2026-11-15). The level is XRoar's model of the bus
(its `sound.c`, §18: low): each source has a gain and an offset in volts,
measured on a real Dragon, that depend on whether the single bit is
disabled, enabled low or enabled high; with the multiplexer off the
single bit alone gives 0 or 3.90 V; with neither driving it **the bus
holds its last level**, which BASIC relies on (it leaves the multiplexer
off after a `SOUND` with the bus high). The multiplexer's source is PIA0
CB2 and CA2 as a number, an input reading high on the A side and low on
the B side as XRoar's PIA has them; the tape with nothing in it is 0.5,
the cartridge 0 until M9. `coco.c` keeps the level as an integer in
hundredths of a volt over 126, exact for the DAC's 63rds and the tape's
half, 59,220 at 4.70 V, the full scale. PIA1 PB1 set as an input reads
the bus back, low at 0.3 of full scale or more. The stamp is the tick at
the start of the writing instruction (EL §6.1).

The PCM is pico-oric's `pcm.c` in ticks: a sample is
91,636,352/234,375 ticks at 150 MHz, and with `whole` near 2^42 its scale
needs 48 fraction bits, not the Oric's 32, whose rounding was a gain
error of tens of LSBs. `audio.c`, `sdk_sim/` and `test_audio_port` are
pico-oric's, renamed; core 0 drains after each field's snapshot and
pushes, blocking on a full queue; `PICO_COCO_AUDIO=OFF` keeps the
timer's pacing as the control and `PICO_COCO_FORCE_LATE` is EL §6.4's
scratch build. The perf line carries `UR`, the underrun samples and
late refills, in place of the field rate (§9.6).

What the tests found: the idle prompt and the whole boot never move the
bus; `SOUND T` and `PLAY` play a four-step staircase on the DAC ($02,
$7E, $FE, $7E for `SOUND`), `SOUND`'s period 20 (256 − T) + 100 cycles,
`PLAY`'s seven notes of O2 within 0.26 % of equal temperament at
A = 440 Hz by their loops, and the field interrupt makes what is heard
about 0.7 % flatter than the loop.

**The drives' sounds** (2026-11-22, the owner's request). A real CoCo's
disk drives are heard beside it: the head's steps, and the motor while
it turns. `drivesnd.c` synthesises them, so nothing is recorded or
shipped, from what was measured in recordings of a drive (the owner's,
below; `tools/drivesnd-analyse.py`, `tools/drivesnd-coeffs.py`, which
writes `drivesnd_coeffs.h`). The drive's noise is a hump at 1 kHz: white
noise through a resonance at 1,050 Hz, Q 1.4, behind a zero at 0.9 and
before a low pass at 6 kHz, fitted to the recordings' bands by least
squares and within 2.3 dB of them from 200 Hz to 8 kHz; it wanders a
quarter either way in swells of about 28 ms, as the recordings' does,
which have no sway once a revolution; under it, tones at 983 and
3,995 Hz at the recordings' levels. A step is nine resonators at the
frequencies a recorded step's excess over the noise peaks at, 590 Hz to
7.7 kHz, their decays from 9 ms down to 1.5, struck, and struck again
at half 11 ms on where the recordings rise again as the head settles;
a burst of white noise on the strike; and the drive's noise raised to
twice its level, falling in about 20 ms, as a recorded step's tail is
of the noise's own colour. Set beside the recordings by the same
measures: the noise's RMS 427 against 405, every band within 2.3 dB
to 8 kHz, a step's excess within about 4 dB a band, and its RMS over
the noise a millisecond at a time of the same shape. The levels and
times are in `config.h`. The voice is the PCM's
(`pcm_t.drive`), added to each sample after the DC blocker, so the
guest's level, its filter and the bus's model are untouched, and with
the sounds off nothing is added: the samples are those of a machine
without them, which `test_drivesnd` holds.

Its events are the cartridge's (§12.4). Each type I command leaves its
step pulses in the WD1793 (`pulse_n` from `pulse_from`, a step's time
apart), counted as the head is pulsed: a Step out at track 0 pulses
nothing, the TR00 test that ends a Restore pulses nothing, and a step
in at the drive's last track pulses as the head meets its stop. The
latch's motor bit, which nothing else acts on, starts and stops the
motor's sound. Before either is handed over the PCM is brought to the
tick that starts the writing instruction, the stamp the bus's changes
take, so a step is struck in the sample its pulse falls in and a train
is never cut short by the next command. A restored state and a
power-on drop what was to come. `disk_sounds = on | off`, on by
default, and Setup's Disk sounds row (§14) choose; core 0 gives the
setting to the machine each field, as it gives the SmartWatch's.

**Recorded sounds** in place of the synthesised ones (2026-11-22, the
owner's, from recordings of a drive under Pixabay's licence). They are
the user's and live on the card, as the ROMs do (D10): `step.wav` and
`motor.wav` in `/coco/sounds/`, read in the boot's card job by
`soundio.c` through `wav.c` (PCM, 8 or 16 bits, mono or stereo, 8 to
96 kHz) into two buffers outside the machine, a step of up to 2,048
samples and a motor's loop of up to 8,192, 20 KB of SRAM. A file not
there leaves its sound synthesised. A step is played from its start at
each pulse, cutting short the last; the loop goes round at the motor's
swelling and fading level. Neither the recordings nor anything cut from
them enters the tree: Pixabay's licence is not the GPL's and forbids
passing them on as they stand (D9). `tools/drive-sounds.py` cuts the
two files from the user's recordings: a step from just before an onset
that stands clear of the noise, 40 ms, its peak at 9,000; one
revolution of the motor, 200 ms, from the steadiest stretch at its
recording's usual level, its end cross-faded into its start, its RMS
at 700; both at 36,621 Hz.

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
CoCo's arrangement is **settled by executing each ROM**: press every cell at the prompt, with and without SHIFT, and read
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
  Shift chord can take (HW §6.3), and Ctrl, held as Shift is SHIFT
  (M13). Backspace and Del are ←.
- **The guest's arrows** are the PicoCalc's. The guest's shifted arrows
  (SHIFT+← erases a line on the CoCo) cannot come from Shift+arrow, which
  sends nothing for Left and Right, so **Alt with an arrow** is SHIFT
  with it; Alt+0 is SHIFT+0 (lower case) and Alt+`@` SHIFT+@ (pause a
  listing). The keys the PicoCalc sends only with Shift are their base
  key's cell with SHIFT: End is SHIFT+←, PgUp and PgDn SHIFT+↑ and ↓,
  Home SHIFT+CLEAR, Insert SHIFT+ENTER, Break SHIFT+BREAK. Either Shift
  alone is SHIFT, for games. Ctrl is CLEAR (M13). That Alt+0
  arrives as `0` with Alt held is read from the MCU's source, not yet
  seen on a device (M7).
- **The emulator's keys** are on Alt and the function keys, which the
  guest lacks, and the Alt layer cannot be taken by a game layout
  (EL §7.2).
- **Game layouts** are overlays, built-in ones generic and named by
  shape, per-title ones text files on the card (EL §7.3). The built-ins
  are chosen by surveying the archive, not guessed (EL §7.3). They come
  with the keyboard joystick in M12; M5 copied no layout code.

**As built in M12** (2026-11-16): pico-oric's layouts (`keymatrix.h`,
`keylayout.c`, `keymapio.c`). A layout binds PicoCalc keys, by physical
key, to CoCo cells or to a joystick's lines; Alt and the function keys
stay the emulator's. **The survey**: M12's archive run counted, for
each title, reads of a joystick's comparator after the DAC had moved,
and reads of the keyboard's rows with one column alone driven low made
by code outside BASIC's ROMs. Of 1,025 titles that ran, 210 read a
joystick in their first minute (207 the right stick, 129 the left), and
258 read the keyboard a column at a time for themselves, most often
column 0 alone (103 titles; ENTER is there), every column (45), column
7 alone (24; SPACE and SHIFT) and the arrows' four (6). No shape of
letter keys stands out, so **the built-ins are the keyboard joystick
alone**, `JOY RIGHT` and `JOY LEFT`, and the rest is the card's:
`/coco/keymaps/*.map`, one `<PicoCalc key> = <CoCo key>` a line, the
targets the keys' names and `JR-LEFT` to `JL-FIRE`, with `name =`,
`tapes =` and `carts =`, whose files are separated by commas since the
archive's names have spaces. Putting a tape in the deck or a cartridge
in the slot that a layout names chooses it, and the menu says so. The
Setup page's Keys row goes round them, and `layout =` in the settings
file names the one a save keeps.

**M13** (2026-11-17): a capital is its letter with the PicoCalc's Shift
as the CoCo's SHIFT. Until then the capitals took SHIFT away, so that a
PicoCalc letter typed the CoCo's whatever its case; in upper case the
ROMs decode a letter the same either way, but in lower case (BASIC
after SHIFT+0, OS-9 after CLEAR+0) no capital could be typed. A capital
sent without Shift, under the PicoCalc's Caps Lock, has no SHIFT, and
in lower case types the small letter. `test_keyboard` types a, then
Shift+A, in lower case and reads back $61 and $41.

**M13** (2026-11-17): **Ctrl is CLEAR.** The CoCo 1 and 2 have no CTRL,
and OS-9 takes CLEAR as its control key: CLEAR+0 turns the upper-case
lock on and off, and CLEAR with a letter is that control character.
Ctrl had reached nothing, and a key pressed with it typed nothing; now
it holds CLEAR while it is down, at least `COCO_KEY_MIN_FIELDS`, and a
key pressed with it waits a field behind it, as Shift holds SHIFT
(EL §7.1). The PicoCalc sends a key with Ctrl unchanged (HW §6.3), so
any key will do. Tab stays CLEAR too. In BASIC, Ctrl alone is CLEAR and
clears the screen. `test_os9` types Ctrl+0 at NitrOS-9 and finds `ECHO`'s
letters in lower case after it, and in capitals before it.

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

**As built in M12** (2026-11-16), XRoar's wiring (§18): PIA0's CB2
chooses the stick, low the right and high the left, and its CA2 the
axis, low X and high Y, the multiplexer's two select lines; PA7 reads
high when the axis is at the DAC's level or above; the right stick's
button pulls PA0 low and the left's PA1, whatever the columns. An axis
is 0, its centre or 65,535 (`coco_joystick_set`). Color BASIC's
`JOYSTK` reads 0, 31 and 63 from them in each ROM, and a centred stick
31 (`test_keyboard`). **With a button held, BASIC takes no key at
all**: KEYIN, having found a new key, reads the rows with no column
driven and gives the key up if any is low ($A211 in 1.1), read off the
ROM and seen in all three. So fire is `]` and not a key BASIC needs,
and a program that reads the button is not at the prompt.

**Analog** (the owner, 2026-11-22): a layout may move its stick while
a line is held and leave it there, as the owner's pointer does in
picocalc-quest, in JOYSTK's 64 places (`keylayout_t.analog`). A run
moves a place as it starts, so a tap is one place, then at 8 places a
second rising to 48 over 0.6 s, the pointer's 40 to 240 pixels a
second across 320 scaled to 64: across in 1.5 s. Opposite lines held
together stop the run where it is; a new direction runs from where the
stick is. Each place is the middle of its step of the DAC, place 31
the centre a digital stick rests at (`coco_joystick_put`). The stick
stays where it is through a park, and a digital layout centres it
again. The built-ins are four: `L ANALOG`, `L DIGITAL`, `R DIGITAL` and
`R ANALOG`; M12's `JOY RIGHT` and `JOY LEFT`, which a settings file may
still name, are the digital ones. `test_keymap` holds the run and
`test_keyboard` reads every place through JOYSTK in each ROM.

---

## 12. Media: ROMs, cartridges, tape, disc, snapshots, settings

### 12.1 ROMs

No permission covers them, so none ships (EL §8.1, D10). The user puts
them in `/coco/roms/`; the README names each file, its size and its
SHA-1. The emulator **identifies ROMs by SHA-1** and knows the common
alternate packagings (the CoCo's two 8 KB ROMs as one 16 KB file). A missing ROM gives a page naming the files, never
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

**As built in M9** (2026-11-15): no `cart.c`; the cartridge is a few
lines of `coco.c`. An image of up to 16 KB loads at `$C000` with
`coco_load_rom`; a power-of-two image smaller than the window repeats
through it, as XRoar's ROM bank does, and any other size is followed by
`$FF`. A loaded ROM autostarts unless the port says not (a DOS will not,
M13): the CART line on the Q clock gives PIA1 CB1 an edge every cycle, so
its flag is set again a cycle after anything clears it (a read of port
B's data, RESET, a write to CRB). XRoar toggles the line every 100 ms
instead; BASIC, which waits for the flag, cannot tell. Color BASIC 1.3's
FIRQ handler ($A0F6) waits two delays of 65,536 loops and runs its start
again before `JMP $C000`, so a cartridge starts about 2.4 s after power-on
on the host (field 145 with 64 KB and Extended, `test_cart`), as on the
real machine. Each of the owner's five `.ccc` images starts on the host.

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

**Phase 1 as built in M9** (2026-11-15), `tape.c` and `cas.c`, with the
whole design in `tape.h`. The routines were read off both ROMs: Color
BASIC 1.1 and 1.3 have the same CSRDON ($A77C), BLKIN ($A70B), WRTLDR
($A7D8) and BLKOUT ($A7F4) at the same addresses, behind the vectors at
$A004–$A00C, so one table serves both and the trap stands aside for any
other image by SHA-1. Each routine is trapped a few instructions in,
after its ORCC and what it calls first, and is left at its own last
instruction or two, which the ROM runs: BLKIN at the SUBA that compares
the checksum, so the ROM sets its own error code. CASON's delay (the
motor settling, ($8A) loops) is trapped too, and takes no time.

The trap is not on every instruction. A JSR, BSR, LBSR or JMP into page
$A7 is one compare in the CPU (`bus_jump`), and at a routine's entry it
puts that page on the slow path until the routine's trap point has been
served; the opcode fetch at the trap point is the trap. A page always
slow was tried first and cost the idle prompt 58 % more host time an
instruction on the workstation, because BASIC's cursor waits in CASON's
delay loop at $A7D3; armed only on entry, the prompt costs the same as
with no trap (7.0–7.2 ns an instruction either way, 2026-11-15).

The deck holds the whole CAS in a 64 KB buffer the port lends the core
(§16's tape buffer), and serves every block at once from it. The port is
asked, with the CPU stalled on the HALT line, only when the deck cannot
answer: CSRDON with the deck empty, carrying CLOAD's name from $1D1, or
a read that runs off the tape's end. A save is appended at the tape's
end, and a save with the deck empty starts a new tape. The port's
policy, by name and the rewind before giving up, is pico-oric's
(`tapeio.c`).

`test_tape` holds the trap's state to the ROM's own routine, the
registers but B, all RAM but $83, $84 and the stack below S, and the
PIAs' registers: BLKIN against the ROM's with only its sync search and
byte reader hooked, for a good block, a bad checksum, a byte that would
not store and an end block; BLKOUT, WRTLDR and CASON against the ROM's
own, unhooked. Eleven bugs planted one at a time each failed it. `CSAVE`
and `CLOAD` round-trip a program in 64 KB with Extended and in a 4 KB
Color BASIC 1.1, `CSAVEM` and `CLOADM` 256 bytes, and the six CAS files
the owner staged each load without an I/O error. The XRoar trace diff
still agrees through boot and typing.

**Phase 2 as built in M12** (2026-11-16), `cassette.c`, with the whole
design in `cassette.h`. **How the ROM times a bit**, read off Color
BASIC 1.1 and 1.3, whose routines are the same bytes: the byte writer
($A82A) sends a byte from bit 0, each bit one cycle of a 36-step sine
on the DAC, every step for a 0 and every other for a 1; the reader
($A755) counts passes of a 28-cycle loop across a whole cycle of PA0
and calls it a 1 under 18 ($8F). The writer run on the host with every
DAC write timed gives, in CPU cycles between the DAC's crossings of
its middle: high 207 for a 1 and 396 for a 0; low 227 after a 1 and
417 after a 0, one more when the next bit is a 1; after a byte's last
bit the caller's cycles, 33 in the leader and BLKOUT's 25, 27, 27, 34
(32 with no data), 38, 36 and 18 by the byte's place; before a block
BLKOUT's sum at 11 cycles a data byte, and its caller's own cycles,
which differ between a block that follows its own leader (81), CSAVE's
blocks and CSAVEM's; and 524,365 cycles from the relay closing to a
leader's first rise. About 1,100 and 2,060 Hz, not the 1,200 and 2,400
of §18's row, which is settled so.

**The player** gives those counts for the deck's image as it is, from
where the head stands, and adds what a CAS leaves out, the silence
before each leader; which writer's figures a block takes it reads from
the file's name block. PA0 is brought up to date when PIA1 is read and
at no other time: nothing but the ROMs' polling loops looks at it, so
no slice ends for the tape and a machine with fast loading on pays
nothing. **The recorder** times the DAC's crossings while the relay is
closed, shifts bits in until a byte is the sync as BLKIN does, and
appends each block whose checksum is right with the leader bytes
before it, so that the image of a save is, byte for byte, the one the
trap writes. A block cut short is not kept. **The relay** is PIA1 CA2
driven high; the player holds its place while it is open, and a span
of it in which the ROM wrote leaves the player where it was.

**One trap, two speeds** (EL §8.3). `fast_tape = off` (the Setup
page's Fast tape) leaves the trap as the port's cue alone: CSRDON with
the deck empty still stalls for the tape CLOAD named, then stands
aside, and CASON's delay passes as it does on the machine. A reader
that has read PA0 32,768 times past the tape's end, about a second of
BLKIN's loop, stalls as the trap does at a tape's end, for the port's
rewind or its giving up, where the real machine would wait for ever.
**Turbo**: core 0 runs unpaced while the relay is closed with fast
loading off, the audio queue fed silence (EL §9.3). A state is not
saved while the signal runs: the player's place within a byte is not
state (§12.5).

`test_cassette` holds the player's walk of the recorder's image to the
ROM's own save, edge for edge, the silences among them, for a short and
a long CSAVE, an ASCII save, a data file and two CSAVEMs, in Color
BASIC 1.1, 1.3 and Extended; holds the image to the trap's; and loads
each back off the signal with the ROM's own reader. `test_recorder`
drives the DAC with no ROM, for what no ROM's writer shows: a wrong
checksum, a block cut short.

Not modelled: a loader that expects other tones or a faster rate than
the ROM's; the tape's own level on the sound bus, which stays at its
middle (§10); a writer other than the ROMs', whose gaps between blocks
are played as CSAVE's.

### 12.4 Discs

The RS-DOS controller cartridge, a Western Digital WD1793 behind a
latch (DragonDOS's, with its WD2797, went with the Dragon, D6):

| | RS-DOS |
|---|---|
| Chip | WD1793 at `$FF48`–`$FF4B` |
| Latch (drive, motor, density, side, precompensation) | `$FF40` |
| INTRQ | NMI, gated by the latch |
| DRQ | **HALT**: the CPU is held until the next byte is ready |

The decodes are as XRoar's source has them; the registers are read off
the DOS ROM before they are constants (EL §8.4, §18).

The chip is the Oric's `wd1793.c`, and its design is kept whole (EL §8.4): **the disc turns in
oscillator ticks**, a search waits for its ID to come round, a transfer
moves a byte a byte-time and loses one the CPU missed; every timed step
is an event the run loop stops at; the chip holds one raw track and
**asks the port for a track at a time**, served with the guest parked.
RS-DOS's HALT pacing is the CPU's stall (§7.1) ended by the chip's
next-byte event.

Images: **JVC/DSK**, sector images from which
`mfmdisk.c` builds tracks; a read-only file is a protected disc; a short
image reads zeros past its end and grows when written (EL §8.4). DMK,
which stores raw tracks, is deferred (§20).

What READY and the motor mean to each DOS, and how each notices a disc
change, are read from the DOS ROMs (EL §8.4).

Disk BASIC does not run on a 4 KB CoCo (the owner, 2026-11-15): the
Machine page refuses the RS-DOS cartridge with 4K, by the rule
`roms_basic_for` keeps (§14). The archive's images are
surveyed before any check the chip makes is trusted, and the whole
archive is held to XRoar by screen, since XRoar's disc timing differs
(EL §8.4).

**Stepping out stops at track 0** (2026-11-21). The type I commands run
the datasheet's flow as XRoar's `wd279x.c` does: before each pulse out,
TR00 is looked at, and a head already at track 0 ends the command with
the track register 0, a step's time later, the extra step being XRoar's
("real code fails without it"). A Restore is a Seek from `$FF` to 0. The
chip had taken a Seek's steps from the registers alone, so Tandy OS-9's
driver, which homes the head by a Seek out from a track register of
`$FF`, pulsed on for 255 steps, 7.65 s at 30 ms, every time it did so.
Found by the disk archive run (M13): every Tandy OS-9 title reached
XRoar's picture 5 to 20 s late. `test_disk` holds the Seek and the Step
out at TR00, each shown to bite by a planted bug. The whole archive run
again with the fix (2,244 titles, `out/m13/archive-all-tr00`): the
pictures identical to XRoar's at the capture rose from 1,004 to 1,031,
every Tandy OS-9 title started now matches XRoar's picture at the
capture or within half a second of it, and no title that matched before
stopped matching. On the board (the owner, 2026-11-22, the UART0 build):
The Interbank Incident reached its speech cartridge question about 30 s
after `DOS`, as under XRoar, where the build before the fix took about
45 s.

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

**As built in M10** (2026-11-15): pico-oric's container and rules
(`snapshot.c`, `snapshot.h`), "COCOSNAP" version 1: a 256-byte state,
the 64 KB of RAM, and the media. The state holds the 6809 (its wait,
latch, active and stale, which a CWAI or an interrupt's entry leaves at
a boundary), the four PIA ports, the SAM, the clock (the tick count at
`cyc0`, the ticks a cycle takes, the field's start, HS's account, each
PIA port's rise due), the sound bus and the trap's hot page; what a
power-on chooses (machine, RAM, the ROMs fitted, the cartridge's CART
line, the trap); the field's shape, which is the build's; and the
SHA-1 of the ROM halves at $8000 and $A000 and of the cartridge's
16 KB window. A load works out the page table, the CPU's lines and its
attention, releases the keys and restarts the sample grid at the
restored tick (`coco_restored`). The time is absolute ticks (§13.2),
so a state taken anywhere, not only between fields, resumes where it
stood. The media are the deck's file and place, the cartridge's file
and room for four drives' (M13), written empty. A state for another
RAM, BASIC or cartridge loads into a machine powered on as it (EL
§8.5), its ROMs from the boot's job and its cartridge from the card by
name. A Dragon 32's state, which M3 to M11 could write on the host, is
refused as not a snapshot (D6).
`snapio.c` keeps four slots in `/coco/snaps/` through `.NEW` and a
rename; the Snapshots page (§14) saves, loads and deletes them. Tested
by `test_snapshot` on a ROM of its own, and `test_snapshot_rom` over
the real ROMs, `milestones.md` M10.

### 12.6 Settings and the card

```
/coco/
  coco.ini         settings: key = value (EL §8.7)
  roms/  tapes/  disks/  carts/  snaps/  keymaps/
```

**Disks, not discs** (the owner, 2026-11-15): in North America, and so
on the Color Computer, it is *disk*. The card's folder is
`/coco/disks/`, and what the user sees says *disk*: the menu's Disks
item and page, and the status line.

**Folders** (the owner, 2026-11-22): the Disks page browses
`/coco/disks/` and the folders in it, so a CoCo SDC card's tree can be
copied there whole; the CoCo SDC itself is not emulated (XRoar 1.13's
is a stub, and no SDC-DOS is needed to run the disks). The page lists
one folder: a `..` row with the folder's path inside one, its folders,
then its images, each in name order without regard to case
(`dirlist.c`, `test_dirlist`). Enter opens a folder or inserts an
image, Backspace goes up, never above `/coco/disks/`; the page opens in
the folder of the disk in the drive, and a new disk is made in the
folder shown. Up to 320 entries are held, their names in one 8 KB pool,
so long names cost only their length: the SDC image's 8.3 names fill
4 KB at its largest folder, 306 entries. A folder that fills either
says how many of how many it shows. Other files, hidden ones and
macOS's `._` shadows are not listed. A disk inserted from a folder is
saved in `boot_disk` and in a save state by its whole path. Save
settings writes each drive's disk: `boot_disk` for drive 0, as before,
and `boot_disk1` to `boot_disk3` (the owner found drive 1's not saved,
2026-11-22).

The family's settings file and rules without change (EL §8.7): every
default in one function, a wrong line changes nothing, saving is a
deliberate menu action that **edits the user's text in place**, the
output parsed back before it is written. Every write to the card goes
through `name.new` and a rename with recovery on load (EL §8.6). No
flash writes.

**As built in M9** (2026-11-15): pico-oric's parser and rewriter
(`settings.c`, `test_settings`) with the CoCo's keys, `basic`, `ram`,
`cart`, `volume`, `perf`, `status`, `backlight` and `boot_tape`, which
`settings.h` lists; `settingsio.c` reads and writes `/coco/coco.ini`
through `/coco/coco.new`. The boot's card job mounts once for the
settings, the ROMs, the cartridge and the boot tape. `machine` is
unknown (D6). **M12** (2026-11-16) adds `layout`, `standard` or a
layout's name, and `fast_tape`, on by default; a layout the card does
not have is said on the status row and the standard keys kept. Files are written with FatFs's fixed date
(`FF_FS_NORTC`); the DS3231's time is used only by the SmartWatch
(§12.7). **M13** adds `vdg` (§9.2) and `smartwatch` (§12.7).
**2026-11-22** adds `artifacts` and `artifact_phase` (§9.5), spelt as
the CoCo's manuals and its users spell the word; the code and these
documents keep *artefact*; and `disk_sounds` (§10).
**2026-11-24** adds `background`, `dark` or `black`, and the optional
`color_` keys, one for each of the VDG's fourteen colours, six hex
digits, which only the file sets and a save keeps as written (§9.5).
An empty colour is refused, since `#00FF00` leaves one.

### 12.7 The SmartWatch

**As built in M13** (2026-11-17), at the owner's request. Tandy's OS-9
Level One 2.01 has three clock modules on its second disk's /MODULES:
`clock.60hz`, the one its boot disk carries, which counts the field
interrupt from the time typed at `TIME ?`, and `clock_tc3` and
`clock_bb`, which also read the date at boot from a Dallas DS1216
SmartWatch. Both hold the chip's 64-bit key, $C5 $3A $A3 $5C twice;
`clock_tc3` finds it at $FF7C, and `clock_bb` at $FF5C, selecting a
Multi-Pak slot through $FF7F first, which RS-DOS's decode at
$FF40–$FF5F rules out here. What "tc3" and "bb" were is not known.

`smartwatch.c` is a DS1216 at $FF78–$FF7F, as `clock_tc3` drives it
(the module read, 2026-11-17): a bit written is a read with A2 low, A0
its value; a bit read is a read with A2 high, on D0. Shut, a read with
A2 high starts the key again, and a wrong bit does too; matched, it is
open for 64 bits of registers, read or written. The registers are the
time the key matched: hundredths, seconds, minutes, hours, day of week,
date, month and year, in BCD. **The year** is the years since 1900, its
tens digit up to 15, so 2026 is $C6: OS-9 Level One keeps the year so,
its 2.01 SETIME takes four digits (`SETIME 2026/11/17 13:45:00`; given
two, it reads "99/12" as 9912), and `clock_tc3` writes 126 as $C6 by
its own sum and reads it back as 126. The chip keeps no century; a
register of 26 shows 1926. A set is kept as an offset from the host's
time until power-off, and never written to the board's clock.

The host's time is local time, from the shell first (`rtc.c`): the
PicoCalc shell keeps UTC in the RP2350's always-on timer, which runs on
through the reboot into this program, and leaves the local offset it had
in POWMAN's scratch registers (`picocalc_shell_time`,
`third_party/picocalc-shell`); read whenever wanted, with no bus
traffic, and never set here. Without it (the loader's menu after a power-on), the DS3231 on `i2c0` at GP4/GP5 (HW §1.1),
which holds UTC as the shell writes it, read at bring-up and once a
minute in the builds that leave those pins free, the release build and
the UART0 one (`PICO_COCO_DS3231`), and taken as UTC, no offset being
known; with neither, 2000-01-01 at boot. The boot log names the source
and the offset, and so does the Setup page when the SmartWatch is turned
on. Core 0 gives the time, and `smartwatch` from the settings or the
Setup page, to the machine before each field. Fitted or not, nothing
else in the CoCo answers at $FF78–$FF7F.

`tools/os9-clock.py` makes the boot disk: OS9Boot's `Clock` replaced by
`clock_tc3`, and startup's `SETIME` a comment. `clock_tc3` is 85 bytes
longer, the 2.01 system disk's OS9Boot has 31 spare and only 49 sectors
are free, so `--drop T1,RS232` leaves out the bit-banger port's
descriptor and driver (§20); the tool moves OS9Boot to a free run where
there is one. `test_smartwatch` holds the protocol and the calendar,
with the controls of a wrong bit, a read in the key and no chip fitted;
`test_os9` boots the disk `fetch-os9.sh` makes so: `DATE T` gives the
host's date, 2026, and `SETIME` sets the chip and `DATE T` reads it
back; with no SmartWatch the same disk reads nonsense. On the board (the
owner): `DATE T` right from the shell's time, 2026-11-17, and from the
DS3231's after the loader's menu, as UTC, 2026-11-22. No DS1216 or
DS3231 read here was compared with a real one.

---

## 13. Timing

### 13.1 The field

`coco_run_field` splits the field at FS's edges, so guest instructions
run while FS is in its flyback state and the field interrupt is a level
with a duration (EL §5.6). FS falls as line 230 begins, counted from
power-on (13 lines of blanking, 25 of top border, 192 active), and rises
as the next field's line 0 begins, as XRoar counts them. The trace diff
holds this core to XRoar's field interrupts for 50 guest seconds (M3).
The line counts are the MC6847 datasheet's for NTSC. The snapshot is
taken where FS falls, since each line is fetched as it is shown (§9.3);
until 2026-11-22 it was taken where the next active line begins.

From M3 to M11 the Dragon 32 ran on the host on XRoar's PAL field of 312
lines. With the Dragon removed (D6, 2026-11-16) the field's shape is
`config.h`'s constants (`COCO_LINE_TICKS`, `COCO_FIELD_LINES`,
`COCO_ACTIVE_LINE`, `COCO_FS_FALL_LINE`, `COCO_HS_LOW_TICKS`), no longer
fields of `coco_t` set at power-on, so the field loop's divisions by
the line are by a constant.

A guest loop on the host must see FS low and escape once a field, and a
redraw begun at the field interrupt must be complete in every snapshot,
with the wrong field shape as a control that fails (EL §5.6).

**As built in M4** (2026-11-15): `coco_run_field` runs on past FS's rise
to the next field's first active line (line 38), and the snapshot is taken there; the
first field after power-on runs from tick 0. `test_video` holds the
rules above with a guest loop and a 58-line redraw, and snapshots taken
where FS rises as the control. The trace diff agrees as before, since the
split adds a boundary and moves no instruction.

**Since 2026-11-22** a field's run ends where FS falls (§9.3): it runs
the last field's blanking, FS low, then FS rising as this field begins
and the SAM's counter loading (`coco_run_blanking`, which a test that
steps a field's active lines calls alone), then this field's active
lines, to its fall. The first after power-on runs from tick 0 to field
0's fall. `test_video`'s control for the redraw now walks RAM where FS
rises, as the snapshot did before each line was fetched as it ran. The
keyboard's measured holds moved with the end (`test_keyboard`): a key
goes down part of the way into a run and up as one ends, so 1.1 and
Extended need 2 fields where they needed 3, and 1.3 needs 3 where it
needed 2; the replay's 4 and 2 type all three.

### 13.2 Clocks

One master oscillator: 14.31818 MHz. Everything is counted in its ticks
(§8.2), and **the tick count is the only clock** (EL §4.3). The values
that depend on it (ticks a line, lines a field, the audio sample period,
tape half-cycles, the disc's byte time) are computed once at power-on and
kept where they are used (EL §9.3). The field rate is 60 Hz, a constant (EL §9.1).

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

**The power switch** (the owner, 2026-11-22): the menu's Power off/on
and Alt+O power on the running machine again, its RAM cleared, so
BASIC starts cold, with the tape, cartridge and disks left in; the
menu's Reset and Alt+K are the reset button, a warm start. Refused, as
Apply is, while a recording is not on the card. Alt+O parks the guest
for no work of core 1's (`PARK_POWER`), so the power-on comes after a
park, where core 0 re-paces, as the menu's does.

---

## 14. User interface

The family's (EL §10), by D5:

- **Boots straight into the guest.** One Alt chord opens the menu, which
  parks the guest; function keys open its pages directly.
- **Pages**: Tapes, Disks, Snapshots, Keys (layouts and the keyboard
  joystick), Machine, Settings, About. Where the Atom and the Oric have
  an item, it is in the same place under the same key.
- **The Machine page** stages changes and applies them by a restart that
  behaves as a power-on: BASIC (Color BASIC alone, or with Extended),
  RAM (4, 16, 32, 64 KB), CPU (6809; 6309 from its milestone),
  cartridge (none, a ROM file, RS-DOS). ROMs are checked on the card first, and it
  refuses while an unsaved recording exists.
- **Pause**, **New tape**, a **status row** naming the first problem, the
  **About page** with the version from `git describe` at build time
  (EL §10).
- **Return to the shell**, when UF2 Loader started the image (D7).
- The firmware names no title (EL §10).

**As built in M9** (2026-11-15): pico-oric's `menu.c`, `park.c` and
pause, on the VDG's 32 by 16 cells in capitals. The main page has the
family's nine items in their order; Disks and Snapshots say which
milestone brings them (M13, M10). The Tapes page ejects, rewinds, makes
a named tape and inserts any CAS in `/coco/tapes/`, showing each one's
first file. Setup has the two lines, the backlight and the volume. The
Machine page stages BASIC (Extended or Color), the RAM (4, 16, 32,
64 KB) and the cartridge (none, or any `.ccc` or `.rom` in
`/coco/carts/`), and Apply powers on as staged; it refuses while a
recording is not on the card, and refuses Extended without its ROMs.
**A 4 KB CoCo runs Color BASIC 1.1 alone** (the owner, 2026-11-15): never
1.3, never Extended, which needs 16 KB or more and 1.3 under it, and
never Disk BASIC (§12.4).
`roms_basic_for` (`roms.c`, `test_roms`) is the one rule the boot, the
power-on and the Machine page take it from; the card job keeps 1.1's
image apart from 1.3's for it. Setting RAM to 4K on the page sets BASIC
to Color 1.1; `ram = 4` with no `bas11.rom` on the card boots 16 KB
and says why. There is no machine or television choice (D6). The status line
shows the deck and the cartridge (`status_format`, `test_status`).
F6's screenshot is not yet in.

**As built in M10** (2026-11-15): the Snapshots page, F3, pico-oric's
row for row: the slot (1 to 4, chosen with < > on any row), Save, Load
and Delete, and every slot's state. A load refuses while a recording is
not on the card, as Apply does; a state for another machine, or one
loaded while the machine waits on the tape, powers on as the state's
machine first, and one whose second pass fails powers the machine on
again rather than resume it half loaded. A tape the state names that
the card no longer has leaves the deck empty and says so on the status
line.

**As built in M12** (2026-11-16): Setup gains the siblings' two rows.
Keys goes round the standard map, `JOY RIGHT`, `JOY LEFT` and the
card's layouts, read again each time the menu opens, and the page
shows the bindings of the one in force; a layout chosen by a tape or
cartridge that names it is said on the status row, and Save settings
does not keep it. Fast tape, off, plays the tape's signal (§12.3). The
status line says `PLAY` or `REC` after the tape's name while the
signal runs. The Keys page of the list above is this row, as on the
siblings.

**2026-11-22**: Setup gains Artifacts (off, simple, full) and Phase
(blue, red, random, with the phase drawn now shown beside random),
§9.5; the main page gains Power off/on after Reset (§13.3), and the
help page Alt+O. The help page had fifteen rows of keys under its
title on a page of sixteen, so the last were never shown; the function
keys now share two rows, and each page's rows are checked against the
status row at compile time. The Keys value moved into the other rows' column, a
layout's name cut to 11 characters there, and the main page dropped its
tape line (the owner). Keys runs, in a row, `L ANALOG`, `L DIGITAL`,
the standard map, `R DIGITAL`, `R ANALOG`, then the card's layouts, so
from the standard map Left or Right chooses a stick and a second press
its analog way (§11.4; the owner); Left and Right stop at the ends, and
Enter steps right and comes round. Setup's last row, Disk sounds, turns
the drives' steps and motor on and off (§10).

**2026-11-24**: Setup gains Background (dark, black) after Phase,
§9.5. Its twelve rows reach the status row, so the bindings of the
layout in force are no longer shown under them; there had been room
for only the first two.

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

Every tape, cartridge and disc image of the CoCo archive, run
on the host for a fixed guest time, each with its last frame and a
one-line report (EL §11.6): undocumented opcodes reached and the
instructions before the first (D12); fields with more than one video mode
(§9.4); which keys titles test and which joystick they read (§11.3);
whether CAS files come gzipped; the largest file, which sizes the
buffers. Failures are classified before any is fixed.

**As built in M11** (2026-11-16): the Color Computer Archive's
cartridges and cassettes (colorcomputerarchive.com/repo/, the owner's
choice), fetched by `tools/fetch-archive.sh`, its CoCo 3 titles and
WAV-only zips left out: 106 cartridges and 953 tapes. A living archive
has no one checksum, so each file's SHA-1 goes to a manifest beside it.
`tools/coco-corpus.c` runs each on a 64 KB CoCo with Extended Color BASIC
for 3,600 fields, the tape served by the trap as `tapeio.c` serves it,
and replays any field that reached an undocumented opcode or changed the
video bits on an active line a step at a time from a copy, for the
lead-in and which bits changed; `tools/archive-run.sh` and
`archive-summary.py` report. The keys and joysticks titles read are left
to M12's layouts. What it found is in `docs/milestones.md`, M11.

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
| ROMs: BASIC 16 KB, cartridge or DOS up to 16 KB | 32 KB |
| Snapshot pool, 3 × ~6.3 KB, and the presenter's shadow; **M12**: each frame 7.1 KB with how its lines are drawn (§9.4) | 26 KB |
| Renderer's table (8 KB worst case) and line buffers; **M12**: two renderers, 17 KB (§9.4) | 10 KB |
| Tape buffer | 64 KB |
| One raw disc track and its build buffer | 16 KB |
| Interpreter and hot callees in SRAM, if the tiers earn it; **34 KB at tier 2, measured in M2** (§7.5) | 30–45 KB |
| Audio ring and queue, log ring, key queues | 16 KB |
| SDK, FatFs, drivers, stacks | 30 KB |
| **Total** | **about 290–305 KB of 520 KB** |

**Measured in M12** (2026-11-16, the tier-2 image with the UART on
port 0): 188,084 bytes of text and 325,928 of bss, 21,796 more bss than
M11's: the second renderer, the frames' line tables, the machine's two
lists of a field's changes, and the card's layouts.

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
 M13 RS-DOS → M15 HD6309 → M16 release  (M14, the Dragons, withdrawn: D6)
```

The CoCo is the machine of every milestone. The Dragon 32 was booted on
the host from M3 as a cheap check that the wiring was data and not
assumption, and frozen on 2026-11-15; **the owner removed it on
2026-11-16** (D6), with the PAL field, and withdrew M14. The numbers of
the milestones after it are kept, since documents and commits cite them.

| M | What | Done when |
|---|---|---|
| **M0** | Repository, the two-target build, `config.h`, CI, `CLAUDE.md`, `milestones.md`; the siblings' files sorted into §5's lists | CI green on both builds; an SDK include in a core header shown to fail the host build (EL §2.1) |
| **M1** | `mc6809.c`; XRoar built by script; the oracle and the behaviour tests | The oracle passes over every opcode and mode; each planted bug fails it; the skip is held to a stepping machine |
| **M2** | **The gate**: the CPU alone on the board (§7.5) | Host cycles per instruction measured; board counts equal the host's; a projected share of core 0 under 60 %, or the levers applied and the result taken to the owner |
| **M3** | The machine on the host: bus, SAM, PIAs, field loop; a CoCo with Color BASIC alone and with Extended | The CoCo boots its real ROMs to `OK` in each RAM size and answers a typed sum, read from video RAM; the trace diff against XRoar runs through boot and typing; a Dragon 32 boots on the host as the second wiring (removed 2026-11-16, D6) |
| **M4** | Video: the VDG through the SAM, every mode and the mismatched pairs, artefact colours, the presenter | Golden images looked at and agreeing with XRoar's rendering; the dirty-band test with its control |
| **M5** | Keyboard: the CoCo's matrix, the mapping table, hold and gap; the Dragon's matrix as a second table (moved to M14 by the freeze) | Every table entry typed through Color BASIC; the long-program test stores every line |
| **M6** | Board bring-up from the copied drivers, each stage its smoke test (EL §13.3) | Test pattern, keys logged, card read, on the owner's Plus 2 W (the owner, 2026-11-15: the one board; the second board is M16's) |
| **M7** | **The CoCo boots on the device**, timer-paced | Extended Color BASIC's banner and `OK` on the panel, a typed program run; the 150-field list equals the host's; the owner has typed on it |
| **M8** | Audio: the level filter, the multiplexer, pacing on the queue | Every sample to 1 LSB on the host; `SOUND` and `PLAY` at pitch on the device; zero underruns; the late path forced |
| **M9** | Card, settings, the menu, the Machine page, ROM cartridges, the tape trap | A game loaded from CAS and from a cartridge image and played; `CSAVE` and `CLOAD` round-trip; the trap's state held to the ROM's own routine |
| **M10** | Snapshots | Restored mid-program into a machine doing something else, identical straight after and 150 fields on |
| **M11** | The perf pass with SRAM tiers; the archive run; the soak | Each feature's cost against a control; D12 and §9.4 decided from the archive; a clean 30-minute soak on both builds |
| **M12** | The tape signal, turbo, recording, the keyboard joystick and game layouts; split-field rendering (§9.4, the owner, 2026-11-16) | Edge-for-edge against the ROM's own save; a recording loads in XRoar; a joystick game played by the owner; the routine split titles of M11's archive run drawn by region and looked at |
| **M13** | Discs for the CoCo: the WD1793, the RS-DOS controller with its HALT pacing, Disk BASIC, JVC/DSK images | Disk BASIC runs `DIR`, `LOAD`, `SAVE`, `DSKINI` and `BACKUP` on the host and the device; the CoCo disc archive held to XRoar by screen |
| ~~M14~~ | **Withdrawn** (D6, the owner, 2026-11-16): the Dragon 32 and 64, DragonDOS and VDK images | — |
| **M15** | HD6309: **deferred** by the owner, 2026-11-22 (§20) | Its oracle passes in both modes; a 6309 program runs on the device |
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
| The NMOS 6809's undocumented opcodes and what software reaches them | Trapped. **M11** (2026-11-16): reached by 37 of 1,059 archive titles (§15.4); implemented as XRoar's model, held by the oracle over every byte of the three pages, the chained prefixes and HCF (§7.1). XRoar's model is hoglet67's measurements; no chip measured here | Medium | A real 6809 run against the oracle's trials |
| A published 6809 functional test of standing exists | **None found** (M1, 2026-11-14) | — | Searched in M1; see below |
| Ticks a line and the active line's start | 57 CPU cycles; the datasheet's. **M3**: 912 ticks a line, 262 a field, FS falling as line 230 begins, held to XRoar's field interrupts by the trace diff | Medium | MC6847 datasheet |
| PIA IRQ response time tRS3 (§8.3) | 1 µs, 14 ticks as XRoar counts it; **M3**: the trace diff needs it | Medium | MC6821 datasheet figure 13 |
| The SAM's bit pairs, its address-counter rule for each video mode, its rate rules and the stretched cycle (§8.2) | As §3.2, §8.2. **M3**: the bit pairs, and RAM's row and column in 4K, 16K and 64K modes for 4K, 16K, 32K and 64K chips as XRoar models them, held through boot in every size by the trace diff; rates (M11) not yet. **M4**: the video counter (§9.2), XRoar's model of its dividers and HS clears, held pixel for pixel to XRoar's MC6883 feeding its MC6847 in every pairing of the eight SAM modes with the ten VDG modes, in both colour sets (`test_vdg_oracle`), and the matched pairs to the addresses the datasheet's modes imply (`test_video`). P1 in map type 1: XRoar's model, and so this core, applies it in both map types, so in all-RAM mode with P1 set the two halves of $0000-$7FFF alias; the M3 review read the datasheet as map type 0 only. The Color Computer 2 service manual says P1 "controls a feature which is not used in the Color Computer 2" and "should be cleared at all times" (2026-11-15), so no software for the machine relies on it either way; XRoar's behaviour is kept. The datasheet would settle the chip's behaviour | Medium | MC6883 datasheet; XRoar's traces |
| What unpopulated memory and unused I/O read (§8.1) | **M3**: absent RAM `$FF`; empty sockets and unused I/O the byte at `$FFFF` (§8.1), as XRoar's trace needs | Medium | Schematics; neither assumed `$FF` |
| The PIA wiring of §3.3, bit for bit, on each machine | As listed. **M3, the CoCo**: IRQ from PIA0, FIRQ from PIA1; the RAM strap on PIA1 PB2 (low on 4K, high on 16K, linked to PIA0 PB6 on 32K and 64K); port B's undriven inputs low; PB1 reading the silent sound high; PA0 the cassette, low; PA7 the comparator; PIAs repeating over 32 bytes. Each one the boot trace needs (§15.2). PB1 reads low until XRoar's sound first updates (the first FS rise, or PB1 or CB2 enabling the sound) and the silent sound high after, which the CoCos read only after the first field: XRoar's behaviour, not yet the hardware's | Medium | Schematics; the ROMs executed |
| VDG data wiring: bit 7 A/S, bit 6 INV, INT/EXT from GM0 (§3.4); the mode on PIA1 PB3–PB7 (§3.3) | As listed. **Settled in M4** (2026-11-15), the ROMs executed: PB7 A/G, PB6–PB4 GM2–GM0 and PB3 CSS by Extended Color BASIC's PMODE 0–4 under SCREEN 1,0 and 1,1, each giving the datasheet's matched VDG and SAM modes (`test_screen`); bit 7 A/S and bit 6 INV by BASIC's own screen, read back from the snapshot as the text guest_row reads from RAM, and SET drawing semigraphics 4. INT/EXT from GM0 is XRoar's wiring, held by the VDG oracle; no ROM run selects it | High; INT/EXT medium | The ROMs executed; schematics for INT/EXT |
| The keyboard matrix (§11.2) | **M3, the CoCo**: every letter, digit and shifted symbol typed through the matrix and printed back by Color BASIC 1.3 and Extended 1.1 (`test_boot`). **Settled in M5** for the CoCo (2026-11-15): all 56 cells pressed at the prompt alone and with SHIFT in Color BASIC 1.1, 1.3 and Extended 1.1, and what POLCAT returned read back; the three agree in every cell, the four empty cells of row 6 decoding whatever follows the ROM's table (`test_keyboard`, `keymap_picocalc.c`) | — | Each ROM executed |
| Key hold and gap for each ROM (§11.1) | **Settled in M5** for the CoCo (2026-11-15): 2 fields down in Color BASIC 1.3, 3 in 1.1 and Extended 1.1, 1 up in all, no repeat; the replay's 4 and 2 (§11.1) | High | Each ROM executed |
| The sound bus: each source's gain and offset by the single bit's state, the bus holding its level with nothing driving it, PB1's feedback at 0.3 of 4.70 V, the idle tape at half scale (§10) | **M8**: XRoar's `sound.c` and `dragon.c`, measured there on a real Dragon; the CoCo assumed the same. The trace diff holds PB1's feedback to XRoar through boot, typing, `SOUND` and `PLAY` (2026-11-15), and BASIC's `SOUND` relies on the hold; no CoCo measured | Low | A CoCo's audio output measured; schematics |
| A change of the VDG's mode undone 14 cycles later, part-way along a line, is seen on a real CoCo (§9.4, Sea Dragon's page flip) | Drawn, as XRoar draws it | Medium | The owner, Sea Dragon on a CoCo |
| PIA Cx2 as an input: high on the A side, low on the B side; the pulse modes (§10) | **M8**: as XRoar's PIA has them; the pulse modes not modelled, as no ROM sounds through them | Medium | MC6821 datasheet; schematics |
| The ROMs' tape block routines, their entry bytes and what they leave (§12.3) | **Settled 2026-11-15** for Color BASIC 1.1 and 1.3, read off both ROMs and held by `test_tape` to the ROMs' own routines executed on the host (`tape.h` lists them) | High | — |
| Tape bit timing as each ROM reads and writes it | Two tones, about 1200 and 2400 Hz. **Settled 2026-11-16** (M12) for Color BASIC 1.1 and 1.3, whose writer and reader are the same bytes: the writer's half-cycles counted off its own save run on the host, 207 and 396 cycles high and 227 and 417 low for a 1 and a 0, about 2,060 and 1,100 Hz at 0.895 MHz, with each caller's cycles between bytes and blocks (`cassette.h`); the reader's threshold 18 passes of a 28-cycle loop a cycle. `test_cassette` holds the player to the ROM's save edge for edge | High | — |
| The joysticks: the comparator's sense, which select line is the stick and which the axis, the buttons' row lines (§11.4) | **M12**: XRoar's wiring; BASIC's `JOYSTK` reads 0, 31 and 63 through it in each ROM, and the buttons PA0 and PA1 low (`test_keyboard`). Which stick is the right one, and the comparator's level at the DAC's own value, are XRoar's | Medium | Schematics; a real stick |
| Where a line's pixels begin after HS falls, for a change of mode along a line (§9.4) | **M12**: 254 ticks, XRoar's MC6847 figures (tWHS 64, tBP 70, tLB 120), and 2 ticks a pixel; a write falls at the end of its cycle. Dark Star's heading bar runs from pixel 1 to 240 by it; no real machine compared | Low | MC6847 datasheet; a photograph of Dark Star or Dragon Fire on a real set |
| A PIA1 write's delay to the picture, for a change of mode along a line (§9.4) | **2026-11-22**: 1 µs, 14 ticks, from two pictures of Dragonfire: a low-resolution photograph of a real CoCo, whose wall edges match with about a cycle's delay and not without, and a sharper picture of a later build, whose every edge falls on a byte's boundary at 14 ticks and a pixel late at 16. Dark Star's and Rommel's Revenge's bars come out centred. The MC6821's tPDW, up to 1 µs, is that size. Where the second picture was taken, and whether the delay is the PIA's, the VDG's, or both, is not known | Medium | MC6821 and MC6847 datasheets; a sharper photograph of Dragonfire, or a scope on PB3 and the video |
| ROM file names, sizes and SHA-1s (§12.1) | **Settled** 2026-11-14 (M3) for `bas13.rom`, `extbas11.rom`, `disk11.rom` and `bas11.rom`, the owner's images, each matching XRoar 1.13's CRC-32 (`roms/README.md`); `d32.rom` was in the table until the Dragon was removed (D6, 2026-11-16); other CoCo versions to come | High | The owner's images; XRoar's ROM list |
| Disc controller registers, latch bits, interrupt and HALT wiring (§12.4) | As the table, from XRoar's decodes | Medium | Each DOS ROM read; controller schematics |
| WD2797 against WD1793: which differences each DOS sees | Side select, some status bits | Low | Datasheets |
| VDK and JVC header rules | Community descriptions | Low | A corpus of real files (EL §8.8) |
| Seeds and flags in RAM at power-on (§13.3) | **M3, the CoCo**: none taken from RAM; the warm-start flag `$71`/`$55` fails on zeros (§13.3) | High | Both ROMs read |
| Artefact colours: which pair gives which colour in each phase (§9.5) | Blue and red, swapped by phase. **M4**: XRoar's simple renderer's rule, its colours, and its two phases, KBRW its NTSC default; held to that rule applied to XRoar's VDG output by the oracle; no photograph compared. **2026-11-22**: the simple and full renderers held to XRoar's own 2-bit and 5-bit renderers; still XRoar's model, and no photograph compared | Low | XRoar's table; photographs of a real set |
| The VDG's fetches on a line (§9.2) | **M4**: 42 bytes in the 32-byte modes and 22 in the 16-byte ones, on to the right border, each counted by the SAM; XRoar's VDG | Medium | MC6847 datasheet's timing; a mismatched pair on a real machine |
| The VDG's address through the multiplexer (§9.2) | **M4**: no page bit P, and the first bank, as XRoar's fetch; only a machine with two banks or P set could tell | Low | MC6883 datasheet |
| The colours a CoCo's VDG and MC1372 show (§9.5) | Modelled 2026-11-23 from figure 10's typical levels, the MC1372's 100° and equal gains (its datasheet), the saturation from the burst and NTSC's decode; held at 90° to MAME's capture of a PAL Dragon. White's place depended on the set. **2026-11-24**: not drawn; the owner kept the idealised primaries, and the settings file's `color_` keys give any other | Medium: no capture of an NTSC CoCo; the MC1372's angle is 85–115° from chip to chip | A capture of an NTSC CoCo's composite output |
| The owner's panel's tone response (§9.5, HW §4.12) | Three points per channel by eye, 2026-11-23: half of full code gives about 4% of the light; corrected through `mc6847_curve_t` | Medium: by eye, with line patterns that may be biased; nothing below a quarter of the light | A colorimeter; the panel's own gamma table from the ST7365P's register format |
| The VDG's mode pins while PIA1 port B is an input (§9.3) | **M4**: low, as XRoar's PIA has an undriven pin; seen only before BASIC sets the port's direction | Low | Schematics |
| The DS1216 SmartWatch's protocol at $FF78–$FF7F, and its year as the years since 1900 (§12.7) | **M13** (2026-11-17): as Tandy's `clock_tc3` drives it, read off the module, and the datasheet's key and register order as remembered; held by `test_os9` booting it. The address and what "tc3" was are the module's alone | Medium | The DS1216 datasheet; the card `clock_tc3` was written for |
| The MC6847T1: its glyphs, and GM0 as lower case, GM1 as inverse, GM2 as the border, no SG6, orange for bright orange (§9.2) | **M13** (2026-11-17): XRoar 1.13's model and its ROM, held pixel for pixel by `test_vdg_oracle t1`; OS-9's use of GM0 executed on the host. No CoCo 2B compared, and no T1 datasheet read | Medium | The MC6847T1 datasheet; a photograph of a CoCo 2B's lower case |

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
- **Composite simulation**: the byte-pair table gives the colours
  software drew with (§9.5). XRoar's finer artefact table, dropped
  with it at first, was built on 2026-11-22 at the owner's request as
  the *full* artefacts (§9.5).
- **Video noise** in the SAM's all-fast rate, and bus snow.
- **A faster guest clock** as an emulator option: no common modification
  beyond the SAM's own rate bits (EL §9.3).
- **RP2040 boards**: a memory question first, and unmeasured (Atom §3.1).
- **Internal flash writes; ROMs in the firmware** (D10).

**Deferred**, with what would bring each back:

- **CoCo 3** (GIME, 512 KB, 640-sample modes) and **MC-10** (a 6803):
  machines of their own, after release.
- **The HD6309** (M15, §7.3): deferred by the owner on 2026-11-22, as
  the software of the CoCo 1/2's time did not use it. §7.3 stands as
  the design if it comes back.
- **RAM expansions**: the MOOH (§21, D15) was moved by the owner on
  2026-11-22 to a separate project, an aspirational CoCo build for OS-9.
  Not to be built here.
- **The Dragon 32, 64, 200-E, Alpha and Professional, and PAL machines**:
  removed by the owner on 2026-11-16 (D6). Not deferred; out of scope.
- ~~**The MC6847T1** and its lower case, for later CoCo 2s~~: built in
  M13 for OS-9 (§9.2).
- **DMK disc images; double-sided and 80-track drive types** beyond what
  the first DOS tests need.
- **Sound and other active cartridges** (Orchestra 90, the Games Master
  Cartridge), the multi-pak: the Oric's AY-3-8912 is there to take.
- **Printer output to a file; the serial ports**.
- **WAV tapes**; **importing XRoar snapshots** (EL §8.8).
- **Split-field rendering** (§9.4): decided by the archive run, and
  built in M12 (the owner, 2026-11-16).
- **300 MHz**: if the gate asks, or as a user's opt-in later (EL §9.4).

---

## 21. Proposal: the MOOH's 512 KB

**Deferred, 2026-11-22**: the owner moved this proposal to a separate
project, an aspirational CoCo build for OS-9; nothing of it will be built
here, and D3's *no PSRAM* stands. The section is kept as that project's
starting point.

**Proposed, 2026-11-21; nothing is built.** The owner asked whether the
emulated CoCo could have extra memory, as a CoCo 3 or a MOOH does, and
whether 256 KB fits. This section answers from a measurement and from
XRoar 1.13's `mooh.c`, and asks for one decision (D15). It reopens D3,
which is the owner's to reopen.

### 21.1 What the MOOH is

Tormod Volden's MOOH is a cartridge for the CoCo 1/2, so it stays inside
D6; the CoCo 3, whose memory sits behind the GIME in place of the SAM and
the VDG, is another machine and stays deferred (§20). As XRoar 1.13
models it (`src/mooh.c`, read 2026-11-21; secondary, §18):

- **512 KB**: 64 pages of 8 KB. Not 256 KB: a 256 KB machine would be one
  no hardware matches, which is a reason against it in itself.
- **A CoCo 3-style MMU**: two tasks of eight task registers, 6 bits each,
  at `$FFA0`–`$FFAF` (readable); the enable bits for the MMU and constant
  RAM at `$FF90` and the task at `$FF91` (write-only in firmware 1). Each
  register maps one 8 KB segment of `$0000`–`$FEFF`. Page `$3F` in
  segments 0–6 means *not mapped*: the machine's own RAM or ROM answers.
  Segment 7 is always the cartridge's. Constant RAM puts `$FE00`–`$FEFF`
  and the vectors at `$FFF0`–`$FFFF` in page `$3F`. A reset sets every
  register to `$3F`, so a MOOH at power-on is a plain CoCo.
- **The cartridge overrides the machine's decode** while it answers
  (XRoar's `EXTMEM`; on the real port presumably SLENB, to confirm, §18).
- **An SD card over a 65SPI-B** at `$FF6C`–`$FF6F`, and a banked ROM of up
  to 64 KB, its bank chosen at `$FF64` until bit 4 locks it. XRoar knows
  two images of the SDBBOOT ROM V1, by CRC; neither would be shipped (D10).
- Hard power-on fills the SRAM with a random pattern.

The VDG reads only the motherboard's RAM through the SAM, so the picture
never touches the cartridge's memory (§9.2).

### 21.2 What SRAM has left

**Measured 2026-11-21**, the M13 working tree at tier 2 with the UART on
port 0: 214,840 bytes of text and 348,888 of bss; static RAM ends at
`0x200631C8`, leaving **118,328 bytes** below the scratch banks (the
release build, 120,400). `malloc` is not linked, so all of it is free.

| Expansion | Extra SRAM | Fits? |
|---|---:|---|
| 512 KB (the MOOH) | 448 KB | No |
| 256 KB (no such board) | 192 KB | Only by freeing about 97 KB first: the tape image's 64 KB (`tapeio.c`), the boot's second copy of the ROMs in `g_boot` (24 KB), and the tape list held twice (9 KB). About 20 KB would remain for everything after M13 |

So SRAM can hold a machine that does not exist, at the cost of the last
of the headroom, and cannot hold the one that does.

### 21.3 The proposal

**D15 (proposed): the MOOH, with its 512 KB in the Plus 2 W's PSRAM.**
This revises D3's *no PSRAM*. D3's reason, one image for every RP2350
board, has weakened since the owner's Plus 2 W became the only board
(M6); the other boards would run without the MOOH, as a 4 KB CoCo runs
without Disk BASIC (§12.4).

- **The bus.** The page table (§8.1) already reaches memory through
  pointers, so a task register's write repoints the 32 pages of its
  segment and nothing is added to an access. `$FF90`, `$FF91` and
  `$FFA0`–`$FFAF` join the slow I/O page; constant RAM's vectors become
  the slow path's concern while it is on. Each page's flags say which
  array it points into, as now, so `coco_copy` and a snapshot's load
  still rebuild the table.
- **The memory.** 512 KB at the PSRAM's cached window, verified at boot
  as HW §2.4 prescribes; a board whose check fails refuses the MOOH on
  the Machine page.
- **Storage.** A CoCo has one cartridge slot and the multi-pak is
  deferred (§20), so **a MOOH machine has no RS-DOS controller**: no
  Disk BASIC, no floppy images, and OS-9 from a floppy (the owner's
  requirement, M13) only on the machine without it. The MOOH's own
  storage is its SD card, which is a new device: the 65SPI-B and the SD
  card's SPI protocol, serving an image file on the PicoCalc's card a
  sector at a time with the guest parked, as tracks are (§12.4).
  Whether a NitrOS-9 build for the MOOH exists, and what it boots from,
  is to be confirmed before this is planned (§18).
- **Snapshots.** "COCOSNAP" version 2, with the task registers and the
  512 KB. A slot grows to about 580 KB, and four of them to 2.3 MB; the
  save's time is measured, not assumed.
- **The oracle.** XRoar runs a MOOH, so the trace diff (§15.2) extends to
  the MMU through boot of the SDBBOOT ROM, found on the card by SHA-1.

### 21.4 The risk, and the measurement that settles it

The PSRAM shares the 16 KB XIP cache with every instruction fetched from
flash (HW §2.4). The interpreter is in SRAM at tier 2 (§7.5), but much
else is not, and a guest whose working set is spread over 512 KB (OS-9's
does, by design) misses. The 6809 makes about a million accesses a
second; whether that costs anything is unknown.

Before any milestone is planned: **M2's bench with the guest's RAM in
PSRAM**, against the same image with it in SRAM in the same sitting, the
board at 150 MHz, results to a file. Three workloads: BASIC's boot; a
loop over 64 KB; and, as the control that must show the cost, pseudo-random
accesses over the whole 512 KB. The gate is M2's: core 0's projected
share stays under 60 % on the realistic workloads. If it fails, the
levers are the XIP cache's use by flash code (more into SRAM, which the
freed buffers of §21.2 could pay for) and 300 MHz (§20).

### 21.5 What is not proposed

- **256 KB in SRAM** (§21.2): no hardware to hold it to, and the cost is
  the project's last headroom.
- **A CoCo 3**: the GIME is a machine of its own (§20, D6).
- **A multi-pak** to put the MOOH and the RS-DOS controller side by
  side: deferred as it was (§20), and a separate decision.

### 21.6 A 4 KB variant, after the 8 KB is held

**Proposed by the owner, 2026-11-21**: a map of 4 KB pages in place of the
MOOH's 8 KB, built only once the 8 KB machine is held to XRoar and its
tests pass. It is a variant of this emulator's own, not hardware:

- **No board and no software has it.** NitrOS-9 Level 2 and the SDBBOOT
  ROM program 8 KB pages, so a 4 KB map serves only software written
  for it, and the 8 KB map stays the default.
- **No oracle.** XRoar cannot run it, so it is held behaviourally: with
  each pair of 4 KB registers set to an 8 KB page's two halves it must
  behave exactly as the 8 KB machine, run in the same test; then by tests
  of its own, each shown to fail on a planted bug.
- **The emulator's cost is small.** The page table's pages are 256 bytes
  (§8.1), so a 4 KB segment is 16 entries, and a register's write
  repoints half as many as an 8 KB one.
- **The registers have to grow.** 16 segments a task, two tasks: 32
  registers, and 128 pages need 7 bits. `$FFA0`–`$FFBF` is the obvious
  place, if the CoCo 1/2 leaves `$FFB0`–`$FFBF` free (to confirm from the
  schematic, §18); the page that means *not mapped* becomes `$7F`.
- **What it is for** (the owner, 2026-11-21): **a custom OS-9 kernel**.
  No CoCo 2 program knows the map exists, and none needs to: under
  OS-9 Level 2 only the kernel and the few modules that handle a
  process's DAT image know the block size, and applications see address
  space and memory requests, not blocks. A kernel built for 4 KB blocks
  gives each process memory in half the steps, so less of the 512 KB is
  lost to rounding, and every application runs on it unchanged. That
  NitrOS-9 Level 2's block size is a build-time constant, and which
  modules depend on it, is to be checked against its source before the
  kernel is planned; whether NitrOS-9 Level 2 runs on a MOOH at all is
  still §21.3's open question.
- **The kernel chooses it, not a setting** (proposed): a bit at `$FF90`,
  clear at reset, so that the machine powers on as a MOOH, the SDBBOOT
  ROM and a stock 8 KB kernel run on it unchanged, and only the custom
  kernel switches to 4 KB. Which bit is free is read from XRoar's
  `mooh.c` and the MOOH's documentation first. The snapshot records the
  bit with the registers.
- **Done when** the custom kernel boots OS-9 with 4 KB blocks on the host
  and the device, and an unmodified application runs under it.
