# Manifest: Pi 4 GIC DTB Discovery Fix

- Date: `2026-03-20`
- Step: `STEP-0226`
- Status: `completed`

## Goal

- make Pi 4 discover the GIC in `hal/aarch64/dtb.c` while preserving the
  existing generic and ZynqMP-oriented paths

## Implementation

Changed file:

- `sources/phoenix-rtos-kernel/hal/aarch64/dtb.c`

Bounded changes:

- `dtb_isInterruptControllerNode()` now also recognizes shallow
  `/soc/interrupt-controller@...` nodes
- `dtb_parseInterruptController()` now takes an `inSOC` flag
- when the interrupt-controller node is under `/soc`, `reg` decoding now uses:
  - `/soc` `#address-cells`
  - `/soc` `#size-cells`
  - `dtb_translateSocAddress()`
- the old fallback path for non-`/soc` layouts remains intact

## Validation

### Build guardrails

- generic build:
  `TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Pi 4 build:
  `RPI4B_DTB_PATH=.../bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- both succeeded in `phoenix-dev`

### Pre-map GDB verification

- Pi 4 at `_hal_interruptsInit + 64` now returns:
  - `gicd = 0xff841000`
  - `gicc = 0xff842000`

### Runtime QEMU verification

#### Generic `virt`

- still reaches:
  - `gic: timer dispatch`
  - `threads: timer irq`
  - `pl011-tty: console ready`

#### Pi 4 `raspi4b`

- now reaches:
  - `gic: timer dispatch`
  - `threads: timer irq`
  - `pl011-tty: tty0 ready`
  - `pl011-tty: console ready`
  - `main: Starting syspage programs ...`
  - `dummyfs: initialized`

## Result

- the Pi 4 early boot blocker was DTB GIC discovery, not timer-source policy
- the Pi 4 QEMU lane is now back in the same early boot band as the generic
  lane
- this is the first Pi 4 `raspi4b` QEMU milestone that clearly includes working
  timer dispatch and usable console registration

## Next Step

- scope one bounded later-boot parity check after `dummyfs: initialized` so the
  next blocker can be identified without re-opening the solved GIC path
