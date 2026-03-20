# Manifest: Pi 4 Timer Countdown Readback

- Date: `2026-03-20`
- Step: `STEP-0201`
- Status: `completed`

## Goal

- determine whether the Pi 4 timer actually counts down after the first arm or
  stays inert before the GIC pending boundary

## Implementation

- extended the existing first-arm timer trace in
  `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
- kept the pending probe bounded to the existing 2 ms window
- added one post-window readback line with:
  - elapsed probe time
  - timer control register
  - timer countdown value

## Validation

### Generic `virt` guardrail lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - `qemu-system-aarch64 -machine virt,secure=on,gic-version=2 -cpu cortex-a53 -smp 1 -m 1G -serial mon:stdio -serial null -display none -kernel _boot/aarch64a53-generic-qemu/plo.elf -device loader,file=_boot/aarch64a53-generic-qemu/loader.disk,addr=0x48000000,force-raw=on`
- Key evidence:
  - `gtimer: arm 1000 us ctl 0x1 tval 59726`
  - `gtimer: pending 1`
  - `gtimer: post 2000 us ctl 0x5 tval 4294890125`
  - `gic: timer dispatch`
  - `threads: timer irq`
  - `pl011-tty: tty0 wake`

### Pi 4 A72 patched lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - `$HOME/tools/qemu-10.2.2/bin/qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -smp 4 -m 2G -nographic -monitor none -kernel _boot/aarch64a72-generic-rpi4b/plo.elf -device loader,file=_boot/aarch64a72-generic-rpi4b/rpi4b/loader.disk,addr=0x48000000,force-raw=on`
- Key evidence:
  - `gtimer: arm 1000 us ctl 0x1 tval 59169`
  - `gtimer: pending 0`
  - `gtimer: post 2000 us ctl 0x5 tval 4294879752`
  - no `gic: timer dispatch`
  - no `threads: timer irq`

## Result

- the Pi 4 timer is not dead and is not stuck armed
- after the bounded probe window, both lanes show `ctl 0x5`, which means:
  - enable bit still set
  - timer interrupt status bit set
- the Pi 4 failure therefore moved later than local timer expiry:
  the timer expires locally, but the current GIC-side pending visibility or
  delivery path still does not reflect that expiry as a handled interrupt

## Next Step

- before widening the interrupt work, review one external Pi 4 bare-metal
  bring-up reference and then scope one bounded GIC PPI-state follow-up
