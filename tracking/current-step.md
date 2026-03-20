# Current Step

## Metadata

- Step ID: `STEP-0216`
- Title: Scope the Pi 4 GIC CPU-interface alias probe
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- select the smallest next follow-up that can explain why Pi 4 QEMU reports
  `gtimer: hppir 0` while the timer still never dispatches

## Scope

In scope:

- review the completed `GICC_HPPIR` result on both lanes
- decide whether the next one-variable probe should be `GICC_AHPPIR` or an
  equally narrow CPU-interface/security-view readback
- keep the selected follow-up limited to one additional GIC CPU-interface
  observation

Out of scope:

- implementing the next probe in this planning step
- new timer-programming changes
- broad interrupt-controller redesign
- Pi 5 or RP1 work

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- completed `GICC_HPPIR` probe evidence
- Pi 4 QEMU `bcm2838.c` timer wiring notes
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the next runtime hypothesis is narrowed to one concrete CPU-interface
  follow-up
- the selected follow-up names the exact register or view to probe
- no implementation work is mixed into this planning step

## Validation Plan

- Review:
  compare the generic `hppir 1023` result with the Pi 4 `hppir 0` result and
  select the smallest explanatory follow-up
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-gic-cpu-interface.md`

## Notes

- Risks:
  do not widen this into interrupt-policy changes before another read-only
  CPU-interface split is tried
- Dependencies:
  completed `STEP-0215` Pi 4 GIC CPU-interface pending probe
- Source reminder:
  local QEMU `10.2.2` source proves that `raspi4b` wires `GTIMER_PHYS`
  directly to GIC PPI 14 in `hw/arm/bcm2838.c`
- User-visible control point before next step:
  after this scope lands, the next bounded move should add only one more
  CPU-interface or alias-view readback
