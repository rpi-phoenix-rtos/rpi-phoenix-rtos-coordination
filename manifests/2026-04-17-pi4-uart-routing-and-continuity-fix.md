# 2026-04-17 Pi 4 UART Routing And Continuity Fix

## Summary

This step moves the primary Pi 4 observability lane back to UART by making
PL011 `UART0` the explicit primary serial path on `GPIO14/15`, then
reinitializing PL011 back to `115200` immediately after firmware handoff.

The goal is not to suppress the firmware-side switch to `103448.300000 Hz`.
The goal is to regain a readable UART stream immediately after that switch.

## Why This Step Was Needed

Recent real-board logs still stopped at:

- `uart: Set PL011 baud rate to 103448.300000 Hz`
- `uart: Baud rate change done...`

That made LED-based diagnosis the fallback lane again, which was too slow and
too perturbing.

External re-check of the official Raspberry Pi UART docs confirmed that on
Pi 4 the correct way to make PL011 `UART0` primary on `GPIO14/15` is to use:

- `dtoverlay=disable-bt`, or
- `dtoverlay=miniuart-bt`

The same docs also state that `miniuart-bt` requires a fixed VPU/core clock
such as:

- `force_turbo=1`, or
- `core_freq=250`

For the active Phoenix lane we selected:

- `dtoverlay=miniuart-bt`
- `init_uart_clock=48000000`
- `force_turbo=1`
- `core_freq=250`

## Source Changes

### `phoenix-rtos-project`

- commit:
  - `bedfb8d` `project/rpi4b: restore pl011 continuity across firmware handoff`

- `_projects/aarch64a72-generic-rpi4b/config.txt`
  - added:
    - `init_uart_clock=48000000`
    - `dtoverlay=miniuart-bt`
- `_projects/aarch64a72-generic-rpi4b/build.project`
  - now stages:
    - `overlays/miniuart-bt.dtbo`
- `_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`
  - now reinitializes PL011 to `115200`
  - emits:
    - `AS0`
- `_projects/aarch64a72-generic-rpi4b/phoenix-kernel8-reloc.S`
  - now reinitializes PL011 to `115200`
  - emits:
    - `TR0`
    - `TR1`
    - `TR2`
    - `TR3`

### Coordination Repo

- `scripts/assemble-rpi4b-bootfs.sh`
  - hardened so the canonical export path explicitly copies
    `overlays/miniuart-bt.dtbo` whenever `config.txt` requests
    `dtoverlay=miniuart-bt`
- `scripts/summarize-rpi4b-uart-log.py`
  - now classifies:
    - `AS0` as `phoenix_armstub`
    - `TR0..TR3` as `phoenix_trampoline`

## Important Warning Found And Fixed

The first export from this step was incomplete:

- the staged image expected `miniuart-bt.dtbo`
- but the exported FAT image did not contain `overlays/miniuart-bt.dtbo`

Root cause:

- the canonical bootfs assembly helper did not reliably preserve overlays in
  the exported bootfs

Fix:

- `scripts/assemble-rpi4b-bootfs.sh` now explicitly stages
  `overlays/miniuart-bt.dtbo` from the authoritative Raspberry Pi firmware tree

This warning was treated as boot-significant and fixed before closing the step.

## Validation

- `./scripts/rebuild-rpi4b-fast.sh --scope project --qemu-sanity`
- `./scripts/assemble-rpi4b-bootfs.sh`
- `./scripts/assemble-rpi4b-bootfs-img.sh`
- `./scripts/assemble-rpi4b-sdimg.sh`
- `./scripts/export-rpi4b-sdimg.sh`
- `./scripts/verify-rpi4b-sdimg.sh`

Additional artifact verification:

- exported FAT image confirmed to contain:
  - `overlays/miniuart-bt.dtbo`

## Current Image

- path:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `8d4770cdf96a6af16fb1a1c85c75cdd267aff839caf8998f523dd2dac4a9ee15`

## Expected Next Real-Board Evidence

Using a normal `--profile firmware` capture:

- firmware log reaches the `103448.300000 Hz` line
- then:
  - `AS0` means the custom armstub regained UART at `115200`
  - `TR0..TR3` mean the reloc trampoline also regained UART and ran

Interpretation:

- no `AS0`, no `TR0..TR3`
  - still no post-firmware UART continuity
- `AS0` only
  - failure is between armstub handoff and trampoline entry
- `TR0..TR3`
  - UART continuity is restored through the Phoenix trampoline and the next
    blocker is later
