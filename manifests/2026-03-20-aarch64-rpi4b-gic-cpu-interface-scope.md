# Manifest: Pi 4 GIC CPU Interface Scope

- Date: `2026-03-20`
- Step: `STEP-0214`
- Status: `completed`

## Goal

- select the smallest next direct-GIC follow-up on the Pi 4 QEMU fast lane now
  that the local-controller detour has been removed

## Evidence Reviewed

Restored Pi 4 QEMU evidence:

- `gic: timer handler set grp 1 en 1`
- `gtimer: pending 0`
- `gtimer: ppi pending 0`
- `gtimer: post 2000 us ctl 0x5 ...`
- no `gic: timer dispatch`

QEMU source evidence:

- `hw/arm/bcm2838.c` wires `GTIMER_PHYS -> GIC PPI 14`
- the direct QEMU path therefore ends at the GIC CPU/distributor interfaces,
  not the BCM2836 local controller

## Selected Next Experiment

- add one bounded GIC CPU-interface readback in the existing timer probe:
  - read `GICC_HPPIR`
  - report the pending interrupt ID seen by the CPU interface
  - compare it with the existing distributor pending readback and the lack of
    dispatch

## Why This Is The Right Next Step

- it stays entirely on the direct QEMU path now proven by source
- it changes one visibility variable without changing scheduler or timer
  programming logic
- it can distinguish “the CPU interface sees a pending interrupt but the
  dispatcher path is missing it” from “nothing is visible even at the CPU
  interface”

## Selected Next Step

- implement the bounded Pi 4 GIC CPU-interface pending probe and validate it on
  the Pi 4 A72 QEMU lane, with a generic build guardrail
