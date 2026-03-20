# Manifest: Pre-Map GIC Base Inspection Scope

- Date: `2026-03-20`
- Step: `STEP-0223`
- Status: `completed`

## Goal

- choose the smallest debugger stop point that exposes the exact `dtb_getGIC()`
  return values before `_pmap_halMapDevice()` replaces them with mapped
  pointers

## Evidence Reviewed

Source context:

- `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
  shows:
  - line `365`: `dtb_getGIC(&gicc, &gicd);`
  - line `366`: first `_pmap_halMapDevice(gicd, ...)`
  - line `367`: second `_pmap_halMapDevice(gicc, ...)`

Disassembly of the current Pi 4 kernel ELF:

- `_hal_interruptsInit` call to `dtb_getGIC` is at `+60`
- the first instruction after the call is at `+64`
- the compiler stores:
  - `gicd` at `sp + 0x48`
  - `gicc` at `sp + 0x40`

## Selected Next Stop Point

- set a breakpoint at `_hal_interruptsInit + 64`
- continue until that point on each lane
- read:
  - `*(addr_t *)($sp + 0x48)` for `gicd`
  - `*(addr_t *)($sp + 0x40)` for `gicc`

## Why This Is The Right Next Step

- it is fully read-only
- it avoids guessing from post-map pointers
- it directly answers the remaining base-provenance question on both lanes

## Selected Next Step

- implement the cross-lane pre-map `dtb_getGIC()` base inspection via the QEMU
  gdbstub lane
