# Manifest: Pre-Map `dtb_getGIC()` Values via QEMU GDB

- Date: `2026-03-20`
- Step: `STEP-0224`
- Status: `completed`

## Goal

- inspect the exact `dtb_getGIC()` return values on both the generic and Pi 4
  lanes before `_pmap_halMapDevice()` rewrites them

## Validation

### Stop point

- breakpoint:
  `_hal_interruptsInit + 64`
- meaning:
  immediately after `dtb_getGIC(&gicc, &gicd)` returns and before either
  `_pmap_halMapDevice()` call
- local stack slots:
  - `gicd`: `sp + 0x48`
  - `gicc`: `sp + 0x40`

### Generic `virt` guardrail lane

- breakpoint hit successfully
- pre-map values:
  - `gicd = 0x0000000008000000`
  - `gicc = 0x0000000008010000`

### Pi 4 A72 `raspi4b` lane

- breakpoint hit successfully
- pre-map values:
  - `gicd = 0x0000000000000000`
  - `gicc = 0x0000000000000000`

## Result

- the Pi 4 lane is not discovering the GIC in the DTB at all
- the blocker is therefore upstream of timer runtime and CPU-interface policy:
  it is in DTB interrupt-controller discovery or `reg` decoding

## Supporting DTB Facts

- Pi 4 DTB path:
  `/soc/interrupt-controller@40041000`
- Pi 4 node properties:
  - `compatible = "arm,gic-400"`
  - `reg = <40041000 1000 40042000 2000 40044000 2000 40046000 2000>`
- DTB cell layout:
  - root: `#address-cells = 2`, `#size-cells = 1`
  - `/soc`: `#address-cells = 1`, `#size-cells = 1`
  - `/soc/ranges` translates the `0x40000000` bus window to CPU
    `0xff800000`

## Next Step

- scope the smallest `dtb.c` fix that:
  - matches the Pi 4 `interrupt-controller@...` node
  - decodes its `reg` property using `/soc` cell widths and address translation
