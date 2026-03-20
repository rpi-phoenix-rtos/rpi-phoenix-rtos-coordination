# Manifest: GIC CPU Interface GDB Comparison Scope

- Date: `2026-03-20`
- Step: `STEP-0221`
- Status: `completed`

## Goal

- choose the smallest read-only generic-versus-Pi4 GIC CPU-interface register
  comparison now that the Pi 4 GIC base-address hypothesis is ruled out

## Evidence Reviewed

Pi 4 gdbstub result:

- `interrupts_common.gicd = 0xffffffffffe01000`
- `interrupts_common.gicc = 0xffffffffffe02000`
- `GICC_HPPIR = 0x0`
- `GICC_AHPPIR = 0x620c`
- `GICC_CTLR = 0x80000`

Runtime evidence already known from the generic lane:

- timer dispatch succeeds
- printed `gtimer: hppir 1023`
- printed `gtimer: ahppir 0`

## Selected Next Comparison

- take one bounded CPU-interface register snapshot on both lanes:
  - `interrupts_common.gicd`
  - `interrupts_common.gicc`
  - `GICC_CTLR`
  - `GICC_PMR`
  - `GICC_BPR`
  - `GICC_HPPIR`
  - `GICC_AHPPIR`

## Why This Is The Right Next Step

- it remains read-only
- it directly compares the live CPU-interface state between the working generic
  lane and the stuck Pi 4 lane
- it is the shortest path to determine whether the remaining issue is security
  view, CPU-interface mode, or a QEMU-model quirk

## Selected Next Step

- implement the bounded generic-versus-Pi4 GIC CPU-interface register snapshot
  via the QEMU gdbstub lane
