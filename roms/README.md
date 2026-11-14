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
