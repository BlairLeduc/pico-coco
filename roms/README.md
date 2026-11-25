# roms/

A staging area for ROM images on the workstation. **Everything in here except
this file is gitignored**, because the CoCo's ROMs are
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
| `$A000` | `bas11.rom` | 8192 | `6270955a` | Color BASIC 1.1, for 4 KB CoCos |
| SSC | `pic-7040-510.bin` | 4096 | `a8e2eb98` | the Speech/Sound Cartridge's TMS7040 (M18) |
| SSC | `sp0256-al2.bin` | 2048 | `b504ac15` | its SP0256-AL2 (M18) |
| `$C000` | `orch90.rom` | 8192 | `15fb39af` | the Orchestra-90 CC (M18) |
| `$C000` | `rs232.rom` | 4096 | `d990e1f9` | the Deluxe RS-232 Program Pak (M19) |

SHA-1:

```
28b92bebe35fa4f026a084416d6ea3b1552b63d3  bas13.rom
ad927fb4f30746d820cb8b860ebb585e7f095dea  extbas11.rom
10bdc5aa2d7d7f205f67b47b19003a4bd89defd1  disk11.rom
cecb7c24ff1e0ab5836e4a7a8eb1b8e01f1fded3  bas11.rom
7c17dcbc21757535ce0b3a9e1ce5ca61319d3606  pic-7040-510.bin
e60fcb5fa16ff3f3b69d36c7a6e955744d3feafc  sp0256-al2.bin
6a20fee9c70b36a6435ac8378f31d5b626017df0  orch90.rom
3fad25f3462a0b581b9c182ac11ad90c8fa08cb6  rs232.rom
```

The two Speech/Sound Cartridge images (M18, 2026-11-24) are MAME's
`coco_ssc` set, its CRC-32s and SHA-1s as above; the owner's copies are
identical. The Orchestra-90's is MAME's and XRoar's image, and the
Color Computer Archive's `.ccc`; the owner's copy is identical. Chosen
as ORCHESTRA-90 on the Machine page, it is read from `/coco/roms/`, as
the disk controller's is; a copy in `/coco/carts/` is still known by
its SHA-1 (2026-11-25).

The Deluxe RS-232 Program Pak's (M19) is MAME's `coco_rs232` set,
`deluxe_rs-232_program_pak_1983_26-2226_tandy.rom`, by its CRC-32 and
SHA-1; the owner's copy is identical (2026-11-25).

## OS-9 disks, for `test_os9`

Not ROMs, but kept here for the same reason: they are found by SHA-1, and
Tandy's is not this project's to distribute. `tools/fetch-os9.sh` fetches
all of them (2026-11-17), and makes the last from 2.01's two disks with
`tools/os9-clock.py`. `test_os9` skips each part whose disk is absent.

```
521740e7a00eefb4b92a6c803391df31c004fdf9  OS9L1V2M.DSK   OS-9 Level One 02.00.00, Tandy (Color Computer Archive)
7019d3b476a3f944a6dab77c7c4c8e1d67b18863  OS9-L1V201M.DSK   OS-9 Level One 02.01.00, the system disk
0ade5dec3cdc0f34654e5ebfc779fb41c2a92737  OS9-L1V201B.DSK   OS-9 Level One 02.01.00, the second disk (/MODULES)
76fd68df3fc0527d1ceb49bc6772a7a7311d7636  OS9-L1V201M-RTC.DSK   the system disk with clock_tc3 (os9-clock.py --drop T1,RS232)
c417fad58ea8eca331e173922748e9903bc20fcd  nos96809l1v030300coco1_40d_1.dsk   NitrOS-9 3.3.0, 360K
8695113e8a9e7d616e7db055af2b1f4a00989808  nos96809l1v030300coco1_80d.dsk     NitrOS-9 3.3.0, 720K
```
