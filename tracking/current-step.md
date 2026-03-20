# Current Step

## Metadata

- Step ID: `STEP-0200`
- Title: Scope the Pi 4 timer-countdown readback experiment
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest follow-up after the negative pending-state result, with
  focus on whether the Pi 4 timer is actually counting down to expiry

## Scope

In scope:

- review the first-arm pending-state result on both lanes
- identify one bounded timer-countdown readback experiment
- keep the next move inside timer-countdown behavior, not broad interrupt work
- update manifests and docs with the chosen next move

Out of scope:

- new implementation code
- scheduler or VM changes
- broad interrupt-controller changes
- firmware-bundle or real-device work
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- Pi 4 A72 patched-lane evidence after the first-arm pending probe
- timer arm and pending probe results on both lanes
- manifests and tracking updates for this planning step

## Acceptance Criteria

- one concrete timer-countdown readback experiment is selected
- that experiment stays inside the current timer-before-pending boundary
- the result is documented precisely enough that the next code change can start
  without reopening GIC pending-state scope

## Validation Plan

- Review:
  inspect the current runtime evidence and keep the next move limited to one
  timer-countdown follow-up
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-timer-pending-probe.md`

## Notes

- Risks:
  do not widen the next move into broad interrupt work before one bounded
  countdown readback experiment is selected
- Dependencies:
  completed `STEP-0199` timer pending-state probe
- Source reminder:
  official Raspberry Pi kernel DTS files on `rpi-6.19.y` and `rpi-7.0.y` are currently identical for Pi 4 and keep `memory@0` bootloader-filled plus `stdout-path` on `serial1` (aux UART); Raspberry Pi documentation also confirms that firmware applies overlays and `dtparam`s before handing the merged DTB to the OS; this step specifically targets the root memory-node cell layout, not UART alias handling
- Architecture reminder:
  Raspberry Pi 4 Model B is based on BCM2711 with a quad-core Cortex-A72 CPU; treat `aarch64a53-generic-rpi4b` only as a temporary diagnostic lane and keep new target work centered on `aarch64a72-generic-rpi4b`
- User-visible control point before next step:
  after this planning step lands, the next bounded move should be exactly one
  code experiment around Pi 4 timer countdown behavior before the GIC pending
  boundary
