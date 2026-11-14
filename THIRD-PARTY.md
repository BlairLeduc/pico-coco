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

## XRoar, the reference and the CPU oracle

Not in the tree, and in no image (`docs/design.md` D2, D8, §15.2).
`tools/trace/build-xroar.sh` clones it at the pinned release (1.13) into
the gitignored `reference/`, and builds it headless in `out/`. CMake
compiles its `src/mc6809/mc6809.c` and `portalib/delegate.c` from that
checkout into `test_mc6809_oracle` alone, through `test/host/xroar_cpu.c`,
this project's driver; `hd6309.c` joins them in M15. XRoar is Ciaran
Anscomb's, under the GPL-3.0-or-later.
