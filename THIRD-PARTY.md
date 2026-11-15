# Third-party material

Everything in this repository is this project's own work under the GPL-3.0 in
[`LICENSE`](LICENSE), with the exceptions below. Much of it is copied from
pico-atom and pico-oric, by the same author under the same licence
(`docs/design.md` §5, D9); a copied file that carries third-party material
brings its entry here with it.

## The CoCo's and the Dragon's ROMs

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

## XRoar, the reference and the CPU oracle

Not in the tree, and in no image (`docs/design.md` D2, D8, §15.2).
`tools/trace/build-xroar.sh` clones it at the pinned release (1.13) into
the gitignored `reference/`, and builds it headless in `out/`. CMake
compiles its `src/mc6809/mc6809.c` and `portalib/delegate.c` from that
checkout into `test_mc6809_oracle` alone, through `test/host/xroar_cpu.c`,
this project's driver; `hd6309.c` joins them in M15. From M4 its
`src/mc6847/mc6847.c`, the two fonts, `src/mc6883.c` and `src/events.c` are
compiled the same way into `test_vdg_oracle` alone, through
`test/host/xroar_vdg.c`. From M3 the
headless XRoar also runs beside `tools/trace/coco-trace.c` for the trace
diff, as a separate program, sharing nothing with this project's code. XRoar is Ciaran
Anscomb's, under the GPL-3.0-or-later.
