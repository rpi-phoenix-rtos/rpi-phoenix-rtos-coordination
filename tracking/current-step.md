# Current Step

## Metadata

- Step ID: `STEP-0198`
- Title: Scope the Pi 4 timer-IRQ pending-state experiment
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- choose the smallest follow-up after the negative physical-timer experiment,
  with focus on whether the selected timer IRQ ever becomes pending in the GIC
  on the Pi 4 patched lane

## Scope

In scope:

- review the current post-`dummyfs` evidence after both timer-source attempts
- identify the single highest-signal next interrupt-delivery experiment
- keep the focus on GIC pending or dispatch state, not broader scheduler work
- update manifests and docs with the chosen next move

Out of scope:

- new implementation code
- scheduler or VM changes
- broad GIC redesign
- firmware-bundle or real-device work
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- Pi 4 A72 patched-lane evidence after both virtual and physical timer-source
  attempts
- current GIC registration and no-dispatch diagnostics
- manifests and tracking updates for this planning step

## Acceptance Criteria

- one concrete next interrupt-delivery experiment is selected
- that experiment stays inside the current Pi 4 patched-lane GIC pending or
  dispatch boundary
- the result is documented precisely enough that the next code change can start
  without reopening timer-source scope

## Validation Plan

- Review:
  inspect the current runtime evidence and keep the next move limited to one
  GIC pending or dispatch follow-up
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-physical-timer-experiment.md`

## Notes

- Risks:
  do not widen the next move into a general interrupt-controller rewrite before
  one bounded pending-state experiment is selected
- Dependencies:
  completed `STEP-0197` physical-timer experiment
- Source reminder:
  official Raspberry Pi kernel DTS files on `rpi-6.19.y` and `rpi-7.0.y` are currently identical for Pi 4 and keep `memory@0` bootloader-filled plus `stdout-path` on `serial1` (aux UART); Raspberry Pi documentation also confirms that firmware applies overlays and `dtparam`s before handing the merged DTB to the OS; this step specifically targets the root memory-node cell layout, not UART alias handling
- Architecture reminder:
  Raspberry Pi 4 Model B is based on BCM2711 with a quad-core Cortex-A72 CPU; treat `aarch64a53-generic-rpi4b` only as a temporary diagnostic lane and keep new target work centered on `aarch64a72-generic-rpi4b`
- User-visible control point before next step:
  after this planning step lands, the next bounded move should be exactly one
  code experiment around Pi 4 timer-IRQ pending or dispatch state
