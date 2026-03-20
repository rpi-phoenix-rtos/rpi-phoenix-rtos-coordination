# Current Step

## Metadata

- Step ID: `STEP-0061`
- Title: Make generic QEMU launcher executable and rerun smoke command
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- apply the smallest launcher fix needed to let the generic QEMU smoke lane start QEMU and rerun the unchanged smoke command

## Scope

In scope:

- make `scripts/aarch64a53-generic-qemu.sh` executable in `phoenix-rtos-project`
- refresh the copied buildroot as needed
- rerun `timeout 10s ./scripts/aarch64a53-generic-qemu.sh` in `phoenix-dev`
- record the earliest post-fix result

Out of scope:

- broader launcher content changes
- `phoenix-rtos-tests` target additions
- Raspberry Pi-specific code
- fixing any new QEMU or boot-path failure beyond documenting it

## Expected Repositories

- `phoenix-rtos-project`
- coordination repo

## Expected Files Or Subsystems

- `phoenix-rtos-project/_targets/aarch64a53/generic-qemu/`
- `phoenix-rtos-project/_projects/aarch64a53-generic-qemu/`
- `phoenix-rtos-project/scripts/aarch64a53-generic-qemu.sh`
- `docs/status.md`
- tracking files and manifest updates for this step
- smoke output captured from the copied buildroot in `phoenix-dev`

## Acceptance Criteria

- the generic launcher is tracked as executable in `phoenix-rtos-project`
- the unchanged smoke command runs far enough to start QEMU
- the result records the next earliest runtime outcome after the launcher-mode fix

## Validation Plan

- Review:
  inspect the launcher mode and comparable existing QEMU launch scripts
- Build:
  refresh the copied buildroot if needed
- Emulator:
  run `timeout 10s ./scripts/aarch64a53-generic-qemu.sh` inside the copied buildroot in `phoenix-dev`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-qemu-launcher-fix-scope.md`

## Notes

- Risks:
  the result must stay as one launcher-mode fix plus one rerun and must not silently turn into broader QEMU or boot-path debugging
- Dependencies:
  completed implementation step `STEP-0060`
- User-visible control point before next step:
  after this rerun lands, the next slice should be the smallest runtime-fix step implied by the earliest observed post-launcher result
