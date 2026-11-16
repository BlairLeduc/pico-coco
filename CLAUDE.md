# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

A **Tandy Color Computer 1/2 emulator for the ClockworkPi PicoCalc**, in C
against the Raspberry Pi Pico SDK, on RP2350 boards only, which will also run
the **Dragon 32 and 64**. The guest is an MC6809E at about 0.89 MHz with an
MC6883 SAM, an MC6847 VDG, two MC6821 PIAs, BASIC ROMs and a cartridge port.
It is a member of the pico-atom family: pico-atom's architecture and MC6847,
pico-oric's later port layer and parts, and a **new 6809 core** in their
style (`docs/design.md` §1, §5).

**Status: M7 is done** (`docs/design.md` §17): the CoCo on the host,
with its picture and its keyboard, and now on the board. The MC6809 core, the SAM, the PIAs and
the field loop boot Color BASIC 1.3 and Extended 1.1 in 16 to 64 KB and
Color BASIC 1.1 in 4 to 64 KB, and the trace diff holds every instruction to
XRoar 1.13's through boot and typing. The SAM's video counter, the VDG and
the snapshot are held pixel for pixel to XRoar's MC6883 and MC6847 by
`test_vdg_oracle`. PicoCalc key events reach the matrix through
`keymatrix.c` and the table in `keymap_picocalc.c`, settled by sweeping
the ROMs (`test_keyboard`). **The Dragon is frozen until M14** (the owner,
2026-11-15): M3's Dragon 32 stays and its tests keep passing, but
nothing is added to it. On the board (the owner's Plus 2 W, the only
board; it needs the UART0 build), core 1 loads Extended Color BASIC's
ROMs off the card by SHA-1, or shows the page naming what is missing,
and presents snapshots in dirty bands with the perf line; core 0 runs
the CoCo a field at a time, paced on the timer, and its first 150 fields
equal the host's (`tools/coco-fields.c`, `-DPICO_COCO_FIELD_LOG=ON`).
The record of each milestone (what was
verified, on which board, on what date, and what was not checked) is in
`docs/milestones.md`. Add to it there.

**Settled decisions** (`docs/design.md` §2, D1–D14, 2026-11-14): a native
emulator from pico-atom, not a port of XRoar (D1); a new CPU core held to
XRoar's `mc6809.c` by an oracle, XRoar never copied into the tree (D2, D8);
one `pico2` image at 150 MHz, no PSRAM (D3); **the CoCo comes first** and is
the default machine, the Dragon second (D6); the family's menu and
`key = value` settings (D5); **ROMs from the SD card by SHA-1**, none shipped,
no flash writes (D10); 256×192 at 1:1 at (32, 64) (D11); undocumented
opcodes trapped and counted (D12); **GPL-3.0** (D9); one branch and pull
request per milestone (D14). Do not reopen these without the owner.

## The documents

| File | Authority on |
|---|---|
| `docs/design.md` | the **guest** and the shape of this project's code: the machines' hardware model, architecture, budgets, milestones, unverified constants |
| `docs/hardware-notes.md` | the **host**: PicoCalc wiring, protocols, timing, measured costs, quirks |
| `docs/emulator-lessons.md` | what pico-atom, pico-jupiter-ace and pico-oric taught about writing any emulator on the PicoCalc |
| `docs/milestones.md` | what each milestone verified, on which board and date, and what it did not check |
| `docs/design-xroar-port.md` | **superseded** (D1); kept for its survey of XRoar. Not a guide to this code |

Read `docs/design.md` before writing emulator code. Its §5 says which
sibling files to copy, adapt or leave, §17 says what each milestone builds
and when it is done, and §18 lists every guest fact that is not yet
confirmed.

**Cross-references are load-bearing.** `design.md` writes `HW §N` for
hardware-notes, `EL §N` for emulator-lessons and plain `§N` for itself. Keep
the section numbers accurate when editing; add new sections at the end of a
section rather than renumbering. `hardware-notes.md` and
`emulator-lessons.md` are **portable**: they travel between projects, so they
may cite each other but never `design.md`, this file or a source file. New
facts about the PicoCalc belong in `hardware-notes.md`, and what generalises
in `emulator-lessons.md`, each written to stand alone (D14).

