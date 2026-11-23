# Third-party material

Everything in this repository is this project's own work under the GPL-3.0 in
[`LICENSE`](LICENSE), with the exceptions below. Much of it is copied from
pico-atom and pico-oric, by the same author under the same licence
(`docs/design.md` §5, D9); a copied file that carries third-party material
brings its entry here with it.

## The CoCo's ROMs

Not in the tree, and not in the firmware (`docs/design.md` §12.1, D10).
`roms/` holds only a README; the emulator reads the ROMs from the SD card and
identifies them by SHA-1.

## Pico SDK import script — Raspberry Pi

`cmake/pico_sdk_import.cmake` is the Raspberry Pi Pico SDK's own
`external/pico_sdk_import.cmake`, copied as the SDK's instructions direct,
under the SDK's BSD-3-Clause licence.

## MC6847 character ROM — XRoar

`src/core/mc6847_font.c` and `src/core/mc6847_font.h` hold the MC6847's
internal 64-glyph character generator, copied from pico-atom (`docs/design.md`
§5). The table is taken **verbatim** from XRoar's `src/mc6847/font-6847.c` and
is byte-identical to it (checked against the pinned checkout, 2026-11-15); only
the comment header differs.

> XRoar — a Dragon and Tandy 8-bit computer emulator
> Copyright 2003-2026 Ciaran Anscomb <xroar@6809.org.uk>
> <https://www.6809.org.uk/xroar/>

XRoar is distributed under the GNU General Public License, version 3 or later,
the same licence as this project, so the data can travel with the repository.
`test/host/test_mc6847.c` asserts the layout the renderer assumes against the
data: 5 px wide in bits 5..1, glyph rows at 3..9 of the 12-row cell, and the
MC6847's own glyph order.

## MC6847T1 character ROM — XRoar

`src/core/mc6847_font_t1.c` holds the MC6847T1's internal 128-glyph
character generator, lower case among them (`docs/design.md` §9.2). The
table is taken **verbatim** from XRoar's `src/mc6847/font-6847t1.c` and is
byte-identical to it (checked against the pinned checkout, 2026-11-17);
only the comment header differs. The owner chose this, as for the MC6847's
(2026-11-17). Copyright and licence as the entry above: XRoar, Ciaran
Anscomb, GPL-3.0 or later. `test/host/test_vdg_oracle.c`, run with `t1`,
holds the renderer and this table to XRoar's MC6847T1 pixel for pixel.

## The sound bus's voltages — XRoar

`src/core/coco.c`'s `sound_gain` and `sound_offset` tables, and the same
figures in `test/host/sound_model.h`, are the voltages XRoar's
`src/sound.c` records as measured on a real Dragon for each source of the
sound multiplexer, with the single-bit output disabled, low and high, and
its 0.3 threshold for feeding the bus back to PIA1 PB1 (`docs/design.md`
§10, §18). Only the measured figures were taken, written as integers in
hundredths of a volt; the code around them is this project's. XRoar is
Ciaran Anscomb's, under the GPL-3.0-or-later, as this project is.

## The artefact colours — XRoar

`src/core/mc6847.c`'s `art5` table, the full artefacts' colours
(`docs/design.md` §9.5), is the 64 colours of XRoar's 5-bit cross-colour
table, `cmp_lut_5bit` in `src/vo_render.c`, generated from that file by
script and written as RGB565; the simple artefacts' blue and orange are its
2-bit table's. Only the colours were taken; the code that uses them is this
project's, held to XRoar's renderer by `test_vdg_oracle`. XRoar is Ciaran
Anscomb's, under the GPL-3.0-or-later, as this project is.

## XRoar, the reference and the CPU oracle

