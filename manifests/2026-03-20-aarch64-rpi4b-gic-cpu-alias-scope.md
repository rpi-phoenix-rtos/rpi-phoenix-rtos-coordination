# Manifest: Pi 4 GIC CPU Interface Alias Scope

- Date: `2026-03-20`
- Step: `STEP-0216`
- Status: `completed`

## Goal

- select the smallest next read-only follow-up that can explain why Pi 4 QEMU
  reports `gtimer: hppir 0` while the timer still never dispatches

## Evidence Reviewed

Current direct-GIC evidence:

- generic lane:
  - `gtimer: pending 1`
  - `gtimer: hppir 1023`
  - successful timer dispatch
- Pi 4 lane:
  - `gtimer: pending 0`
  - `gtimer: ppi pending 0`
  - `gtimer: hppir 0`
  - no timer dispatch

Current kernel source facts:

- `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
  already defines both:
  - `gicc_hppir`
  - `gicc_ahppir`

## Selected Next Experiment

- add one bounded alternate CPU-interface pending readback:
  - read `GICC_AHPPIR`
  - print one `gtimer: ahppir ...` line in the existing timer probe
  - compare it with the existing `gtimer: hppir ...` result on both lanes

## Why This Is The Right Next Step

- it changes one visibility variable only
- it stays on the direct Pi 4 QEMU timer path proven by `bcm2838.c`
- it can distinguish “Pi 4 `hppir 0` is an alias/security-view artifact” from
  “no alternate CPU-interface pending view exists either”

## Selected Next Step

- implement the bounded Pi 4 GIC CPU-interface alias probe and validate it on
  the Pi 4 A72 QEMU lane, with a generic build and runtime guardrail
