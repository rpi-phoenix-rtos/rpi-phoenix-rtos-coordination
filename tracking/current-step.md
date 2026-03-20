# Current Step

## Metadata

- Step ID: `STEP-0205`
- Title: Scope the Pi 4 timer-group experiment
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest follow-up after the private-pending-state probe failed to
  explain the Pi 4 timer delivery gap

## Scope

In scope:

- review the current generic and Pi 4 timer registration differences
- identify one bounded timer-group follow-up
- keep the next move on the timer-to-GIC seam and out of broad interrupt
  redesign
- update manifests and docs with the chosen next move

Out of scope:

- implementation code
- scheduler or VM changes
- broad interrupt-controller redesign
- Pi 5 or RP1 work

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- generic and Pi 4 timer registration evidence
- current timer group-setting logic in `interrupts_gicv2.c`
- manifests and tracking updates for this planning step

## Acceptance Criteria

- one concrete timer-group experiment is selected
- that experiment stays on the current timer-to-GIC boundary
- the result is documented precisely enough that the next code change can start
  without reopening timer-source scope

## Validation Plan

- Review:
  inspect current runtime evidence and keep the next move limited to one
  timer-group follow-up
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-private-pending-readback.md`

## Notes

- Risks:
  do not widen the next move into broad interrupt work before one bounded
  timer-group experiment is selected
- Dependencies:
  completed `STEP-0204` private-pending-state readback
- Source reminder:
  official Raspberry Pi kernel DTS files on `rpi-6.19.y` and `rpi-7.0.y` are currently identical for Pi 4 and keep `memory@0` bootloader-filled plus `stdout-path` on `serial1` (aux UART); Raspberry Pi documentation also confirms that firmware applies overlays and `dtparam`s before handing the merged DTB to the OS; this step specifically targets the root memory-node cell layout, not UART alias handling
- Architecture reminder:
  Raspberry Pi 4 Model B is based on BCM2711 with a quad-core Cortex-A72 CPU; treat `aarch64a53-generic-rpi4b` only as a temporary diagnostic lane and keep new target work centered on `aarch64a72-generic-rpi4b`
- User-visible control point before next step:
  after this planning step lands, the next bounded move should be exactly one
  timer-group experiment based on the generic `grp 0` vs Pi 4 `grp 1` evidence