**Notation:** `$XXXX` is a guest address or value, in documents and comments,
as the 6809 world writes hex. `0x` is a host value, and is what C code uses.
A *cycle* is a 6809 E cycle; a *tick* is one of the master oscillator's,
the machine's only clock (`design.md` §8.2, §13.2).

Documents are written in British spelling and in the plain, measured style of
the existing ones: say what was checked, on what, and when.

## The siblings

`../pico-atom` (Acorn Atom) and `../pico-oric` (Oric) are emulators for the
same hardware, by the same author, under the same licence. `design.md` §5
sorts their files into **copy**, **adapt** and **not used**; where both have
a file, the Oric's is the later version and is the one taken.

- **Read the siblings, never edit them** from this project.
- Rename `PICO_ATOM_`/`PICO_ORIC_` → `PICO_COCO_`, `ATOM_`/`ORIC_` → `COCO_`
  and `atom_`/`oric_` → `coco_`. Change the `design.md §N` references in
  comments to this project's sections.
- **Bring the tests with it.** A copied module counts as reused only once its
  sibling's tests pass here under the new names.
- A copied file brings its `THIRD-PARTY.md` entry.
- Anything that moves into the core, or between the two cores, is searched
  for `sleep_` (EL §2.3).

## Build and test

```sh
# host: src/core/ with the system compiler, no Pico SDK, under CTest
cmake -S . -B build/host -DPICO_COCO_HOST=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host -j
ctest --test-dir build/host --output-on-failure

# firmware: the newest SDK and toolchain under ~/.pico-sdk, unless
# PICO_SDK_PATH names one that exists
tools/build.sh                                          # build/pico/pico-coco.uf2
tools/build.sh -DPICO_COCO_UART=OFF build/pico-release  # the build that ships
tools/build.sh -DPICO_COCO_RAM_TIER=0 build/tier0       # one directory per tier; 2 is the default
tools/build.sh -DPICO_COCO_UART_PORT=0 build/uart0      # UART0 on GP2/GP3: a DS3231 holds GP4/GP5

# hardware, with the Debug Probe's SWD and UART both connected, and the
# Mac's display kept awake (caffeinate -d): a sleeping display wedges the
# probe until it is replugged (hardware-notes.md §2.7)
tools/uart-log.sh 30 out/run.log &   # capture UART1 first, so the banner is in it
tools/flash.sh                       # reset halt + resume, never reset run (HW §2.7)
tools/flash.sh build/uart0/pico-coco.elf   # the owner's board, which has the clock
tools/flash.sh build/uart0/pico-coco-bench.elf   # M2's gate: the CPU alone, timed
tools/uart-type.sh 'PRINT 2+3\r'      # type at the guest over the UART
tools/uart-screen.sh                 # the guest's text screen into the log
tools/perf-run.sh build/uart0/pico-coco.elf out/m7/t2   # workloads; perf-summary.sh reduces them

# the board's first 150 fields against the host's (design.md §15.5)
tools/build.sh -DPICO_COCO_UART_PORT=0 -DPICO_COCO_FIELD_LOG=ON build/fieldlog
build/host/coco-fields > out/fields-host.txt  # compare with the log's "fieldlog" lines
build/host/test/host/page-ppm out/pages        # the missing-ROM pages, to look at

# the trace diff against XRoar (design.md §15.2): XRoar built headless by
# tools/trace/build-xroar.sh, the host build's coco-trace, ROMs in roms/
tools/trace/trace-coco.sh -ram 64 -ext -fields 600 -type "$(printf 'PRINT 2+3\r')"
tools/trace/trace-coco.sh -bas bas11 -ram 4 -fields 600   # a 4 KB CoCo
tools/trace/trace-coco.sh -machine dragon32 -fields 500   # PAL: 50 fields a second

# the golden images (design.md §9.6): look at every changed one before
# committing it
build/host/test/host/vdg-ppm out/golden      # to look at
build/host/test/host/vdg-ppm test/golden     # regenerate the goldens
```

