# STEP-0342

## Title

Implement the next xHCI structural-capability refinement

## Date

2026-03-22

## Repositories

- `phoenix-rtos-devices`
- coordination repo

## Change Summary

The Pi 4 xHCI path now extracts and validates the next controller-shape fields
after the existing page-size and port-count checks:

- `HCSPARAMS1` max slots
- `HCSPARAMS2` max scratchpad buffers
- `HCCPARAMS1` context size

The code now rejects:

- zero reported slots
- non-32-byte context size

The step remains intentionally pre-root-hub, pre-ring, and pre-enumeration.

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

The xHCI path now records the next structural facts needed before later
runtime-controller work:

- `nslots`
- `nscratchpad`
- `contextSize`

## Next Step

- scope the smallest controller-register layout step after the capability-shape
  refinement
