# media/

A staging area for cartridge, tape and disc images on the workstation.
**Everything in here except this file is gitignored**: the software is not
this project's to redistribute (design document §15.3).

The emulator loads them from the SD card, under `/coco/carts/`,
`/coco/tapes/` and `/coco/discs/` (design document §12.6). The host tests
that load real software look here, or in the directory `PICO_COCO_MEDIA`
names, and report each image they find; without any they pass on what
needs none.

| Folder | Images | Read by |
|---|---|---|
| `carts/` | `.ccc` or `.rom`, up to 16 KB | `test_cart` |
| `tapes/` | `.cas` | `test_tape` |
| `discs/` | `.dsk` (M13) | — |
