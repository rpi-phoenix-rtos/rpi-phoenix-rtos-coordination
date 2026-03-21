# STEP-0344

## Title

Implement the smallest xHCI controller-register layout refinement

## Date

2026-03-22

## Repositories

- `phoenix-rtos-devices`
- coordination repo

## Change Summary

The Pi 4 xHCI path now extracts the controller-level doorbell and runtime
register offsets from the capability block:

- `DBOFF`
- `RTSOFF`

The code also adds the smallest sanity checks justified before any later
interrupter or ring work:

- non-zero doorbell offset
- non-zero runtime offset
- both offsets must stay inside the mapped controller MMIO window

The step remains intentionally pre-root-hub, pre-interrupt, pre-ring, and
pre-enumeration.

## Files

- `sources/phoenix-rtos-devices/usb/xhci/xhci.c`

## Validation

Validated in `phoenix-dev` with a fresh copied-buildroot Pi 4 A72 build:

```sh
./scripts/prepare-buildroot.sh --copy-components
cd ~/phoenix-buildroots/phoenix-rtos-project-copy
export PATH="$HOME/phoenix-toolchains/aarch64-phoenix/bin:$PATH"
TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image
```

Result:

- build completed successfully
- live staged Pi 4 image composition remained unchanged

## Result

The xHCI path now records the next register-layout facts needed before future
interrupter or ring work:

- `dboff`
- `rtsoff`

## Next Step

- scope the smallest pre-interrupt xHCI capability refinement after the new
  register-layout data
