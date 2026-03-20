# Manifest: Pi 4 Timer Pending Probe

- Date: `2026-03-20`
- Step: `STEP-0199`
- Status: `completed`

## Goal

- determine whether the selected timer IRQ ever becomes pending in the GIC on
  the Pi 4 patched lane

## Upstream Repositories

### `phoenix-rtos-kernel`

- Commit: `TO_BE_FILLED`

## Changes

Updated:

- `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.h`
- `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`

Behavioral change:

- add a small GICv2 pending-bit read helper
- add a first-arm-only pending-state probe in `gtimer_timer.c`
- the probe samples the selected timer IRQ pending bit in a bounded 2 ms window
  and prints one result line

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

   - reaches:
     - `gtimer: source virtual irq 27`
     - `gtimer: arm 1000 us ...`
     - `gtimer: pending 1`
     - `gic: timer dispatch`
     - `threads: timer irq`

2. Pi 4 A72 patched lane

   - reaches:
     - `gtimer: source physical-nonsecure irq 30`
     - `gtimer: arm 1000 us ...`
     - `gtimer: pending 0`
     - `threads: wakeup programmed`
     - `dummyfs: devfs initialized`
   - still does not reach:
     - `gic: timer dispatch`
     - `threads: timer irq`
     - `pl011-tty: tty0 wake`

## Conclusion

- the selected timer IRQ does not become pending in the GIC during the bounded
  probe window on the Pi 4 patched lane
- the current blocker is therefore earlier than GIC CPU-interface dispatch
- the next bounded question is whether the timer countdown itself is advancing
  and reaching expiry on the Pi 4 lane

## Selected Next Step

- add a bounded timer-countdown readback after the pending probe window so the
  Pi 4 lane can be divided into:
  - timer not counting down correctly
  - timer counts down but still never asserts
