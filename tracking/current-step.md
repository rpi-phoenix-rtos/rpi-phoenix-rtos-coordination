# Current Step

## Metadata

- Step ID: `STEP-0241`
- Title: Implement the smallest root-dummyfs fast-lane image change
- Status: `planned`
- Date: `2026-03-21`
- Milestone / phase: `Phase 1`

## Objective

- make `/` exist on the generic and Pi 4 fast lanes with the smallest image
  definition change

## Scope

In scope:

- add a plain root `dummyfs` syspage app before the existing `devfs` app on the
  fast lanes
- rebuild generic and Pi 4 images
- verify that the root-lookup blocker moves forward

Out of scope:

- unrelated project restructuring
- Pi 5 or RP1 work
- real hardware work

## Expected Repositories

- `phoenix-rtos-project`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-qemu/user.plo.yaml`
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`
- `docs/status.md`
- `docs/testing-automation.md`
- `manifests/`
- `tracking/current-step.md`
- `tracking/step-history.md`

## Acceptance Criteria

- generic QEMU gets past the current root-lookup stall
- Pi 4 QEMU is rerun too if the change stays in the shared fast lane
- the result narrows the next move to one concrete follow-up

## Validation Plan

- Analysis only:
  not applicable
- Emulator:
  - rebuild generic `virt`
  - rerun generic QEMU
  - rerun Pi 4 QEMU if the result remains shared
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-21-aarch64-rootfs-gap-scope.md`

## Notes

- Risks:
  keep the change limited to the fast-lane image definition and avoid mixing it
  with unrelated `psh` or kernel fixes
- Dependencies:
  completed `STEP-0240` shared `lookup()`-contract review
- Source reminder:
  both lanes currently start `devfs` but no root dummyfs instance
- User-visible control point before next step:
  after this step lands, the next follow-up should depend on how far the boot
  advances once `/` exists
