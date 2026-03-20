# Manifest: Generic AArch64 `plo` Secondary-Core Containment

- Date: `2026-03-20`
- Step: `STEP-0118`
- Status: `completed`

## Goal

- contain non-primary cores in generic AArch64 `plo` so the Pi 4 QEMU `plo.elf` lane can progress past the early multi-core startup storm

## Changes

Updated:

- `sources/plo/hal/aarch64/generic/_init.S`
- `sources/plo/hal/aarch64/generic/hal.c`

Patch summary:

- added a generic `hal_coreJumpFlag`
- added a non-primary-core trap in generic AArch64 startup:
  - check `mpidr_el1 & 0xff`
  - let only CPU 0 continue to `_startc`
  - park other cores in `wfe`
- set `hal_coreJumpFlag = 1` immediately before `hal_exitToEL1()` in `hal_cpuJump()`

## Validation

Environment:

- `phoenix-dev`
- copied buildroot:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-step0118-containment`

Build validation:

- `TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- `TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`

Both targets built successfully from the same patched `plo` sources.

Runtime validation:

1. Generic `virt` sanity check:

   - command:
     - `timeout 12s qemu-system-aarch64 -machine virt,secure=on,gic-version=2 -cpu cortex-a53 -smp 1 -m 1G -serial mon:stdio -serial null -display none -kernel _boot/aarch64a53-generic-qemu/plo.elf -device loader,file=_boot/aarch64a53-generic-qemu/loader.disk,addr=0x48000000,force-raw=on`
   - result:
     - `Phoenix-RTOS loader v. 1.21 ...`
     - `hal: Cortex-A53 Generic`
     - `cmd: Executing pre-init script`
     - `alias: Setting relative base address to 0x0000000000200000`
     - `Phoenix-RTOS microkernel v. 3.3.1 ...`
     - `pl011-tty: started`

2. Pi 4 `raspi4b` `plo.elf` smoke:

   - command shape:
     - `-machine raspi4b -cpu cortex-a72 -smp 4 -m 2G -nographic -monitor none`
     - `-kernel .../plo.elf`
     - `-device loader,file=.../loader.disk,addr=0x48000000,force-raw=on`
   - result:
     - `Phoenix-RTOS loader v. 1.21 ...`
     - `hal: Cortex-A53 Generic`
     - `cmd: Executing pre-init script`
     - `alias: Setting relative base address to 0x0000000000200000`
   - the earlier exception storm and garbled multi-core UART output are gone

## Conclusion

- the generic AArch64 `plo` secondary-core containment patch is successful
- the Pi 4 QEMU lane now fails at a stable post-alias boundary instead of collapsing immediately in multi-core startup noise
- the next blocker is no longer loader SMP chaos

## Selected Next Step

- compare the stable Pi 4 post-alias state against the known-good generic lane and select the smallest next Pi 4 boot blocker
- the strongest current candidate is DTB availability, because:
  - the generic QEMU build emits `/etc/system.dtb`
  - the current Pi 4 build without `RPI4B_DTB_PATH` does not
