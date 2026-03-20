# Manifest: Generic Secondary-Core Containment Across Kernel Handoff

- Date: `2026-03-20`
- Step: `STEP-0175`
- Status: `completed`

## Goal

- keep non-boot CPUs trapped in generic AArch64 `plo` across the current kernel handoff so the single-core generic kernel target is no longer handed multiple loader CPUs on `-smp 4` lanes

## Upstream Repository

### `plo`

- Commit: `7aa5938`

## Changes

Updated:

- `sources/plo/hal/aarch64/generic/_init.S`

Changed the generic `other_core_trap` path to keep non-boot CPUs parked in the trap loop instead of branching into `hal_exitToEL1()` after `hal_coreJumpFlag` changes.

## Validation

Environment:

- `phoenix-dev`
- copied buildroots:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0175-generic`
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0175`
- QEMU `10.2.2`
- Pi 4 DTB source:
  - `https://github.com/raspberrypi/firmware`
  - commit `63ad7e7980b030cb4649ecedf2255c9226e5a1e8`
  - `boot/bcm2711-rpi-4-b.dtb`

Build validation:

- `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

Runtime validation:

1. Generic `virt -smp 4`

   - still reaches:
     - `Phoenix-RTOS microkernel v. 3.3.1`
     - later timer, tty, and kernel startup logs
   - no longer relies on repeated secondary-core `A3` handoff markers to reach the kernel banner
   - does show new post-banner exception noise, which must be treated as a follow-up clue rather than as a blocker for this bounded step

2. Pi 4 `raspi4b -smp 4`

   - now reaches only a single:
     - `A3`
   - no longer shows:
     - repeated `AAA333`
     - later repeated `A3`
   - still does not reach:
     - `Phoenix-RTOS microkernel v. 3.3.1`

## Conclusion

- releasing secondary cores from generic `plo` was not the root cause of the Pi 4 failure after the EL3 transfer
- the containment experiment removed the repeated Pi 4 handoff noise and made the remaining boundary cleaner
- the Pi 4 failure is now more tightly bounded:
  - after the single loader EL3 transfer marker `A3`
  - before the first visible kernel banner
- the next bounded step should target the earliest visible kernel entry point

## Selected Next Step

- scope the smallest earliest-kernel-entry visibility change for the generic AArch64 kernel
