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

Not in the tree, and in no image (`docs/design.md` D2, D8, §15.2). From M1 a
script builds it from its own checkout at a pinned release, outside the
tree, and its `mc6809.c` and `hd6309.c` are compiled only into a host test.
XRoar is Ciaran Anscomb's, under the GPL-3.0-or-later.
