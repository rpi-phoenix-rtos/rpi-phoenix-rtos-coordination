# Manifest: Pi 4 GIC CPU Interface Alias Probe

- Date: `2026-03-20`
- Step: `STEP-0217`
- Status: `completed`

## Goal

- determine whether the alternate CPU-interface pending view differs from the
  current `GICC_HPPIR` result on the direct Pi 4 QEMU timer path

## Implementation

- added `interrupts_getAliasedHighestPending()` in
  `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- exposed the helper in
  `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.h`
- extended the bounded timer probe in
  `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
  to print one `gtimer: ahppir ...` line

## Validation

### Generic `virt` guardrail lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - `qemu-system-aarch64 -machine virt,secure=on,gic-version=2 -cpu cortex-a53 -smp 1 -m 1G -serial mon:stdio -serial null -display none -kernel _boot/aarch64a53-generic-qemu/plo.elf -device loader,file=_boot/aarch64a53-generic-qemu/loader.disk,addr=0x48000000,force-raw=on`
- Key evidence:
  - `gtimer: hppir 1023`
  - `gtimer: ahppir 0`
  - successful timer dispatch still occurs

### Pi 4 A72 QEMU lane

- Build:
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - `$HOME/tools/qemu-10.2.2/bin/qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -smp 4 -m 2G -nographic -monitor none -kernel _boot/aarch64a72-generic-rpi4b/plo.elf -device loader,file=_boot/aarch64a72-generic-rpi4b/rpi4b/loader.disk,addr=0x48000000,force-raw=on`
- Key evidence:
  - `gtimer: hppir 0`
  - `gtimer: ahppir 524`
  - no timer dispatch

## Result

- the alternate CPU-interface view does differ on Pi 4, but not in a way that
  is self-explanatory from the current runtime evidence alone
- the stronger follow-up clue now comes from source inspection:
  - Phoenix DTB matching still special-cases `interrupt-controller@...` only
    under `amba_apu`
  - the official Pi 4 DTB exposes `/soc/interrupt-controller@40041000`

## Next Step

- verify the runtime GIC base provenance directly on both lanes before adding
  more CPU-interface probes
