# Manifest: GIC CPU Interface Register Comparison via QEMU GDB

- Date: `2026-03-20`
- Step: `STEP-0222`
- Status: `completed`

## Goal

- compare one small live GIC CPU-interface register set between the working
  generic lane and the stuck Pi 4 lane without rebuilding Phoenix

## Validation

### Generic `virt` guardrail lane

- QEMU:
  `/usr/bin/qemu-system-aarch64 -machine virt,secure=on,gic-version=2 ... -gdb tcp::1235`
- Symbols:
  `_build/aarch64a53-generic-qemu/prog/phoenix-aarch64a53-generic.elf`
- GDB commands:
  - `target remote :1235`
  - `p/x interrupts_common.gicd`
  - `p/x interrupts_common.gicc`
  - `p/x *(unsigned int *)(interrupts_common.gicc + 0)`
  - `p/x *(unsigned int *)(interrupts_common.gicc + 1)`
  - `p/x *(unsigned int *)(interrupts_common.gicc + 2)`
  - `p/x *(unsigned int *)(interrupts_common.gicc + 6)`
  - `p/x *(unsigned int *)(interrupts_common.gicc + 10)`
  - `x/12wx interrupts_common.gicc`
- Key results:
  - `interrupts_common.gicd = 0xffffffffffe01000`
  - `interrupts_common.gicc = 0xffffffffffe02000`
  - `GICC_CTLR = 0x1`
  - `GICC_PMR = 0xfe`
  - `GICC_BPR = 0x3`
  - `GICC_HPPIR = 0x3ff`
  - `GICC_AHPPIR read slot = 0x0`

### Pi 4 A72 `raspi4b` lane

- QEMU:
  `/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64 -M raspi4b ... -gdb tcp::1234`
- Symbols:
  `_build/aarch64a72-generic-rpi4b/prog/phoenix-aarch64a72-generic.elf`
- GDB commands:
  the same bounded register set as above
- Key results:
  - `interrupts_common.gicd = 0xffffffffffe01000`
  - `interrupts_common.gicc = 0xffffffffffe02000`
  - `GICC_CTLR = 0x80000`
  - `GICC_PMR = 0x0`
  - `GICC_BPR = 0x620c`
  - `GICC_HPPIR = 0x0`
  - `GICC_AHPPIR read slot = 0x620c`

## Result

- the generic lane presents a normal, self-consistent CPU-interface view
- the Pi 4 lane presents a markedly different CPU-interface state at the same
  mapped pointer pair
- local QEMU `10.2.2` source in `hw/intc/arm_gic.c` does not implement an
  explicit `0x28` CPU-interface read case for `AHPPIR`, so the existing
  `AHPPIR` signal should not be treated as authoritative until the actual base
  selection and offset semantics are confirmed more directly

## Next Step

- inspect the pre-map `gicd` and `gicc` locals at `_hal_interruptsInit()`
  before `_pmap_halMapDevice()` so the exact values returned by `dtb_getGIC()`
  are known on both lanes