Both targets build under `-Wall -Wextra -Werror`, and CI builds both on every
push, with and without the UART. Run the host build as well as the firmware
one: the core building clean without the SDK is what keeps SDK headers out of
`src/core/`.

- **No ROM is committed.** `roms/` is a gitignored staging area (only its
  README is in git). Host tests will find ROMs there by SHA-1 and **skip**
  without them; CI has none. A skip is not a pass.
- **Tests use no framework**: `test/host/test_util.h` with `CHECK` and
  `TEST_DONE`, one binary per area, exit 77 for a skip. Assert behaviour by
  executing it rather than by comparing two tables in the repository, and
  give a timing test a control that must fail. Plant bugs one at a time to
  show a test bites, and record them.
- **A golden image proves nothing until someone has looked at it.**
- **XRoar is a tool, never source** (D2, D8): `tools/trace/build-xroar.sh`
  clones it at the pinned release into `reference/` (gitignored) and builds
  it headless in `out/xroar`. Its CPU is linked only into
  `test_mc6809_oracle`, and its MC6847 and MC6883 only into
  `test_vdg_oracle`; both skip without the checkout; re-run cmake after
  fetching it. Plant a CPU bug in a scratch copy and run both
  `test_mc6809_oracle` and `test_mc6809_behaviour`: some bugs only one sees.

## Architecture: the parts that are easy to break

- **`src/core/` never includes an SDK header and never allocates.** State
  lives in `coco_t` or in buffers sized in `src/core/config.h`, where every
  fixed capacity lives. `src/port/` is the only place SDK headers belong.
  Checked in M0: `#include "pico/stdlib.h"` planted in `config.h` fails the
  host build.
- **`coco_run` is the seam** (`design.md` §6): it runs whole instructions
  until at least the requested time has passed and returns the true count;
  the caller carries the debt. Never add a "run exactly N" variant.
- **Copy a machine with `coco_copy`, never `=`.** The page table points into
  the struct.
- **Load the ROMs, then power on** (`design.md` §13.3), in one order the
  host harness shares.

## Hardware invariants

From `hardware-notes.md` §10. Each one cost real debugging time on a sibling:

- `spi_set_format()` and the `D/CX` write go **before** CS low (the 40 ns
  CS-high rule).
- Never touch the LCD from an interrupt handler, and never mask interrupts
  around a blit. Audio has a ~3.5 ms refill deadline.
- **Core 1 never calls `sleep_us`/`sleep_ms`**: their alarm IRQ runs on
  core 0, in the middle of the guest. Use `busy_wait_us_32` (HW §9.7).
- **Core 0 never calls `printf` once the guest runs.** It logs into a ring
  that core 1 drains.
- No flash writes: settings go to a text file on the card, written through
  `.new` and a rename.
- `main()` sets the core rail explicitly, because it survives a reset
  (HW §3).
- Build each SRAM tier in its own build directory and check its symbols with
  `arm-none-eabi-nm` (HW §9.8).

## Unverified constants

`docs/design.md` §18 lists every guest fact written from secondary knowledge
or from XRoar's source, with a confidence for each, and §3 is written from
memory. **Do not treat the design as authoritative for a low or medium
row.** Settle the row from a primary source (a datasheet, a schematic, or
best, **the real ROMs executed** on the host harness), record in §18 how and
when it was settled, then make it a `#define`. A constant stays runtime
configuration until it is settled.

## Measurement discipline

Every performance figure in `design.md` is an estimate until a measurement
replaces it. Keep the estimate next to the measurement. Measure each feature
against a control build in the same sitting, and write results to a file.
"Built" and "done" are different words: a milestone with a device step is
done only once it has been checked on the device.

## Comments

Comments cite the document next to any decision that looks arbitrary, e.g.
`(design.md §8.2)` or `(hardware-notes.md §4.3)`, rather than restating the
reasoning. A device hook that does not exist yet is marked where its call
belongs, with a comment naming the milestone (`/* M8: ... */`).
