# Manifest: Pi 4 GIC Base Provenance via QEMU GDB

- Date: `2026-03-20`
- Step: `STEP-0220`
- Status: `completed`

## Goal

- establish a debugger-assisted Pi 4 `raspi4b` QEMU lane and use it to verify
  the live GIC base addresses without another kernel instrumentation patch

## Setup

Environment:

- QEMU:
  `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64`
- GDB:
  `gdb-multiarch 15.1` in `phoenix-dev`
- Symbols:
  `_build/aarch64a72-generic-rpi4b/prog/phoenix-aarch64a72-generic.elf`

Pi 4 launch shape:

- `-M raspi4b -cpu cortex-a72 -smp 4 -m 2G`
- current Pi 4 PLO/kernel lane
- `-gdb tcp::1234`

## Validation

### Pi 4 A72 `raspi4b` lane

- QEMU reached the current known stall after:
  - `gtimer: hppir 0`
  - `gtimer: ahppir 524`
  - `dummyfs: devfs initialized`
- attached with GDB using the current symbolized kernel ELF:
  - `target remote :1234`
  - `p/x interrupts_common.gicd`
  - `p/x interrupts_common.gicc`
  - `p/x *(unsigned int *)(interrupts_common.gicc + 6)`
  - `p/x *(unsigned int *)(interrupts_common.gicc + 10)`
  - `p/x *(unsigned int *)(interrupts_common.gicc + 0)`
  - `x/4wx interrupts_common.gicc`

Key results:

- live stop point:
  - `pc = 0xffffffffc000de50`
  - `hal_cpuHalt()` / idle thread context
- live mapped bases:
  - `interrupts_common.gicd = 0xffffffffffe01000`
  - `interrupts_common.gicc = 0xffffffffffe02000`
- live CPU-interface values:
  - `GICC_HPPIR = 0x0`
  - `GICC_AHPPIR = 0x620c`
  - `GICC_CTLR = 0x80000`

## Result

- the Pi 4 lane does reach a stable pair of mapped distributor and
  CPU-interface pointers in `interrupts_common`
- this proves that the current gdbstub lane can inspect the live interrupt
  state without another code patch
- this does not yet prove the exact pre-map physical values returned by
  `dtb_getGIC()`, so a direct breakpoint inspection is still the right follow-up

## Next Step

- take one bounded generic-versus-Pi4 GIC CPU-interface register snapshot via
  the new gdbstub lane before changing kernel code again
