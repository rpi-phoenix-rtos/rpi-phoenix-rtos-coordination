# Manifest: Pi 4 Physical Timer Experiment

- Date: `2026-03-20`
- Step: `STEP-0197`
- Status: `completed`

## Goal

- force the Pi 4 patched lane from the current virtual timer to the
  non-secure physical timer and observe whether timer dispatch resumes

## Upstream Repositories

### `phoenix-rtos-kernel`

- Commit: `c971d9b6`

### `phoenix-rtos-project`

- Commit: `9c9f109`

## Changes

Updated:

- `sources/phoenix-rtos-kernel/hal/aarch64/dtb.c`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h`

Behavioral change:

- the common kernel DTB timer-source chooser now accepts an optional
  board-defined `DTB_FORCE_PHYS_TIMER` override
- the Pi 4 A72 board config enables that override for this bounded experiment

The generic `virt` lane is intentionally left on the existing common policy.

## Validation

Environment:

- `phoenix-dev`
- copied buildroot:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy`

Build validation:

- generic guardrail:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Pi 4 patched lane:
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

Runtime validation:

1. Generic `virt` lane

   - still reaches the established later boot band
   - still reports:
     - `gtimer: source virtual irq 27`
     - `gic: timer dispatch`
     - `threads: timer irq`
     - `pl011-tty: tty0 wake`

2. Pi 4 A72 patched lane

   - now reports:
     - `gtimer: source physical-nonsecure irq 30`
     - `gic: timer handler set grp 1 en 1`
     - `threads: wakeup programmed`
     - `dummyfs: devfs initialized`
   - still does not reach:
     - `gic: timer dispatch`
     - `threads: timer irq`
     - `pl011-tty: tty0 wake`

## Conclusion

- the timer-source experiment is negative
- the current Pi 4 patched-lane stall is not explained by the virtual-vs-
  physical timer choice alone
- the next bounded question is now interrupt delivery state:
  does the selected timer IRQ ever become pending in the GIC on the Pi 4 lane?

## Selected Next Step

- add one bounded GIC-side pending-state visibility step for the selected timer
  IRQ on the Pi 4 patched lane
