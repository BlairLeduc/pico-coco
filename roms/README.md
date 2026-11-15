# roms/

A staging area for ROM images on the workstation. **Everything in here except
this file is gitignored**, because the CoCo's and the Dragon's ROMs are
copyrighted and this project does not redistribute them (design document
§12.1, D10).

The emulator loads ROMs from the SD card at `/coco/roms/` and identifies them
by SHA-1. The host tests will look for them here, by SHA-1, and skip the
tests that need a ROM that is absent; a skip is not a pass.

The files, their sizes and their hashes are written here once they are
settled from the owner's images and cross-checked against XRoar's list of
known ROMs (design document §18).

## Known images

Settled 2026-11-14 from the owner's images; XRoar 1.13's list of known
ROMs agrees with each by CRC-32 (design document §18). `src/core/romset.c`
carries the same table, and `test_romset` holds the two to each other.

| Slot | File | Size | CRC-32 | Image |
|---|---|---:|---|---|
| `$A000` | `bas13.rom` | 8192 | `d8f4d15e` | Color BASIC 1.3 |
| `$8000` | `extbas11.rom` | 8192 | `a82a6254` | Extended Color BASIC 1.1 |
| `$C000` | `disk11.rom` | 8192 | `0b9c5415` | Disk Extended Color BASIC 1.1 (M13) |

SHA-1:

```
28b92bebe35fa4f026a084416d6ea3b1552b63d3  bas13.rom
ad927fb4f30746d820cb8b860ebb585e7f095dea  extbas11.rom
10bdc5aa2d7d7f205f67b47b19003a4bd89defd1  disk11.rom
```
