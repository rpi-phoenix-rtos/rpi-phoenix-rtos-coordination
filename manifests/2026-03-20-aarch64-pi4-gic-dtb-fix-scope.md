# Manifest: Pi 4 GIC DTB Fix Scope

- Date: `2026-03-20`
- Step: `STEP-0225`
- Status: `completed`

## Goal

- choose the smallest `dtb.c` change that makes Pi 4 discover the GIC without
  widening into a broad DTB parser rewrite

## Evidence Reviewed

Runtime:

- Pi 4 `dtb_getGIC()` returns:
  - `gicd = 0`
  - `gicc = 0`

DTB shape:

- node path:
  `/soc/interrupt-controller@40041000`
- node properties:
  - `compatible = "arm,gic-400"`
  - `reg = <40041000 1000 40042000 2000 40044000 2000 40046000 2000>`
- cell widths:
  - root: `#address-cells = 2`, `#size-cells = 1`
  - `/soc`: `#address-cells = 1`, `#size-cells = 1`

Current Phoenix parser constraints:

- `dtb_isInterruptControllerNode()` only matches:
  - `interrupt-controller@...` under `amba_apu`
  - shallow `intc@...`
- `dtb_parseInterruptController()` currently assumes 64-bit address tuples and
  does not use `/soc` cell widths or address translation

## Selected Fix

- widen interrupt-controller node recognition just enough to match the Pi 4
  shallow `/soc/interrupt-controller@...` node
- make `dtb_parseInterruptController()` decode `reg` tuples via:
  - `/soc` `#address-cells`
  - `/soc` `#size-cells`
  - `dtb_translateSocAddress()`
- keep the old fallback path for existing non-`/soc` layouts

## Why This Is The Right Next Step

- it directly addresses the observed zero-base result
- it stays inside one file and one subsystem
- it preserves the existing generic and ZynqMP-oriented paths as fallback

## Selected Next Step

- implement the bounded Pi 4 GIC DTB discovery fix in `hal/aarch64/dtb.c`
