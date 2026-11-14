# pico-coco

A **Tandy Color Computer 1/2** emulator for the **ClockworkPi PicoCalc**, in C
against the Raspberry Pi Pico SDK, which will also run the **Dragon 32 and
64**. It is a member of the pico-atom family: pico-atom's architecture, its
MC6847, its port layer and its menu, with a new MC6809 in place of the 6502.

**Status: M1, the CPU on the host.** The MC6809 core runs under the host
tests, held instruction by instruction to XRoar's; the firmware still prints
only a banner and a heartbeat on UART1, and no machine is emulated yet. See [`docs/design.md`](docs/design.md) for the
design and [`docs/milestones.md`](docs/milestones.md) for what has been
checked.

No ROM is included: see [`roms/README.md`](roms/README.md).

## Building

```sh
# host: the portable core and its tests, no Pico SDK
cmake -S . -B build/host -DPICO_COCO_HOST=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host -j
ctest --test-dir build/host --output-on-failure

# the CPU oracle needs XRoar's checkout at the pinned release; without it
# the oracle test reports itself skipped
tools/trace/build-xroar.sh --checkout-only   # reference/xroar
tools/trace/build-xroar.sh                   # and a headless build in out/xroar

# firmware: build/pico/pico-coco.uf2, for every RP2350 board
tools/build.sh
tools/build.sh -DPICO_COCO_UART=OFF build/pico-release   # no UART
```

## Licence

GPL-3.0; see [`LICENSE`](LICENSE) and [`THIRD-PARTY.md`](THIRD-PARTY.md).
