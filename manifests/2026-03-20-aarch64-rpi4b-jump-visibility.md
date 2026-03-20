# Manifest: Filtered Pi 4 `hal_cpuJump()` Visibility

- Date: `2026-03-20`
- Step: `STEP-0170`
- Status: `completed`

## Goal

- split the Pi 4 official-DTB jump-path silence after `go: jump` without widening beyond `plo/hal/aarch64/generic/hal.c`

## Upstream Repository

### `plo`

- Commit: `568eabe`

## Changes

Updated:

- `sources/plo/hal/aarch64/generic/hal.c`

Added raw jump-path markers for:

- missing-entry failure
- entry to `hal_cpuJump()`
- after `hal_interruptsDisableAll()`
- immediately before `hal_exitToEL1()`
- unexpected return from `hal_exitToEL1()`

## Validation

Environment:

- `phoenix-dev`
- copied buildroots:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0170-generic`
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0170`
- QEMU `10.2.2`
- Pi 4 DTB source:
  - `https://github.com/raspberrypi/firmware`
  - commit `63ad7e7980b030cb4649ecedf2255c9226e5a1e8`
  - `boot/bcm2711-rpi-4-b.dtb`

Build validation:

- `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

Runtime validation:

1. Generic `virt`

   - now shows:
     - `hal: jump entry`
     - `hal: jump irq off`
     - `hal: jump exit el1`
   - and then immediately reaches:
     - `Phoenix-RTOS microkernel v. 3.3.1`

2. Pi 4 DTB-backed `raspi4b`

   - now also shows:
     - `hal: jump entry`
     - `hal: jump irq off`
     - `hal: jump exit el1`
   - but still never reaches:
     - `Phoenix-RTOS microkernel v. 3.3.1`
     - any kernel, timer, or user-space log
   - and does not show:
     - `hal: jump returned`

## Conclusion

- the Pi 4 official-DTB lane is no longer blocked in the C-side jump path
- both lanes reach the exact call into `hal_exitToEL1()`
- the current Pi 4 boundary is now strictly inside the assembly EL handoff in `plo/hal/aarch64/generic/_init.S` or in the first instructions executed after that handoff
- the next bounded step should target assembly-side EL-exit visibility only

## Selected Next Step

- scope the first assembly-side Pi 4 EL-exit visibility step
