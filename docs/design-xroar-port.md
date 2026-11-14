# PicoCalc XRoar: design (superseded)

> **Superseded on 2026-11-14** by `design.md`, a native emulator in the
> pico-atom family with a new 6809 core. This document is kept for its
> survey of XRoar's source (§3) and for the cost estimate (§6.4) that led
> to the change. Nothing in it was built.

A port of [XRoar](https://www.6809.org.uk/xroar/), Ciaran Anscomb's Dragon
and Tandy Colour Computer emulator, to the ClockworkPi PicoCalc fitted with
a Pimoroni Pico Plus 2 or Plus 2 W.

This document covers the shape of the port: what is taken from upstream and
how, what stands in for XRoar's desktop front ends, how the two cores share
the work, the memory budget, the milestones, and the constants and
decisions still open. It is written against two portable companions in this
directory:

- `hardware-notes.md`: the authority on the PicoCalc. Cited as **HW §N**.
- `emulator-lessons.md`: what earlier PicoCalc emulators and ports taught.
  Cited as **EL §N**.

It follows the method of `picocalc-quest` and `picocalc-doom` (ports by the
same owner): upstream as a pinned submodule, one patch, and shims (D1).
Those projects are cited as **Quest** and **Doom**, with their own section
numbers where they help.

Plain **§N** points within this document. Guest addresses are written
`$XXXX` and host values `0x…` (EL §14.1). Dates are written in full.
**Estimates are labelled as estimates**; everything else is read from
XRoar's source at the pinned commit, from the companions' measurements, or
from Quest's and Doom's repositories as they stood on 2026-11-14.

**Status, 2026-11-14.** Design only. Nothing is built. Upstream was read at
commit `1630182d3076beaaa170f11fc897408f7fc36ccb` (2026-11-14, one commit
after the 1.13 release tag).

**Contents**

1. [Goals and non-goals](#1-goals-and-non-goals) ·
2. [Decisions](#2-decisions) ·
3. [What upstream is](#3-what-upstream-is) ·
4. [Hardware mapping](#4-hardware-mapping) ·
5. [Source strategy: submodule, patch, shims](#5-source-strategy-submodule-patch-shims) ·
6. [Architecture](#6-architecture) ·
7. [Display](#7-display) ·
8. [Audio and pacing](#8-audio-and-pacing) ·
9. [Input](#9-input) ·
10. [Storage, files and media](#10-storage-files-and-media) ·
11. [The menu](#11-the-menu) ·
12. [Clocks, voltage, PSRAM and flash](#12-clocks-voltage-psram-and-flash) ·
13. [Memory budget](#13-memory-budget) ·
14. [Boot and exit](#14-boot-and-exit) ·
15. [Development workflow](#15-development-workflow) ·
16. [Testing](#16-testing) ·
17. [Milestones](#17-milestones) ·
18. [Unverified constants and assumptions](#18-unverified-constants-and-assumptions) ·
19. [Risks](#19-risks) ·
20. [Dropped and deferred](#20-dropped-and-deferred)

---

## 1. Goals and non-goals

**Goals**

- Run XRoar's Dragon 32, Dragon 64 and Tandy CoCo 1/2 machines on the
  PicoCalc at full speed, with the picture on its LCD, sound from its PWM
  audio, and typing and play from its own keyboard (D3).
- Cassette images, floppy disk images with the DragonDOS, Delta and RS-DOS
  controller cartridges, ROM cartridges and XRoar's own snapshots, all
  from the SD card, with XRoar's own file formats and behaviour.
- Carry XRoar as an unmodified submodule plus one patch, so following
  upstream is a patch refresh, not a merge (D1). The emulation is
  XRoar's; the port adds a front end and a platform.
- A menu in XRoar's own vocabulary, and XRoar's own `xroar.conf` on the
  card as the settings file (D5).
- Work with the PicoCalc shell: start from it, take arguments from it,
  leave to it (D4).
- One release image, built for the Pico Plus 2, which also runs on a
  Plus 2 W (D2).

**Non-goals**

- Boards without PSRAM (Pico 2, Pico 2 W) and RP2040 boards (D2). XRoar
  allocates everything on the heap and holds whole disk images in memory
  (§3.4).
- In the first release: the CoCo 3, the MC-10 and Alice, and the RAM
  expansions (D3, §20). They are deferred, not dropped.
- XRoar's composite video simulation (`-ccr partial`, `-ccr simulated`),
  its GDB target, tracing, the Becker port, printing to a pipe, and
  libsndfile, zlib and libpng features (§5.4, §20).
- Changes to the emulation. A change to an upstream file is made only
  where the PicoCalc needs it and a shim cannot do it (§5.3).
- Shipping ROM images. The user puts them on the card (§10.3).

---

## 2. Decisions

Numbered and dated (EL §14.7). **Settled** decisions are the owner's and
are not reopened without the owner. **Proposed** ones are this document's
recommendation and need the owner's word before the milestone that depends
on them.

| # | Date | Decision | State |
|---|---|---|---|
| D1 | 2026-11-14 | Port by patching upstream source, kept as a submodule, with the patch pipeline of `picocalc-quest` and `picocalc-doom` (§5). | Settled |
| D2 | 2026-11-14 | Target the Pimoroni Pico Plus 2 and Plus 2 W (RP2350B, 8 MB PSRAM). One image, built for the Plus 2; no Wi-Fi. | Settled |
| D3 | 2026-11-14 | First release: XRoar's `dragon` architecture (Dragon 32/64 and relatives, Tano, CoCo 1/2) with its cartridges. CoCo 3 and MC-10 are deferred to milestones after release (§20). | Settled |
| D4 | 2026-11-14 | A PicoCalc shell program, as Doom: exit through `picocalc_shell.h` (or power off without the loader), arguments from the shell, development over a Debug Probe on SWD and UART0, GP2 (TX) / GP3 (RX), 115200 8-N-1; GP4/GP5 left to the shell's clock (§4, §14, §15). | Settled |
| D5 | 2026-11-14 | The menu mirrors XRoar's desktop menus and option names (Machine, Cartridge, Tape control, Drive control, TV controls), and the settings file is XRoar's `xroar.conf` on the card (§10.4, §11). | Settled |
| D6 | 2026-11-14 | Whatever is learned about the PicoCalc goes into `hardware-notes.md` as it is learned, and what generalises about porting into `emulator-lessons.md`, each written to stand alone (Doom D6). | Proposed |
| D7 | 2026-11-14 | Run at **300 MHz**, core rail 1.20 V, from the start, as Doom ships; confirmed or revised at the M2 gate (§12.1). The estimate in §6.4 leaves no room at 150 MHz. | Proposed |
| D8 | 2026-11-14 | Start from `picocalc-doom`'s PicoCalc layer, scripts and CI, by copy, rename, re-verify (EL §14.6, §5.5). | Proposed |
| D9 | 2026-11-14 | Licence the port's own code **GPL-3.0-or-later**, XRoar's licence. Code copied from Doom's layer is GPL-2.0-or-later, by the same owner, and may be taken under the later version (§5.5). | Proposed |
| D10 | 2026-11-14 | No internal flash writes. Settings, media and snapshots live on the card (HW §7.1, EL §8.7). | Proposed |
| D11 | 2026-11-14 | The picture is 1:1: the 256×192 active area at (32, 64) inside a 320×240 frame at y = 40, which is XRoar's default "title" picture (640×480) at half its sampling in each direction. No scaling (§7.1). | Proposed |
| D12 | 2026-11-14 | NTSC artefact colours through XRoar's lookup-table cross-colour (`-ccr 5bit`) only; the filtered simulations are not built (§7.4). | Proposed |
| D13 | 2026-11-14 | Pin the submodule to the **1.13 release tag**, and move it only by a deliberate bump (§5.2). Upstream's server speaks only the dumb HTTP protocol; whether the submodule points at it or at a mirror in the owner's GitHub account is open (§19). | Proposed |
| D14 | 2026-11-14 | Keys reach XRoar through its own host-keyboard layer, so XRoar's Ctrl shortcuts, translated and raw keyboard modes and keyboard joystick all work as documented upstream; the port adds only what the PicoCalc's keyboard forces (§9). | Proposed |
| D15 | 2026-11-14 | GitHub `BlairLeduc/picocalc-xroar`; one branch and pull request per milestone, CI green on both builds (EL §14.7). | Proposed |

---

## 3. What upstream is

XRoar emulates the Dragon 32/64, the Tandy CoCo 1, 2 and 3, the MC-10 and
relatives. It is C, GPL-3.0-or-later, built with autotools. The tree is
about 95,000 lines in `src/` and `portalib/`, of which the GTK, SDL,
Windows, macOS and X11 front ends are roughly a third. It has no
submodules and one bundled library, `portalib` (strings, lists, delegates,
allocation wrappers).

### 3.1 How it is put together

| Piece | Files | What it is |
|---|---|---|
| Parts | `part.c`, `serialise.c` | Every emulated component (CPU, SAM, VDG, PIA, machine, cartridge) is a heap-allocated *part* in a tree, created by name from a part database and serialised field by field for snapshots |
| Events | `events.c`, `events.h` | Queues of events on a 32-bit tick counter at 14.31818 MHz. Every device times itself in ticks |
| Delegates | `portalib/delegate.h` | A function pointer with its context pointer. Parts are wired to each other, and to the front end, with these |
| Messenger | `messenger.c`, `ui.h` | UI state is a set of tagged messages (`ui_tag_machine`, `ui_tag_tape_playing`, `ui_tag_disk_insert`, …). Emulator and front end both send and receive them |
| Modules | `module.c`, `ui.h`, `vo.h`, `ao.h` | A front end is a **UI module** that supplies a video-output interface and a file requester, plus an **audio-output module**. `null/ui_null.c` and `null/ao_null.c` are the smallest, and `wasm/wasm.c` is a front end with no desktop under it |
| Top level | `xroar.c` (3,565 lines) | Options and `xroar.conf` (`xconfig.c`), machine and cartridge profiles, media loading, and `xroar_init()`, `xroar_init_finish()`, `xroar_run(ncycles)`, `xroar_shutdown()` |

The front end drives it: `main()` calls `xroar_init(argc, argv)`, which
picks the UI module by name and builds the machine, then calls the UI
module's `run` delegate in a loop, and `run` calls `xroar_run()` for a
stretch of guest time (`main_unix.c`, `wasm/wasm.c`).

### 3.2 The machine

The `dragon` architecture (`dragon/dragon.c` and its variants) is an
MC6809 or HD6309, an MC6883 SAM, an MC6847 VDG, two MC6821 PIAs, a
keyboard matrix, a cartridge port, a cassette and a printer port. Three
things about how XRoar runs it decide this design:

- **The CPU calls the machine on every bus cycle.** `mc6809.c` is
  accurate per cycle, dead cycles included. Each cycle goes through the
  `mem_cycle` delegate to `cpu_cycle()` in `dragon.c`, which checks
  watchpoints, asks the SAM to decode the address and say how many ticks
  the cycle takes, advances the clock and **runs the event queue**, sets
  the interrupt lines from the PIAs, and only then reads or writes RAM,
  ROM, a PIA or the cartridge.
- **The VDG renders every sample of every scanline.** `mc6847.c` builds
  each line as 912 palette-index bytes at 14.31818 MHz, two samples per
  pixel, of which 512 are the active area, and hands it to the front end's
  `render_line(burst, npixels, data)` delegate, 262 times a field. A mode
  change mid-line takes effect at the beam's position. With frame skip,
  lines of a skipped field are not rendered, but their sync events and
  video fetches still happen.
- **Sound is mixed in floating point** (`sound.c`), single precision, from
  the DAC, the single-bit output, the tape and the cartridge, and written
  to the audio module through a `write_buffer` delegate that **blocks**.
  That block is how XRoar keeps real time (§8.2).

### 3.3 What the port keeps, replaces and drops

| Upstream | The port |
|---|---|
| CPU, SAM, VDG, PIAs, machines, cartridges, keyboard matrix, tape, virtual disks and drives, FDC, snapshots, parts, events, messenger, options | **Kept, compiled unchanged** |
| `main_unix.c` | Replaced by `picocalc/main.c` (§14) |
| UI modules (GTK 2/3, SDL 2/3, Windows, macOS, WebAssembly, null) | Replaced by one UI module, `picocalc/ui_picocalc.c`, with its video interface and file requester (§7, §11) |
| Audio modules (ALSA, OSS, PulseAudio, CoreAudio, JACK, SDL, null) | Replaced by `picocalc/ao_picocalc.c` over the PWM ring (§8) |
| `vo_render.c`: RGB frame buffers, cross-colour, composite simulation | Compiled, but used only to compute colours; the port keeps the picture as palette indices (§7.3, §7.4) |
| Host keyboard back ends (X11, SDL, Windows) | Replaced by `picocalc/hkbd_picocalc.c` feeding `hk_scan_press()` (§9) |
| Host joysticks (evdev, joydev, SDL), mouse | Dropped; XRoar's keyboard joystick is kept (§9.4) |
| `stdio` on a host filesystem | newlib's `stdio` over FatFs through the port's system calls (§10.1) |
| GDB target, trace, Becker port, pthreads, zlib, libpng, libsndfile, TRE | Not configured (§5.4) |
| CoCo 3, MC-10 architectures | Not configured in the first release (D3) |

### 3.4 Memory, as upstream uses it

Read from source; the sizes are to be measured in M1 (§18).

- Everything is `malloc`: parts, configurations, strings (`sds`), lists
  (`slist`), events, sound buffers. `xmalloc()` aborts on failure.
- Guest RAM comes from `ram.c`, in banks; ROM images from `rombank.c`.
  Both are read on the per-cycle path.
- **A disk image is loaded whole** (`vdisk.c`): each side is one
  allocation of cylinders × track length, re-allocated as the disk grows,
  and written back to its file when ejected or at exit.
- Tapes are read through `stdio` as they play (`tape_cas.c`).
- A snapshot load frees the machine's part tree and builds another.

---

## 4. Hardware mapping

| XRoar needs | PicoCalc provides | Section |
|---|---|---|
| A picture: 262 lines × 912 samples of palette indices per field | 320×320 RGB565 LCD on `spi1`, written by DMA from core 1 | §7 |
| Audio: a blocking sink for sample buffers | Stereo PWM on GP26/GP27, DMA ring, 36,621 Hz at 300 MHz | §8 |
| Host key events as scancodes with modifiers | Translated key events from the southbridge on `i2c1`, polled | §9 |
| A filesystem through `stdio` | SD card on `spi0`, FatFs | §10 |
| A large heap | 8 MB PSRAM for large blocks, SRAM for the rest | §13 |
| A clock for real time | The PWM sample clock, by blocking on the audio queue | §8.2 |

Pins are the carrier's (HW §1.1). The port never touches GP23–25 or GP29
(the Plus 2 W's radio; the Plus 2's LED), or GP4/GP5 (the shell's DS3231
on `i2c0`). GP0/GP1 are left alone. The debug UART is UART0 on GP2/GP3
(D4).

---

## 5. Source strategy: submodule, patch, shims

### 5.1 Layout

Doom's, with the names changed:

```
picocalc-xroar/
  CMakeLists.txt                 the build root; ours
  cmake/upstream.cmake           configure-time copy and patch, with a stamp
  cmake/upstream-sources.cmake   upstream files we compile, as lists (§5.4)
  cmake/xroar-config.cmake       what upstream's configure would define (§5.4)
  modules/xroar/                 submodule, pinned; never edited
  patches/xroar.patch            every change to an upstream file, one patch
  xroar/                         gitignored: upstream copied and patched by the build
  working/                       gitignored: upstream files being edited, at their upstream paths
  reference/                     gitignored: the owner's ROMs and media for host tests; never committed
  picocalc/                      the port's own code (§5.3)
  test/host/                     host tests for picocalc/, and the host XRoar (§16)
  scripts/                       apply_patch.sh, create_patch.sh, check_sources.sh,
                                 build.sh, build_release.sh, flash.sh, capture.sh,
                                 keys.sh, counters.sh, profile.sh, gate.sh, soak.sh
  docs/                          hardware-notes.md, emulator-lessons.md, design.md,
                                 milestones.md (from M0)
  CLAUDE.md                      agent instructions (from M0, EL §14.1)
```

### 5.2 The patch pipeline

Quest's and Doom's, unchanged:

1. **Patch at configure time.** `cmake/upstream.cmake` copies
   `modules/xroar/` to `xroar/` and applies the patch when the submodule's
   commit, the patch or `apply_patch.sh` has changed since the last copy
   (a stamp file holds all three), and lists them in
   `CMAKE_CONFIGURE_DEPENDS`.
2. **`create_patch.sh` walks `working/`.** Every file there is diffed
   against the same path in the submodule, in sorted order, labelled
   `a/<path>` and `b/<path>` with no timestamps, so the same `working/`
   always gives the same patch. Adding a file to the patch is copying it
   into `working/`.
3. **No new files in the patch.** New code goes in `picocalc/`. The patch
   holds edits to upstream files only, and `create_patch.sh` refuses
   anything else.
4. **Each patched hunk carries a `picocalc-xroar:` comment** citing the
   section of this document that requires it.

To change an upstream file: `scripts/apply_patch.sh --working`, edit under
`working/`, `scripts/create_patch.sh`. To follow upstream: bump the
submodule, `apply_patch.sh --working`, fix rejects (left beside their file
as `.rej`), `create_patch.sh`, `check_sources.sh` (§5.4), build, run the
host tests. `test/host/test_patch_roundtrip.sh` comes with the scripts
(EL §14.6).

The patch may be empty; M0 starts with one that edits no files, which
`apply_patch.sh` already allows.

### 5.3 Shims before patches

XRoar was written to take front ends, and its seams are wider than
doomgeneric's. The port is, first, **a UI module and an audio module**.
Upstream's `ui_module_list` is a pointer variable (`ui.c`), and a UI
module names its own audio modules in its `ao_module_list` field
(`xroar.c`), so `main.c` points `ui_module_list` at a list holding the
port's module before it calls `xroar_init()`, and no upstream file changes
(confirmed by building, M1, §18).

| Upstream seam | Upstream implementations (not compiled) | PicoCalc implementation |
|---|---|---|
| `main()` | `main_unix.c` | `picocalc/main.c`: clocks, log, core 1, checks, then `xroar_init()` with an argument vector the port builds, `xroar_init_finish()`, and the UI's `run` (§14) |
| `struct ui_module`, `ui_interface.run` and the menu-update delegates | GTK, SDL, Windows, macOS, WebAssembly, null | `picocalc/ui_picocalc.c`: the run loop (§6.2), the menu (§11), a messenger client for the UI tags the menu shows |
| `struct vo_interface`: `render_line`, `draw`, palette and burst delegates | `vo_sdl2.c`, `vo_gtkgl.c`, … | `picocalc/vo_picocalc.c`: lines into a pool buffer as palette indices, publish at field end (§7) |
| `struct filereq_interface`: `load_filename`, `save_filename` | GTK, Windows, Cocoa, CLI | `picocalc/filereq_picocalc.c`: a file list drawn as a page (§11.3) |
| `struct ao_interface`, `sound_interface.write_buffer` | ALSA, CoreAudio, SDL, null, … | `picocalc/ao_picocalc.c`: convert, queue for the PWM ring, block for pacing (§8) |
| `hk_scan_press()`, `hk_scan_release()` and the host-keyboard tables | `hkbd_x11.c`, SDL, Windows | `picocalc/hkbd_picocalc.c`: PicoCalc key events to XRoar scancodes (§9) |
| `malloc`, `realloc`, `free` | the C library | `picocalc/alloc.c`: link-time wraps sending large blocks to PSRAM (§13.2) |
| `open`, `read`, `write`, `lseek`, `stat`, `unlink`, `rename` under `stdio` | the host | `picocalc/syscalls.c` over FatFs (§10.1) |
| `getenv("HOME")` and the configuration search path | the host | Arguments to `xroar_init()`: `-c`, `-rompath` and the rest (§10.2) |

The patch, **expected**, to be confirmed as each milestone reaches it. It
starts empty; every row is a hunk that may turn out not to be needed, and
none is written until a measurement or a failed build asks for it.

| File | Change | Milestone |
|---|---|---|
| `src/mc6809/mc6809.c`, `src/mc6883.c`, `src/dragon/dragon.c`, `src/events.c`, `src/mc6821.c`, `src/mc6847/mc6847.c` | A section attribute on the functions of the per-cycle path, so they run from SRAM, if `objcopy --rename-section` on whole objects (EL §6.5) is too coarse (§6.5) | M2 |
| `src/dragon/dragon.c` | Perhaps: the watchpoint check on every cycle compiled out when nothing can set one (§6.5) | M2 |
| `src/xroar.c` | Perhaps: one hunk if the module lists or the configuration path cannot be supplied from outside (§18) | M1 |
| `src/vdisk.c` | Perhaps: write-back made safe against power loss, through `.new` and a rename (§10.6) | M9 |

### 5.4 Which upstream files are built

Upstream's autotools build is not used: it is a host build with its own
feature tests. Upstream names its sources explicitly in `src/Makefile.am`
and `portalib/Makefile.am`, under conditionals (`MACHINE_ARCH_DRAGON`,
`PART_MC6809`, `CART_ARCH_DRAGON`, `TRACE`, `ZLIB`, …).
`cmake/upstream-sources.cmake` holds the **result** as explicit lists for
this configuration:

- on: the `dragon` machine architecture and its cartridges; the parts they
  need (MC6809, HD6309, MC6821, MC6847, MC6883, WD279x, AY-3-891x,
  SN76489 and the rest the cartridges pull in); snapshots; traps and
  breakpoints, which fast tape loading uses (`tape.c`);
- off: `coco3`, `mc10`, WebAssembly, every GUI, every audio back end,
  every joystick back end, GDB, trace, logging to taste (§15.3), pthreads,
  zlib, libpng, libsndfile, TRE, Becker, the CLI file requester,
  experimental parts.

`cmake/xroar-config.cmake` writes the `config.h` upstream's `configure`
would have, from the same choices, so every `HAVE_…` and `WANT_…` is in
one reviewed place.

`scripts/check_sources.sh` reads upstream's `Makefile.am` files at the
submodule's commit, evaluates their conditionals for this configuration,
and diffs the result against our lists: every upstream source must be
compiled or listed as excluded, nothing both, nothing unknown. It first
checks that the conditionals it knows are still the ones upstream uses, so
a new one is reported rather than missed. CI runs it.

Upstream is compiled with its own warning level, not the port's
`-Wall -Wextra -Werror`; `picocalc/` is compiled with it (EL §2.1).

### 5.5 Borrowed code

| Source | What | How |
|---|---|---|
| `picocalc-doom/picocalc/` (same owner) | `board.c` (clocks, the 300 MHz path, flash QMI, rail, identity), `clock.c`, `core1.c`, `framepool.c`, `lcd.c`, `southbridge.c`, `kbd.c`, `log.c`, `page.c`, `font8x10.c`, `psram_check.c`, `status.c`, `exit.c`, `counters.h`, `shell_args.c` | Copy, rename prefixes (`picocalc_doom_` → `picocalc_xroar_`), re-verify, bring the tests (EL §14.6) |
| `picocalc-doom/picocalc/` | `audio_shim.c` and `audio_stage.c` (the PWM ring and its conversion), `video_shim.c`, `input_shim.c`, `keymap.c`, `main.c` | **Adapt**: XRoar's `ao`, `vo` and `hkbd` seams in place of doomgeneric's |
| `picocalc-doom/scripts/`, `cmake/`, `test/host/`, CI | The patch pipeline and its round-trip test, the probe tools, the gate and soak scripts, the fake SDK headers for host tests | Copy and rename; `check_sources.sh` rewritten for `Makefile.am` (§5.4) |
| `picocalc-shell/include/picocalc_shell.h` | Return to the shell, its clock, its arguments | Copy the shell's current header |
| `picocalc-doom/picocalc/` | `opl_core1.c`, `doom_files.c`, `dg_picocalc.c`, `frame_hash.c`, `render_sram.c`, `settings.c`, `config_stdio.h` | **Not used**: `xroar.conf` is read by upstream's own parser (D5), and `stdio` is real (§10.1) |
| The sibling emulators (`pico-atom`, `pico-jupiter-ace`, `pico-oric`) | Nothing copied. Their lessons are in `emulator-lessons.md` | — |

**Licence (D9).** XRoar is GPL-3.0-or-later (its bundled `portalib` parts
carry their own compatible notices, kept in place). Doom's PicoCalc layer
is GPL-2.0-or-later and the same owner's; it is taken here under
GPL-3.0-or-later. A copied file keeps its attribution (the 8×10 font's,
the ClockworkPi panel initialisation's, FatFs's). Every file in
`picocalc/` carries an `SPDX-License-Identifier`.

Anything moving onto core 1 is searched for `sleep_` (EL §2.3, HW §9.7).

---

## 6. Architecture

### 6.1 Which core owns what

XRoar is one thread. It stays one thread, on core 0, with the card,
because it reads files synchronously through `stdio` from wherever it is
(Doom §6.1).

| Core 0 | Core 1 |
|---|---|
| All of XRoar: CPU, devices, events, sound mixing | LCD presenting (§7.5) |
| The UI module's run loop and the menu (§6.2, §11) | Southbridge I²C: keyboard FIFO, battery, backlight (§9.1) |
| `render_line`: one scanline into the pool buffer being filled (§7.2) | Draining the log ring to UART0 (§15.3) |
| The audio DMA IRQ at priority `0x40`: copy and re-arm (§8.1) | |
| SD card and FatFs, as XRoar and the menu ask | |
| Key events, popped and passed to `hk_scan_press()` (§9.2) | |

```
            core 0                                          core 1
 ┌──────────────────────────────┐                 ┌────────────────────────────┐
 │ ui run loop:                 │                 │ southbridge poll           │
 │   pop keys → hk_scan_*  ◄────┼──── SPSC ───────┤   [state, code]            │
 │   xroar_run(one field)       │                 │                            │
 │     6809 cycle → SAM →       │                 │                            │
 │     events → VDG line        │                 │                            │
 │     render_line: 256 indices │  pool: ready    │ take ready buffer          │
 │       into filling buffer    ├───────────────► │   palette → RGB565 rows    │
 │     field end: publish       │                 │   changed rows by DMA      │
 │     sound_update →           │                 │ release buffer             │
 │       write_buffer: convert, │                 │                            │
 │       queue, block (pacing)  │   log ring      │ pump log → UART0           │
 │ DMA IRQ (0x40): queue → PWM  ├───────────────► │                            │
 │ menu: draws a page buffer,   │                 │                            │
 │   SD/FatFs (spi0)            │                 │                            │
 └──────────────────────────────┘                 └────────────────────────────┘
```

### 6.2 The run loop, the menu and "parking"

The UI module's `run` is the port's main loop. Each turn it pops the key
events core 1 queued, hands them to XRoar or to the menu, and, while the
guest runs, calls `xroar_run()` for one field's worth of cycles.

The emulator family's park-and-hand-over (EL §2.5) is not needed here,
because nothing but core 0 ever touches the machine. Its purpose is kept:

- **While the menu or a file list is open, the guest is not run**, so
  guest time stands still and card latency is invisible to it. The menu
  runs on core 0, between calls of `xroar_run()`.
- **The audio queue is fed silence at the rate it drains** while the guest
  is stopped, so the sink never underruns and pacing needs no catch-up
  (§8.2).
- **A card operation XRoar makes while the guest runs** (a tape block, a
  disk write-back, a snapshot) blocks core 0 where it stands. Guest time
  does not advance during it, since XRoar's clock is its tick counter
  (EL §4.3), but the audio queue can run dry: underruns are counted per
  cause (§8.3), and expected only at such moments.
- **Measuring windows restart** when the guest resumes (EL §2.5).

`xroar_run()` returns after about the cycles asked; the remainder is
upstream's to carry, in its own cycle counter (EL §2.2 is upstream's
concern here, and M1 checks that long-run time does not drift, §16).

### 6.3 Invariants

- **The LCD is touched only from core 1, in thread context.** No interrupt
  masking around a blit (HW §5.7).
- **Core 1 never reads XRoar's memory.** It reads a published pool buffer
  and the palette that came with it, and the key and log queues.
- **XRoar is called only from core 0.** Everything core 1 learns
  (keys, card detect, battery) crosses in a queue or a word.
- **No `sleep_*` on core 1**; it waits with `busy_wait_us_32` (HW §9.7).
- **Core 0 never blocks on the UART.** `stdio`'s console and XRoar's
  logging go into the log ring (§15.3).
- **Everything on the audio refill path is in SRAM** (HW §5.4).
- **No flash writes** (D10).
- **No reset with a southbridge transaction in flight** (HW §6.1): every
  reboot or exit goes through one function that parks core 1 between
  transactions first.
- **Shift with an arrow, Space or Backspace is never needed** by a binding
  the port defines (HW §6.3, §9.3).
- **`modules/xroar/` and `xroar/` are never edited** (§5.2).

### 6.4 What the per-cycle design costs: an estimate, and the gate

XRoar spends its accuracy where the sibling emulators did not: a call out
of the CPU on every bus cycle, an event queue checked on every cycle, and
a VDG that produces every sample. The costs below are **estimates** made
by reading the code; EL §1 records such estimates being wrong by 3× in
both directions, so they serve only to place the gate.

| Work | Rate | Estimate |
|---|---|---|
| Bus cycles: delegate call, watchpoint check, SAM decode and timing, clock and event queue, IRQ lines, memory or device | 894,886 a second (14.31818 MHz ÷ 16; more in the SAM's fast modes) | 150–300 host cycles each: **134–268 M host cycles a second** |
| VDG rendering, per rendered field: 192 active lines × about 744 samples, plus fetches and the line callback | 60 fields a second without frame skip | 8–15 host cycles a sample: **69–129 M a second at 60 fields, half at 30** |
| `render_line` in the port: 256 indices a line, cross-colour when on | 192 lines a rendered field | 5–10 M a second at 60 fields |
| Sound: float mixing per change and per field | 36,621 samples a second | 5–15 M a second |

Against 300 M host cycles a second, the total is **about 70–140 % of
core 0 at 60 rendered fields a second, and 59–118 % at 30**. At 150 MHz
it does not fit at all, which is why D7 starts at 300 MHz. The honest
reading is that full speed is **not assured**, and nothing downstream
should be built until it is measured.

**The gate (M2).** XRoar's machine alone on the board, as compiled for the
port, at 300 MHz: a Dragon 32 with a test ROM image the project builds
itself (no copyrighted ROM is needed to measure), the null video and audio
paths, running a compute loop, a screen-writing loop and a sound loop, for
frame skip 0, 1 and 2. It reports host cycles per bus cycle and the share
of core 0 a real-time machine would take, and the same image run on the
host must agree on cycle and instruction counts (EL §12).

- **Pass:** at most **75 % of core 0** for the heaviest workload, at the
  frame skip the port will ship, leaving room for `render_line`, sound and
  the audio interrupt. (Doom ran 79 % busy at this clock for 30 minutes,
  HW §3.)
- **Fail:** apply the levers of §6.5 in order, measuring each. If the
  machine still does not fit, stop and take the result to the owner
  before any other port work: the choices then are a slower guest, a
  larger patch to the per-cycle path, or another approach.

### 6.5 Levers, in order

Each is measured against a control build in one sitting (EL §12), in its
own build directory (HW §9.8).

1. **Remove interference**: nothing on core 1 that interrupts core 0
   (EL §2.3).
2. **Frame skip**: XRoar's own option. Rendering every second field
   halves the VDG's cost and changes no timing. The panel cannot show 60
   full fields a second at this size anyway (§7.5).
3. **Configure out what the port cannot use**: GDB, trace and logging in
   the cycle path are build options, not patches.
4. **`-O3` on the cycle path's objects**, and inlining across them where
   one object allows it.
5. **SRAM placement, in measured tiers** (HW §9.2, §9.8): the cycle path
   (the CPU's dispatch, `cpu_cycle()`, the SAM's `mem_cycle`, event
   dispatch, the PIA's read and write, the VDG's scanline renderer), by
   `objcopy --rename-section` on whole objects after compiling (EL §6.5)
   before any section attribute in the patch. A post-link check walks
   the path for veneers into flash.
6. **Guest ROM in SRAM**, which the SRAM heap gives for free (§13.2).
7. **Patch hunks on the hot path**, each one a cost at every upstream
   bump: the per-cycle watchpoint check; a direct call in place of a
   delegate. Last, and only with a measurement that names them.

---

## 7. Display

### 7.1 Geometry (D11)

XRoar's default picture, "title", is 640×480: 640 of the line's 912
samples and 240 of its 262 lines, each line doubled. The VDG's samples are
half pixels, so **every second sample and every line, undoubled, is
320×240**, which is the panel's width exactly. The active area, 256×192,
sits at (32, 24) inside it, with 32 pixels of border left and right and 24
above and below.

On the 320×320 panel the frame goes at y = 40, as Doom's does, so the
active area is at **(32, 64)**: the placement HW §4.10 records as tested.
The 40-row bands above and below the frame carry a status line (tape
position, drive activity, the machine's name) and a perf line, each drawn
only when its text changes and each hidden by a setting (EL §5.4).

No scaling: 256×192 at 1.25× was tried twice before and dropped twice
(EL §5.4).

### 7.2 What a frame is

A pool buffer holds what core 1 needs to draw one field, and nothing of
XRoar's:

- **the active area as palette indices**: 256 × 192 bytes, 49,152 bytes;
- **one border index per line**, 240 bytes, taken from the line's left
  border;
- **the palette**: 256 RGB565 entries, 512 bytes, and a generation number
  that changes when any entry does (§7.3).

The border inside a line is a single colour unless a program changes mode
mid-line in the border, which the port does not show (§20). Core 1 draws
the border as rectangles, and only when a line's index or the palette
changes.

The pool is Doom's and the emulators' (EL §2.4): three buffers, four
states, publishing drops an older `ready` buffer, the state machine has no
lock of its own and the port drives it under one spinlock; dropped frames
are counted. It lives in **SRAM** (3 × 49,904 bytes, about 150 KB): core 0
writes it a line at a time, and a renderer's target in PSRAM cost Doom a
third of its frame rate (HW §9.3).

`render_line` is called for all 262 lines. The port keeps a line counter,
reset at the vertical sync XRoar reports, and for the 240 lines of the
picture copies every second sample of the active area and one border
sample. For a line XRoar passes with no data (frame skip), it does
nothing. At the `draw` delegate, which upstream calls once a shown field,
it publishes the buffer and claims another.

### 7.3 Colours come from upstream

XRoar tells the video interface its palette as luma and chroma
(`palette_set_ybr`) or RGB, with brightness, contrast, saturation and hue
applied by `vo_render.c`. The port **compiles `vo_render.c` and uses one
instance of it, in RGB565, as a colour calculator**: its buffer is a
single line, and when the palette changes the port renders one line whose
samples are the palette indices themselves and reads the RGB565 values
back. So the port's colours are upstream's by construction, and TV
controls (brightness, contrast, saturation, hue) work through upstream's
own messages with no port code.

The port wraps the palette and burst delegates only to learn that
something changed. The probe runs on core 0 at a field boundary, costs one
rendered line, and bumps the palette's generation.

This is the shim. If `vo_render.c` proves too heavy to carry for it
(its filter tables, its size on the SRAM heap, §18), the fallback is a
one-hunk patch that exposes its palette tables to the port.

### 7.4 Artefact colours (D12)

On NTSC machines, alternating black and white pixels in the
high-resolution modes show as colours, and CoCo software depends on it.
XRoar's `-ccr 5bit` does this by table: where the samples are black or
white runs, a 5-bit history of pixels selects one of 2 × 32 colours by
phase (`vo_render_tmpl.c`). At the port's sampling, one VDG pixel is one
output pixel, and that renderer already emits its colours in pairs of
samples, so it maps onto 256 pixels exactly.

The port's `render_line` reproduces that selection and stores **an index
into a reserved part of the palette** (64 entries for the artefact
colours, the phase folded in), whose RGB565 values are read from
upstream's renderer by the same probe as §7.3, with black-and-white
patterns as the line. The selection logic is the one piece of upstream's
video the port re-implements; a host test holds it to upstream's
`render_cmp_5bit`, pixel for pixel, over generated lines and over real
screens (§16).

PAL machines (the Dragons) have no artefact colours and take the plain
path. `-ccr simple` (the 2-bit table) is the same mechanism with a smaller
table and is offered too. `partial` and `simulated` filter every sample in
floating point and are not built.

### 7.5 The presenter

Core 1 takes the `ready` buffer and sends what changed:

1. If the palette's generation differs from the last one sent, every row
   is dirty (EL §5.3: a mode or colour change repaints pixels that did
   not change as bytes).
2. Otherwise a row is dirty when it differs from the same row of the
   **shadow**, a copy of the last active area sent (49,152 bytes, SRAM).
   Rows are grouped into bands of 8 with a column span, as the emulators
   do (EL §5.3).
3. Each dirty band is one LCD window: rows expanded through the palette
   into two RGB565 line buffers ping-ponged under polled DMA (HW §4.6).
4. Border rectangles are sent for lines whose border index changed.
5. The shadow and the presented generation are brought up to date.

A full 256×192 repaint is 11.5 ms on the wire (HW §4.7), inside a field;
a full 320×240 frame with borders is about 18 ms, more than a field, which
is why the border is sent only when it changes. With three buffers the
guest never waits for the panel, and a field the panel cannot show is
dropped and counted (EL §2.4).

Tearing is accepted (HW §1.2, EL §5.4).

### 7.6 Pages

The menu, the file list, the missing-ROM page and the error page are text
pages in the port's own 8×10 font (Doom's `page.c`), drawn by core 0 into
a page buffer and published through the same pool with a flag that says
"page"; core 1 draws a page over the whole 320×320 panel. Closing a page
invalidates the shadow, and the next field is sent whole (EL §10).

The guest's own font is not used for the pages: it is in XRoar's VDG
tables, 32 columns wide, and the menu wants 40 (§11).

---

## 8. Audio and pacing

### 8.1 Output

Doom's and Quest's, with the numbers of HW §5.9: at 300 MHz, divider 1,
TOP 4095, oversample 2, a 73.2 kHz carrier at 12 bits and
**300,000,000 / 8,192 = 36,621.09 samples a second**; two DMA channels
chained through a ring of two 512-frame halves, aligned to its size with
the hardware read wrap; both the read address and the count reset on
re-arm; `DMA_IRQ_0` at priority `0x40`; everything on the refill path in
SRAM (HW §5.3, §5.4). The interrupt only copies finished compare words
from a queue and re-arms. A gain stage sits before the conversion, and
saturates (HW §5.10).

Where `.bss` lands relative to OpenOCD's work area is checked whenever the
ring moves (HW §2.7), and the 8 KB alignment's hole before `.bss` is used
for SRAM code (HW §2.3).

### 8.2 The audio module, and pacing

`picocalc/ao_picocalc.c` asks upstream for a sound interface at 36,621 Hz,
signed 16-bit, host-endian, two channels (the Orchestra 90 cartridge is
stereo; everything else is the same signal twice), with a buffer of one
ring half. Upstream fills the buffer as guest time passes and calls
`write_buffer`. The port's `write_buffer`:

1. converts the frames to PWM compare words and pushes them to an SPSC
   queue (producer and interrupt are both on core 0, so no lock);
2. **blocks, with `__wfi()`, until the queue has room**, which is the
   pacing of EL §6.3 and exactly what upstream's own audio modules do: the
   PWM wrap is the clock, and guest and audio share it by construction;
3. returns the buffer for upstream to refill.

Upstream's rate is an integer, 36,621, against a true 36,621.09: the guest
runs 2.6 parts in a million slow, which nothing can observe. The control
quantity for every measurement is samples consumed a second against
`time_us_64()` (EL §6.3).

**Rate limiting off** (XRoar's own setting, its fast-forward): the port
drops the samples and tops the queue up with silence without blocking
(EL §9.3). **Muted** still produces and consumes (EL §6.2). **Guest
stopped** (menu, pause, a file list): the run loop feeds silence at the
drain rate (§6.2).

Sound is mixed in single-precision floating point, which is hardware on
the M33. A `double` in the mixing path would be software (HW §2.2): the
M2 image is checked for `__aeabi_d…` calls reachable from `sound_update()`
and the per-cycle path, and any found is a finding to take upstream, or a
hunk (§18).

### 8.3 Counters

Two, never one (HW §5.8): **underrun samples** (the producer starved) and
**late refills** (the interrupt starved), and beside the first the cause
when the port knows it: card I/O in progress, menu opening, or none. The
late path is forced once with a scratch build (EL §6.4).

---

## 9. Input

### 9.1 From the southbridge to core 0

Core 1 polls the southbridge FIFO in thread context between presents and
between a present's bands (EL §7.1), drains it each time, and pushes
`[state, code]` pairs into an SPSC queue. The rules are the notes':
held-key state from press and release events; repeats arrive as presses;
letters and shifted punctuation canonicalised for held-state identity; a
release for a key that is not down is ignored; Insert is I under Alt and
Enter otherwise; Caps Lock is accepted (HW §6.2, EL §7.1). The poll also
feeds the southbridge's 2.5 s bus watchdog, so no card wait on core 0 can
starve it.

### 9.2 Into XRoar (D14)

XRoar has a host-keyboard layer, `hkbd.c`, between a platform's scancodes
and the guest's matrix. Feeding it gives the port, unchanged, everything
XRoar documents:

- **translated mode** (the default): the character typed is the character
  the guest sees, whatever the guest's own shift state must be for it;
- **raw mode**: keys by position, for games;
- **the Dragon and CoCo keymaps**, and XRoar's switch between them;
- **XRoar's Ctrl commands** (`hk_scan_press()` routes a key pressed with
  Control to `emulator_command()`): reset, load, run, insert a disk, save
  a snapshot, cycle the joysticks, toggle translation, and the rest of
  upstream's manual. The southbridge passes Ctrl chords through unchanged
  (HW §6.3), so these work as they do on a desktop;
- **the keyboard joystick** (§9.4).

`picocalc/hkbd_picocalc.c` maps each PicoCalc code to an XRoar scancode
(`hk_scan_…`) and calls `hk_scan_press()` and `hk_scan_release()`. The
table is **data**, one row per PicoCalc code, checked by a host test
(EL §7.2). The mismatch to handle: the southbridge resolves Shift itself
and sends `!`, not Shift and `1`. XRoar's layer wants the key and the
modifier, and looks the symbol up itself. So each shifted code maps to
its **unshifted key on a US layout**, the Shift the southbridge already
reported supplies the level, and the port installs a US symbol table so
XRoar arrives back at `!`. Characters the PicoCalc types with Shift that a
US keyboard does not, or the reverse, get a flag that presses or releases
XRoar's Shift around the key (EL §7.2). How `hkbd.c` takes a layout with
no desktop under it is read and confirmed in M1 (§18); if it cannot, the
fallback is upstream's lower seam, `keyboard_unicode_press()` and
`keyboard_press()`, with the Ctrl commands called directly.

**Binding at press time**: the scancode a press mapped to is kept with the
held key, so its release undoes exactly that (EL §7.1).

**The guest's pace.** XRoar's matrix is read by the guest's ROM when the
ROM scans. A key tapped on the PicoCalc is seen by the port for at least
one poll (20–33 ms), and the ROM may need longer or shorter. The minimum
hold and gap for each ROM (Dragon, CoCo Color BASIC) are **settled by
executing the ROM on the host**, with a control that loses keys, and by
typing a long program and counting stored lines (EL §7.1). The port's
replay then holds each key that long before passing its release. XRoar's
own `auto_kbd.c` (its type-a-string feature) is the reference for what
upstream considers enough.

### 9.3 What the PicoCalc's keyboard forces

- **Shift+Left, Shift+Right, Shift+Space and Shift+Backspace send
  nothing**, release included (HW §6.3). On a Dragon, Shift+arrows and
  the like are typed rarely, but **BREAK** matters: Esc is BREAK
  (XRoar's default, `hkbd.c`), and no binding the port adds uses Shift
  with an arrow. When Shift goes down with Left or Right held, the port releases
  the arrow itself.
- **Alt+`,` `.` Space and B belong to the southbridge** (backlights,
  battery). The port binds nothing there.
- **The port's own keys are on Alt and the function keys**, which no
  Dragon or CoCo has: the menu, pause, and direct pages (§11.2). XRoar's
  commands stay on Ctrl. A key pressed with Alt down never reaches the
  guest.
- **Clear, the guest's own arrows, and the CoCo's keys with no PicoCalc
  equivalent** are placed by M5 from XRoar's own host-key assignments
  (Home is CLEAR upstream; Home is Shift+Tab here), each with a plain or
  Alt-layer key (EL §7.2), and listed in the README.

### 9.4 Joysticks

The PicoCalc has no joystick. XRoar's **keyboard joystick** maps keys to
an axis or a button of either port, with configurations in `xroar.conf`
and a Ctrl command to cycle them. The port keeps it and ships defaults
that suit the PicoCalc: the arrows as the right joystick's axes and a
key the southbridge delivers alongside the arrows as fire (`]`, EL §7.3;
Space and Ctrl also qualify, HW §6.3). Game-specific layouts are
`xroar.conf` text, the user's (EL §10: the firmware names no title).

---

## 10. Storage, files and media

### 10.1 Driver and `stdio`

The SD driver and FatFs are Doom's: `spi0`, 400 kHz then 25 MHz requested,
card detect on GP22 debounced, exFAT enabled, every wait bounded, a failed
transfer marking the card uninitialised (HW §7.1). The card is on core 0.

XRoar uses `stdio` and POSIX file calls throughout (`fopen`, `fread`,
`fseek`, `fgetc`, `fprintf`, `stat`, …). The port gives it the real
thing: **newlib's `stdio`, with the system calls underneath**
(`_open`, `_read`, `_write`, `_lseek`, `_close`, `_fstat`, `_stat`,
`_unlink`, `_rename`, and directory reading for the file list) implemented
in `picocalc/syscalls.c` over FatFs. A `FILE *` is then a real `FILE`, so
`fprintf`, `fscanf` and `feof` work without the link-time wraps and their
traps (EL §8.9). The console's descriptors go to the log ring.

FatFs's own allocator is wrapped to return NULL for a request that cannot
fit, so its fallbacks run (HW §7.1, EL §8.9).

### 10.2 Card layout

```
/xroar/
  xroar.conf            XRoar's configuration (D5, §10.4)
  roms/                 firmware ROM images (§10.3)
  tapes/  disks/  carts/  snaps/     where the file lists open; any folder works
```

`main.c` builds `xroar_init()`'s argument vector: `-c /xroar/xroar.conf`,
`-rompath /xroar/roms`, the port's UI and audio modules, the audio rate
and buffer, then **the shell's arguments**, which are XRoar's own
command-line options and come last so they win
(`picocalc-xroar -machine coco2bus -run game.cas`). Build-time overrides
for scripted runs follow the same route (EL §13.1). The folder is
XRoar's name for itself, so a card laid out for XRoar elsewhere needs
only its paths in `xroar.conf`.

### 10.3 ROMs

The Dragon and Tandy ROMs are not redistributable, and the port ships
none. The user copies them to `/xroar/roms/` under the file names
XRoar's manual gives (`d32.rom`, `d64_1.rom`, `d64_2.rom`, `bas13.rom`,
`extbas11.rom`, `ddos10.rom`, `disk11.rom`, …); the README repeats
upstream's table with sizes.

Upstream identifies ROMs by **CRC-32**, with built-in lists of known
images, and picks the default machine as the first whose ROMs it finds.
The port leaves all of that to upstream. It adds:

- **a missing-ROM page**: when no machine has its ROMs, a page naming the
  files XRoar looked for and where, never a blank screen (EL §8.1);
- **an About page** listing each ROM slot of the running machine: file,
  size, CRC-32 and upstream's name for the image, beside the firmware and
  XRoar versions, the board, the clocks and the die temperature (EL §10).

The gate and the early host tests use a test ROM the project assembles
itself, so CI never needs a copyrighted file; tests that need the real
ROMs find them in `reference/` and skip without them (EL §11.2).

### 10.4 Settings: `xroar.conf` (D5)

The settings file is XRoar's own, read by XRoar's own parser at start-up.
Anything XRoar documents for `xroar.conf` works: machine and cartridge
profiles, ROM paths, keyboard and joystick configuration, tape and disk
options, TV options.

Port-specific settings (the status and perf lines, the backlight, the
gain, the key hold) need a home. They are added **as options in the same
file under a `picocalc-` prefix**, registered with upstream's option
parser from the UI module if it takes options from a module, as upstream's
own UI modules do; otherwise they live in a second file, `picocalc.ini`,
read by Doom's parser (§18).

**Saving.** XRoar can write its whole configuration (`-config-print-all`
produces it; `-config-auto-save` writes it on exit). The menu's "Save
configuration" uses that writer, through `xroar.conf.new` and a rename
(HW §7.1), as a deliberate action; auto-save on exit is off by default.
This **regenerates the file**, losing the user's comments and layout,
where EL §8.7 asks for an edit in place. It is accepted here as the
price of D5: the file's format belongs to upstream. The menu says so
before the first save, and keeps the previous file as `xroar.conf.bak`.

### 10.5 Tape

All upstream's: CAS images, ASCII BASIC files, fast loading through ROM
intercepts, automatic leader padding, tape rewriting, a tape for output.
The port adds the menu (§11) and the status line (motor, position). With
rate limiting off a tape loads as fast as core 0 allows (§8.2).

WAV tapes come through upstream's built-in WAV-only reader
(`sndfile_compat.c`), which stands in for libsndfile; other audio formats
would need the library and are deferred (§20). M8 checks a WAV tape loads
and what it costs.

### 10.6 Disks

All upstream's: VDK, JVC, DSK/OS-9 and DMK images, four drives, the
DragonDOS, Delta and RS-DOS cartridges, write enable and write-back per
drive. A disk is held whole in memory, which the PSRAM provides (§13.2):
reads and writes during play cost no card time, and the cost of PSRAM
access falls on disk transfers only.

What the PicoCalc changes:

- **Write-back happens at eject and at exit**, so a disk written and not
  ejected is lost at power-off. The port ejects every drive on the way
  out (§14.3), marks a drive with unwritten changes on the status line,
  and offers "write back now" in the Drive control menu.
- **A write-back takes time**: a 180 KB image is about half a second at
  the card's write rate, a 720 KB one about two (HW §7.1), with the guest
  stopped and silence fed.
- **Power loss during a write-back** can leave a damaged image. Whether
  upstream's writer goes through a temporary file is read in M9; if not,
  the `vdisk.c` hunk of §5.3 makes it `.new` and rename.

### 10.7 Snapshots and cartridges

Snapshots are XRoar's own format, read and written by upstream; the port
supplies file names through the requester and numbered quick slots. A
snapshot load rebuilds the machine's parts, which exercises the heap
(§13.2, §19). Loading one stops the guest, as any card work does.

ROM cartridges load from files as upstream does, and upstream's cartridge
profiles (disk controllers, Orchestra 90, Games Master Cartridge with its
SN76489, the multi-pak and IDE cartridges) are available as upstream
builds them for the `dragon` architecture; the IDE and SD-card cartridges
want block-device images and are untested until asked for (§20).

---

## 11. The menu

### 11.1 Shape (D5)

The menu mirrors XRoar's desktop menus, by name and in upstream's order,
so XRoar's manual documents it:

| Menu | Items, as upstream |
|---|---|
| **File** | Run…, Load…, Insert disk…, Save snapshot…, Load snapshot…, quick slots, Quit |
| **View** | TV controls (input, cross-colour, brightness, contrast, saturation, hue), Inverse text, Frame skip, the port's status and perf lines |
| **Hardware** | Machine (the profiles upstream lists), Cartridge, Keyboard type, Joysticks (right, left, swap), Soft reset, Hard reset |
| **Tool** | Tape control (input file, position, play, fast, pad, rewrite, output file), Drive control (four drives: insert, new, eject, write enable, write back), Keyboard translation, Rate limit |
| **Help** | About, Save configuration |

It is drawn as text pages (§7.6), 40 columns, one level at a time, with
the guest stopped. Every item **sends upstream's UI message or calls
upstream's function**, and every value shown comes from upstream's
messages: the port's UI module is a messenger client like any desktop
front end, so the menu cannot disagree with the emulator. The machine and
cartridge lists are filled by the `update_machine_menu` and
`update_cartridge_menu` delegates upstream calls.

Changing the machine is upstream's own action and takes effect as
upstream makes it (a new machine, powered on). The menu warns that the
program in memory is lost.

### 11.2 Keys

- **The menu key**, an Alt chord, opens it; Esc closes a level.
- **Function keys open pages directly** from the running guest: F1 the
  menu, F2 Drive control, F3 Tape control, F4 Machine, F5 About; closing
  a page so opened returns to the guest. Inside the menu they do nothing
  (EL §10).
- **XRoar's Ctrl commands** work as upstream documents, and those that
  ask for a file open the file list (§11.3).
- **Pause** on an Alt chord: guest stopped, backlight dimmed after its
  level is read, restored on any key, which is not passed on (EL §10).

The exact chords are chosen in M7 against HW §6.3's list of what the
southbridge delivers, and written into the README.

### 11.3 The file requester

Upstream asks its UI for a file name through `load_filename` and
`save_filename`. The port's requester is a page: the folder's entries,
filtered by the extensions the request allows, `._` files hidden
(HW §7.1), folders first, the last folder used for each kind of file
remembered for the session. Saving offers a numbered new name
(`tape001.cas`) so a user without a computer has somewhere to save
(EL §10), and a short name typed on the keyboard.

Upstream's delegate returns a string it will `free()`. The requester runs
its own key loop on core 0, feeding silence, until a choice or Esc.

### 11.4 Status and errors

- A **status row** in the menu names the first problem: a missing ROM, a
  disk that failed to write back, a protected image, a refused snapshot,
  a bad line in `xroar.conf` as upstream reports it (EL §10).
- Upstream's warnings and errors go to the log; the ones a user must see
  are surfaced by the port from the UI messages and return values it
  already receives, not by parsing log text.
- A fatal error (`xmalloc()` failing, an assertion) shows an error page
  with the message and returns to the shell on a key (§14.3).

---

## 12. Clocks, voltage, PSRAM and flash

### 12.1 The clock (D7)

Board bring-up is Doom's `board.c`, by copy: `main()` starts at 150 MHz,
raises the rail to 1.20 V, doubles the flash's QMI divider and RX delay
from SRAM, raises the PLL to 300 MHz, puts `clk_peri` on `clk_sys`, and
only then starts stdio, I²C, the LCD and audio, so each driver derives its
rate from the clock it finds (HW §3, EL §9.4). The rail is set
explicitly on the 150 MHz path too. A flash command puts the bootrom's
timing back, so ids are read before the clock goes up (HW §3).

300 MHz is outside the RP2350's datasheet. Doom soaked it for 30 minutes
at 79 % of core 0 on this board (HW §3); this port soaks it again under
its own load (M11). The M2 gate decides whether 300 MHz is needed or
150 MHz would do, and D7 is confirmed or revised there.

### 12.2 Board target

One image, built for `pimoroni_pico_plus2_rp2350`, which runs on the
Plus 2 W without touching its radio (D2). The banner reports the physical
board (package, flash size, PSRAM) separately from the build target
(HW §2.1), and the die temperature is read from the channel the package
has (HW §8.1).

### 12.3 PSRAM

Initialised by the SDK before `main()`, its QMI timing rewritten after
the clock moves (HW §3), then **verified by a real round trip** before
any allocator is given it (HW §2.4, Doom's `psram_check.c`). Without
working PSRAM the port shows a page saying so and returns to the shell.

### 12.4 Flash

Read-only (D10). The image is the firmware alone: no ROMs, no media.
Under UF2 Loader the App partition is the flash less 8 KB as the loader
was built (HW §2.7); the image is far below it (§13.3).

---

## 13. Memory budget

### 13.1 SRAM (520 KB)

**Estimates**, to be replaced by the linker map and high-water marks from
M2 on (EL §14.4). `arm-none-eabi-size` is printed on every build, and
fixed capacities live in one header (EL §2.1).

| Region | Estimate |
|---|---:|
| Frame pool: 3 × (256×192 + 240 + palette) (§7.2) | 150 KB |
| Presenter's shadow (§7.5) | 49 KB |
| Page buffer, 40×32 cells, and the status and perf lines (§7.6) | 4 KB |
| The per-cycle path placed in SRAM (§6.5), if the gate asks | 30–60 KB |
| Audio ring (8 KB, aligned) and compare-word queue (§8.1) | 12 KB |
| SRAM heap: XRoar's parts, strings, lists, sound buffers (§13.2) | 48–96 KB |
| SRAM heap: guest RAM (64 KB) and ROMs (16–40 KB) | 80–104 KB |
| SDK, FatFs, drivers, the port's `.data` and `.bss`, log ring | 30 KB |
| Stacks: core 0 (enlarged, below) and core 1 (2 KB) | 10 KB |
| **Total** | **413–515 KB** |

The upper end does not leave a margin. The first relief, if the map
confirms it, is the shadow: a 32-bit hash per row (768 bytes) in place of
the 49 KB copy, at the cost of a vanishing chance of a stale row. The
second is which tier of code goes to SRAM.

**Core 0's stack.** XRoar's call depth under `xroar_init()`, a snapshot
load and the file requester is unknown. Both stacks are painted from M1
and their high-water marks reported (HW §2.3); core 0's is sized from the
measurement, with the linker script changed if the default 4 KB is not
enough (Doom §12.1).

### 13.2 The heap: SRAM and PSRAM

XRoar calls `malloc` for everything, small and hot or large and cold. The
port wraps `malloc`, `calloc`, `realloc` and `free` at link time
(`picocalc/alloc.c`):

- a request **below a threshold** goes to the C library's heap in SRAM;
- a request **at or above it** goes to a second allocator over the PSRAM
  window;
- `free` and `realloc` tell the two apart by address.

The threshold is set so that guest RAM banks and ROM images, which are
read on every bus cycle, stay in SRAM, and disk sides, which are large and
touched only during disk transfers, go to PSRAM: **96 KB, to be settled by
the allocation profile** (§18). M1's host build logs every allocation
(size, caller, lifetime) through the same wrapper over a boot, a tape
load, a disk session, a machine change and a snapshot load; the profile
sets the threshold, sizes the SRAM heap, and shows whether anything small
is allocated often enough to fragment it.

`xmalloc()` aborts when memory runs out; the port's abort is the error
page (§11.4). The SRAM heap's high-water mark and largest free block are
on the heartbeat.

### 13.3 PSRAM (8 MB) and flash

PSRAM holds the large blocks: up to four disk images (a 720 KB disk is
about 1 MB as tracks with headers; four of them fit with room), and
whatever else crosses the threshold. Nothing per-frame lives there
(HW §9.3).

Flash holds the image: **estimated** 500–700 KB for XRoar as configured,
the SDK, FatFs and the port, against 16 MB.

---

## 14. Boot and exit

### 14.1 Boot

1. Clocks, rail, flash timing (§12.1). The log ring.
2. Core 1 launched: southbridge (version register checked, a failure
   said plainly, HW §6.1), LCD initialised and cleared, the pool.
3. PSRAM verified (§12.3). The card mounted; no card is a page, with a
   retry on card detect.
4. The shell's arguments read (`picocalc_shell.h`).
5. `xroar_init()` with the argument vector of §10.2. Upstream reads
   `xroar.conf`, finds ROMs, builds the machine, loads any media named.
6. No machine with its ROMs: the missing-ROM page (§10.3).
7. `xroar_init_finish()`, then the run loop. The guest's own power-on
   screen appears; the emulator is invisible until asked (EL §10).

Upstream's order of loading ROMs and powering on is upstream's, and the
host harness runs the same `main` path as the firmware, so the host cannot
hide a mistake in the firmware's order (EL §9.2).

### 14.2 Date and time

XRoar needs no wall clock to emulate. FatFs's `get_fattime()` uses the
shell's clock when it is set (Doom §13.2), so files saved on the card are
dated.

### 14.3 Exit

Quit (the File menu, or upstream's Ctrl command) goes through one
function:

1. `xroar_shutdown()`: upstream ejects disks (writing them back), closes
   tapes, and auto-saves the configuration if the user turned that on.
2. The card unmounted.
3. Core 1 parked between southbridge transactions, the audio DMA stopped
   (HW §6.1).
4. `picocalc_shell_exit()` when the loader is present; otherwise a page
   saying it is safe to switch off, and a power-off through the
   southbridge on a key.

Upstream's `main()` never expected to return; here it does. Pointers left
in `main`'s frame, at-exit handlers that call `exit()`, and anything
`xroar_shutdown()` assumes about being called once are checked in M7
(EL §2.6).

---

## 15. Development workflow

### 15.1 Probe and UART

A Debug Probe on SWD and UART0, GP2 (TX) / GP3 (RX), 115200 8-N-1 (D4).
`scripts/flash.sh` is Doom's: capture first, `reset halt`, DMA and PWM
held in reset, `program`, `resume`; never `reset run`, never a bare
`halt` (HW §2.7). `caffeinate -d` while the probe is in use. One reader on
the port, and a writer that opens it once for a whole run (HW §2.7).

### 15.2 Two builds

- **Development**: the log and typed keys on UART0. Bytes received become
  PicoCalc key events, with reserved bytes for the menu, pause and a
  screen dump (EL §13.1). The **screen dump** decodes the VDG's text
  screen from guest RAM through the VDG's character set and prints it,
  which is how a scripted run and the soak read what the guest printed
  (EL §11.2).
- **Release**: no UART; logging compiled out; counters in one block at a
  fixed symbol with a magic word, read over SWD twice and kept when the
  reads agree (HW §2.7). This is the build that ships and the build that
  is soaked.

Build-time options preload media and settings for scripted runs, each in
its own build directory (EL §13.1).

### 15.3 Log and heartbeat

XRoar's logging and `stdio`'s console go into the log ring on core 0, and
core 1 pumps it to the UART (EL §2.3). Once a second, a heartbeat:
real-time ratio; core 0's share inside `xroar_run()` and outside it; host
cycles per bus cycle; fields run, rendered, published, presented and
dropped; rows sent; longest present; underrun samples by cause, late
refills, queue depth and low water, samples consumed a second (the
control); keys received and dropped; I²C errors; the SRAM heap's
high-water mark and largest free block; both stacks' high-water marks;
log lines dropped; battery and charging; die temperature. Every counter
must be able to fire (EL §12).

---

## 16. Testing

**Host tests of `picocalc/`**, no framework, one binary per area under
CTest, exit 77 a skip (EL §11.1): the frame pool under a randomised
interleaving; the scancode table (every PicoCalc code maps once; nothing
needs a swallowed chord); the held-key rules against captured southbridge
streams, the Alt-first Insert and the stray Space release among them; the
allocator split; the system calls over an in-memory FatFs; the audio
conversion; the patch round trip. Controls that must fail, and planted
bugs, as EL §11.1 asks.

**The host XRoar.** The same upstream source lists, the same patch, the
same `config.h`, and the port's UI, video and audio modules with their
hardware ends replaced by buffers, built for the workstation. It is the
port minus the board. With it:

- **Equivalence with upstream's own build.** XRoar built its own way
  (autotools, the null UI) and the host XRoar run the same test ROM and
  key script with upstream's trace on in both; the traces must be
  identical. This is the port's proof that its source lists, `config.h`
  and patch did not change the emulation. Upstream's CPU has no
  functional suite in this project: the port does not modify it, and this
  equivalence is the check (EL §3.3's "a skip is not a pass" applies to
  this test).
- **The real ROMs**, when `reference/` has them: boot each machine to its
  prompt, type a line, read the text screen; load and run a tape both
  fast and at the signal's pace; a DOS session on a disk image with a
  write-back; a snapshot saved mid-program and restored into a machine
  doing something else.
- **The video path**: frames from the port's `render_line` compared with
  frames from upstream's own RGB565 renderer at the same viewport,
  decimated, pixel for pixel, in every VDG mode and colour set, with
  cross-colour on and off; golden images committed only after they have
  been looked at (EL §5.7).
- **The keyboard**: every table entry typed through the real ROM in
  translated mode and read off the screen, alone and with each modifier;
  the hold and gap settled with a control that loses keys (EL §7.1).
- **The allocation profile** (§13.2), and a run of repeated machine
  changes and snapshot loads against a heap of the board's size.
- **Time**: a long run's tick count against the fields it produced, to
  show the run loop's slicing does not drift (§6.2).

**On the board**: the gate (§6.4); the machine's registers and cycle
count at the end of each of the first 150 fields, equal to the host's
(EL §13.3); scripted workloads typed over the UART, each feature measured
against a control build in one sitting (EL §12); and the soak (M11).

**CI** on every push: both builds, `picocalc/` under `-Werror`, the host
tests, `check_sources.sh`, the patch round trip, and the equivalence run
with the project's own test ROM.

---

## 17. Milestones

Each ends with something that runs and something that is measured, and is
recorded in `docs/milestones.md` with the date, the board, the compiler,
what was checked and by whom, and a not-checked list carried forward
(EL §14.3). One branch and pull request each (D15). A milestone is not
done until its hardware findings are in the notes (D6).

```
 host only                              device
 M0 skeleton, patch pipeline, CI ─────────────────────────────┐
 M1 host XRoar ───────────────► M2 machine on board (GATE) ─┐ │
                                M3 board bring-up ◄──────────┼─┘
 M4 video path on host ───────► M5 GUEST BOOTS ON DEVICE ◄───┘
                                M6 audio and pacing
                                M7 menu, file requester, xroar.conf, exit
                                M8 tape → M9 disks → M10 snapshots,
                                   cartridges, joysticks, TV controls
                                M11 performance pass and soak → M12 release
```

| M | What | Done when |
|---|---|---|
| **M0** | Repository, submodule pinned (D13), the patch pipeline with an empty patch, both builds, CI, `CLAUDE.md`, `milestones.md` | CI green; the round-trip test passes; an SDK include in a host-tested file fails the host build |
| **M1** | The host XRoar: source lists, `config.h`, `check_sources.sh`, stub modules, the test ROM | The equivalence run passes; with real ROMs a Dragon 32 reaches its prompt and answers a typed line; the allocation profile is recorded; §18's M1 rows are settled |
| **M2** | **The gate**: board, clocks and log only; XRoar's machine alone on the board | §6.4's figures measured and recorded, board counts equal to host counts; pass, or the levers applied and the result taken to the owner; D7 confirmed or revised |
| **M3** | Bring-up from Doom's layer: southbridge, LCD test pattern, keyboard log, card, PSRAM check, exit to the shell | Each stage's smoke test seen on the panel or in the log (EL §13.3) |
| **M4** | The video module on the host: lines to indices, palette probe, cross-colour, the presenter's diff against a simulated panel | Pixel-for-pixel agreement with upstream's renderer; golden images looked at |
| **M5** | **The guest boots on the device**: run loop, pool, presenter, keyboard through `hkbd`, timer pacing | A Dragon 32 and a CoCo 2 boot to their prompts on the panel and run a typed program; the 150-field list equals the host's; the owner has typed on it |
| **M6** | Audio: the PWM ring, the audio module, pacing on the queue, counters | Sound heard at the right pitch; samples consumed a second at nominal; zero underruns at the prompt and in a sound loop; the late path forced once |
| **M7** | The menu, the file requester, `xroar.conf` and its save, the missing-ROM and About pages, pause, exit | Every menu item changes what upstream reports; a configuration saved and read back; exit returns to the shell with the southbridge answering |
| **M8** | Tape: load fast and at pace, save, rewrite, the status line, rate limit off | A program saved on the device loads in desktop XRoar, and the reverse |
| **M9** | Disks: the DOS cartridges, four drives, write-back, the heap split in PSRAM | A DOS session with files written and read back after a power cycle; write-back's time and safety measured |
| **M10** | Snapshots, ROM cartridges, the sound cartridges, keyboard joysticks, TV controls | A snapshot from the device loads in desktop XRoar, and the reverse; a joystick game played by the owner |
| **M11** | Performance pass against the previous milestone's image in one sitting; the 30-minute soak, development and release builds | Every counter zero on every heartbeat with the workload seen running (EL §11.5); the release build's counters read over SWD |
| **M12** | Release: README, licence texts, attribution, the UF2 | A fresh card and a flashed board, set up from the README alone |

After release, each its own design section first: **CoCo 3** (the GIME's
640-sample modes on a 320-pixel panel, 512 KB of guest RAM, a second
gate), **MC-10**, RAM expansions, the block-device cartridges.

---

## 18. Unverified constants and assumptions

Written from a reading of the source and from the companions, not from
execution. Each is settled before the code that depends on it, and how it
was settled is recorded here (EL §14.2).

| What | Assumed | Confidence | Settled by |
|---|---|---|---|
| Host cycles per bus cycle, and the VDG's and sound's costs (§6.4) | The table of §6.4 | **Low** | M2, on the board |
| The port's UI and audio modules can be supplied without a patch (§5.3) | Yes: `ui_module_list` is an assignable pointer, and the UI module carries its own `ao_module_list` | High (read from `ui.c`, `xroar.c`) | M1, by building |
| `hkbd.c` works with a symbol table installed by the port and no desktop back end (§9.2) | Yes | Medium | M1, by building and typing on the host |
| A UI module can register its own options with `xconfig` (§10.4) | Yes, as upstream's own modules do | Medium | M1, reading `xroar.c` and `ui.h` |
| `vo_render.c` can be built alone as a colour calculator, and its size on the heap (§7.3) | Yes; tens of KB | Low | M4 |
| `render_line` is called for every one of 262 lines in line order, with no data on a skipped field, and `draw` once a shown field (§7.2) | Yes | High (read from `mc6847.c`, `dragon.c`, `vo.h`) | M4, by counting |
| The active area's first sample and line within what `render_line` is given (§7.1) | `VDG_ACTIVE_LINE_START`, `VDG_ACTIVE_AREA_START` from `mc6847.h` | High | M4, golden images |
| PAL machines' line count and field rate as the front end sees them | Upstream pads to 50 Hz outside the VDG's 262 lines | Low | M4, by counting lines a field on a Dragon |
| The allocation profile: sizes, counts, the SRAM heap needed, the threshold (§13.2) | 48–96 KB small; threshold 96 KB | Low | M1 |
| Guest RAM and ROMs are allocations below the threshold | Yes for 64 KB machines | Medium | M1 |
| Core 0's stack depth (§13.1) | Under 8 KB | Low | M2 onward, painted |
| No `double` arithmetic on the per-cycle or mixing paths (§8.2) | None | Medium | M2, the disassembly |
| The key hold and gap each ROM needs (§9.2) | A few fields | Low | M5, executing the ROMs |
| Upstream's disk write-back is not safe against power loss (§10.6) | Writes the file in place | Low | M9, reading `vdisk.c` |
| WAV tapes load without libsndfile (§10.5) | Yes, through `sndfile_compat.c`, a WAV-only stand-in | Medium (read from its header comment) | M8 |
| `xroar_shutdown()` may be called and `main` may return (§14.3) | Yes | Medium | M7 |
| The image's size in flash (§13.3) | 500–700 KB | Low | M1 (host), M2 (linker map) |
| The submodule can be cloned by CI from upstream's server reliably (D13) | Unknown | Low | M0 |

---

## 19. Risks

1. **Speed.** XRoar's per-cycle design may not reach full speed on a
   300 MHz M33 (§6.4). The gate is the second milestone so that this is
   known before anything is built on it, and the levers are ordered.
   Failing it is a decision for the owner, not a problem to engineer
   around quietly.
2. **SRAM.** The budget's upper end is the whole 520 KB (§13.1). The
   allocation profile (M1) and the linker map (M2) come before the
   display and audio are built, and the two reliefs are named.
3. **Heap fragmentation.** XRoar frees and rebuilds its part tree on a
   machine change and a snapshot load, on a small heap. A host run
   against a heap of the board's size shows it, or shows it does not
   happen (§16).
4. **300 MHz is an overclock** (§12.1), and this port may depend on it
   where Doom only benefited.
5. **Upstream moves.** XRoar is actively developed; its internal
   interfaces (the UI tags, the module structures) are not a stable API.
   A pinned release, a patch that is small or empty, and
   `check_sources.sh` keep a bump to a bounded job, and the equivalence
   test says whether it worked.
6. **Upstream's server.** `www.6809.org.uk` serves git over the dumb HTTP
   protocol only: no shallow clone, and CI depends on its availability. A
   mirror of upstream in the owner's GitHub account, updated at each
   bump, would remove that (D13, open).
7. **Disk changes lost at power-off** (§10.6): the user must eject or
   quit. The status line and "write back now" reduce it; they do not
   remove it.
8. **The keyboard.** The PicoCalc cannot deliver some Shift chords, and
   resolves Shift before the port sees a key (§9). Translated mode suits
   that well; raw mode, which games use, is where the misfit shows, and
   M5 finds its extent.
9. **`xroar.conf` regenerated on save** loses the user's comments
   (§10.4), against EL §8.7. Accepted with D5, said in the menu.

---

## 20. Dropped and deferred

**Dropped**, with the reason, so nobody re-plans them (EL §14.5):

- **Scaling the picture** to fill the panel: uneven pixel doubling for
  56 % more wire time (EL §5.4, D11).
- **Composite video simulation** (`-ccr partial`, `simulated`), the
  OpenGL filters, zoom and picture-area choices: floating-point work per
  sample, for a 320-pixel panel (D12).
- **Mid-line border changes**: the frame keeps one border colour a line
  (§7.2).
- **GDB target, tracing in the firmware, the Becker port, printing to a
  pipe**: they need a host that is not there. Printing to a file is
  upstream's and stays if it costs nothing.
- **Host joysticks and the mouse**: none is wired.
- **Wi-Fi**: nothing uses the Plus 2 W's radio (D2).
- **ROMs in the firmware**: no permission covers them (§10.3).
- **The emulator family's menu and `key = value` settings file**: the
  owner chose XRoar's vocabulary and `xroar.conf` (D5).
- **Internal flash writes** (D10).

**Deferred**, with what would bring each back:

- **CoCo 3** and **MC-10** (D3): after release, each with a design
  section of its own (§17).
- **Pico 2 and Pico 2 W** (D2): would need disks served from the card
  rather than memory, a `vdisk.c` patch of some size, and a tighter heap.
- **RAM expansions** (NX32, MOOH, SAMx8, iMMUnity): large guest RAM on
  the per-cycle path, so a PSRAM cost to measure first.
- **IDE and SD-card cartridges, hard-disk images**: upstream's block
  devices over `stdio` should work as they are; untested until asked for.
- **150 MHz**: if the gate shows it is enough (D7).
- **Gzipped images** (zlib) and **audio-file tapes through libsndfile**:
  each is a library to carry.
- **An edit-in-place writer for `xroar.conf`**: if the regenerated file
  proves a nuisance (§10.4).
