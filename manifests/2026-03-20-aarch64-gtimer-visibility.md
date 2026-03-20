# Manifest: Common AArch64 Timer Source / IRQ Visibility

- Date: `2026-03-20`
- Step: `STEP-0144`
- Status: `completed`

## Goal

- determine which common AArch64 timer source and IRQ are selected and whether the first wakeup arming reaches the timer frontend before the missing interrupt

## Upstream Repository

### `phoenix-rtos-kernel`

- Commit: `f46c7d8e`

## Changes

Updated:

- `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`

Added tightly filtered, one-time kernel timer markers for:

- selected timer source and IRQ
- first common timer wakeup arm request

The patch does not change timer policy, IRQ routing, or wakeup behavior.

## Validation

Environment:

- `phoenix-dev`
- copied buildroots:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0144-generic`
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0144`
- QEMU `10.2.2`

Build validation:

- `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/phoenix-buildroots/phoenix-step0132/_boot/aarch64a53-generic-rpi4b/rpi4b/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

Runtime validation:

1. Generic `virt`

   - reached:
     - `gtimer: source physical-nonsecure irq 30`
     - `pl011-tty: tty0 lookup retry`
     - `threads: nsleep enter`
     - `gtimer: arm 1000 us`
     - `threads: wakeup programmed`
     - later `name: register devfs`
     - later `dummyfs: devfs registered`
     - later `dummyfs: devfs initialized`
   - did not reach before timeout:
     - `threads: timer irq`
     - `pl011-tty: tty0 wake`

2. Pi 4 DTB-backed `raspi4b`

   - remained unchanged at:
     - loader startup
     - `pl011-tty: started`
     - `pl011-tty: register tty0`
     - `pl011-tty: tty0 lookup`
     - `pl011-tty: tty0 lookup retry`
   - did not expose visible new `gtimer:` markers in this boot slice

## Conclusion

- the common AArch64 timer frontend selects `physical-nonsecure irq 30` on the generic fast lane
- the first wakeup arm request reaches `gtimer_timer.c` and is capped to `1000 us` by the current scheduler wakeup policy
- despite that arming, no timer interrupt reaches `threads_timeintr()` before timeout
- the next bounded blocker is now the GIC-side timer handler registration / dispatch path for the selected timer IRQ, not timer arming itself
- the Pi 4 DTB-backed lane still does not expose the new kernel-side timer markers in this boot slice, so the generic lane remains the authoritative fast diagnostic lane

## Selected Next Step

- scope the smallest GIC timer registration / dispatch visibility step before attempting a timer-source or GIC fix
