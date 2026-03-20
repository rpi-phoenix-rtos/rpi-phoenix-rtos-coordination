# Manifest: Pi 4 GIC CPU Interface Pending Probe

- Date: `2026-03-20`
- Step: `STEP-0215`
- Status: `completed`

## Goal

- determine whether the GIC CPU interface sees any pending interrupt after the
  Pi 4 timer expires on the direct `GTIMER_PHYS -> GIC PPI 14` QEMU path

## Implementation

- added `interrupts_getHighestPending()` in
  `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- exposed the helper in
  `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.h`
- extended the bounded timer probe in
  `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
  to print one `gtimer: hppir ...` line

## Validation

### Generic `virt` guardrail lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - `qemu-system-aarch64 -machine virt,secure=on,gic-version=2 -cpu cortex-a53 -smp 1 -m 1G -serial mon:stdio -serial null -display none -kernel _boot/aarch64a53-generic-qemu/plo.elf -device loader,file=_boot/aarch64a53-generic-qemu/loader.disk,addr=0x48000000,force-raw=on`
- Key evidence:
  - `gic: timer handler set grp 0 en 1`
  - `gic: timer dispatch`
  - `gtimer: pending 1`
  - `gtimer: ppi pending 0`
  - `gtimer: hppir 1023`
  - `threads: timer irq`

### Pi 4 A72 patched lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - `$HOME/tools/qemu-10.2.2/bin/qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -smp 4 -m 2G -nographic -monitor none -kernel _boot/aarch64a72-generic-rpi4b/plo.elf -device loader,file=_boot/aarch64a72-generic-rpi4b/rpi4b/loader.disk,addr=0x48000000,force-raw=on`
- Key evidence:
  - `gic: timer handler set grp 1 en 1`
  - `gtimer: pending 0`
  - `gtimer: ppi pending 0`
  - `gtimer: hppir 0`
  - no `gic: timer dispatch`

## Result

- the CPU-interface view now differs across the two lanes:
  - generic lane ends with `hppir 1023`
  - Pi 4 lane reports `hppir 0`
- the next bounded move should clarify whether Pi 4's `hppir 0` means:
  - a real SGI 0 pending at the CPU interface
  - or a security/aliasing artifact that needs the alternate pending view

## Next Step

- scope one bounded follow-up on the CPU-interface view, most likely
  `GICC_AHPPIR` or another equally narrow readback that can explain the Pi 4
  `hppir 0` result
