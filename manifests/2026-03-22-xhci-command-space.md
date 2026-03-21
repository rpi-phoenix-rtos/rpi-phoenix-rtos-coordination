# STEP-0352

## Title

Implement the smallest xHCI memory-allocation step for `DCBAA` and the command ring

## Date

2026-03-22

## Repositories

- `phoenix-rtos-devices`
- coordination repo

## Change Summary

The Pi 4 xHCI path now allocates the first controller-owned memory objects
needed for later controller programming:

- one 4K-aligned `DCBAA` page
- one 64K-aligned command-ring backing block

The code:

- zeroes both allocations
- records their physical addresses
- validates the required alignment
- keeps the step strictly pre-register-programming and pre-enumeration

The command-ring allocation is intentionally conservative: the whole 64K-aligned
block is reserved now so the 64K boundary constraint is satisfied before later
ring-structure work.

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

The xHCI path now owns the first controller memory objects needed before later
register programming:

- `dcbaa`
- `cmdRing`
- `dcbaaPhys`
- `cmdRingPhys`

## Next Step

- scope the smallest xHCI register-programming step for `DCBAA`, `CRCR`, and
  `CONFIG`