Not in the tree, and in no image (`docs/design.md` D2, D8, §15.2).
`tools/trace/build-xroar.sh` clones it at the pinned release (1.13) into
the gitignored `reference/`, and builds it headless in `out/`. CMake
compiles its `src/mc6809/mc6809.c` and `portalib/delegate.c` from that
checkout into `test_mc6809_oracle` alone, through `test/host/xroar_cpu.c`,
this project's driver; `hd6309.c` joins them in M15. From M4 its
`src/mc6847/mc6847.c`, the two fonts, `src/mc6883.c` and `src/events.c` are
compiled the same way into `test_vdg_oracle` alone, through
`test/host/xroar_vdg.c`; from 2026-11-22 its renderer, `src/vo_render.c`
with `ntsc.c`, `filter.c`, `colourspace.c`, `messenger.c`, `ui.c`,
`vdg_palette.c` and portalib's `xmalloc.c`, `intfuncs.c` and `slist.c`,
through `test/host/xroar_render.c`. From M3 the
headless XRoar also runs beside `tools/trace/coco-trace.c` for the trace
diff, as a separate program, sharing nothing with this project's code. XRoar is Ciaran
Anscomb's, under the GPL-3.0-or-later.

## PicoCalc LCD initialisation values — ClockworkPi

`src/port/lcd.c`, copied from pico-oric (`docs/design.md` §5), which had it
from pico-jupiter-ace and pico-atom, sends the panel's gamma, power, VCOM,
frame-rate, inversion, display-function and manufacturer commands with the
parameter bytes used by ClockworkPi's own driver,
`Code/picocalc_helloworld/lcdspi/lcdspi.c` in
<https://github.com/clockworkpi/PicoCalc> at commit
`f91519806d4b2e0a62c4638a9f695cd5162c5479`. Only the register values were
taken — the driver around them is the family's own — and hardware-notes.md
§4.4 directs using them because generic controller defaults may not suit
this glass. The pixel format (`0x55`) and entry mode (`0x06`) differ from that
driver's 18-bit setup and come from hardware-notes.md §4.4 instead.

GitHub detects no licence file in that repository at that revision. The
southbridge reply layout in `src/port/southbridge.c` was likewise checked
against the same repository's keyboard firmware, but no code was taken from it.

## The shell's return call — picocalc-shell

`third_party/picocalc-shell/picocalc_shell.h` is copied **verbatim** from
`include/picocalc_shell.h` in <https://github.com/BlairLeduc/picocalc-shell>
at commit `55f338ef6c3c1dc89291f1c9ae6f11d114f6ed97`, by way of pico-oric,
which took it at that commit. It is the call the menu's *Exit to shell*
makes to have UF2 Loader start the PicoCalc shell from the card
(design.md §14, D7).

> Copyright (c) 2026 Blair Leduc.
> SPDX-License-Identifier: GPL-3.0-or-later OR MIT

It is taken here under the GPL-3.0-or-later, this project's licence. To
update it, copy the file again unchanged and record the new commit above.

## FatFs — ChaN

The firmware links FatFs R0.15 (with patch 1) for the SD card, as its
siblings do; the card files were copied from pico-oric's `src/port/`. It is
**not in the tree**: CMake copies `ff.c`, `ff.h`, `ffunicode.c` and
`diskio.h` out of the Pico SDK's `lib/tinyusb/lib/fatfs/source/` into the
build directory at configure time. The one FatFs file in the repository is
`src/port/fatfs/ffconf.h`, which started as the SDK's copy and records in its
header which values were changed.

> FatFs — Generic FAT Filesystem Module
> Copyright (C) 2022, ChaN, all right reserved.
> <http://elm-chan.org/fsw/ff/>

FatFs's licence, from the header of `ff.c`, is reproduced in full:

```
Copyright (C) 2022, ChaN, all right reserved.

FatFs module is an open source software. Redistribution and use of FatFs in
source and binary forms, with or without modification, are permitted provided
that the following condition is met:

1. Redistributions of source code must retain the above copyright notice,
   this condition and the following disclaimer.

This software is provided by the copyright holder and contributors "AS IS"
and any warranties related to this software are DISCLAIMED.
The copyright owner or contributors be NOT LIABLE for any damages caused
by use of this software.
```

Its one condition applies to source, and `ffconf.h` carries the notice for
that reason. Binary redistribution has no condition in this version of the
licence; the notice is reproduced here anyway.
