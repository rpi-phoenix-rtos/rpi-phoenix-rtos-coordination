# Current Step

## Metadata

- Step ID: `STEP-0065`
- Title: Mount `ram0` PHFS in generic pre-init and rerun smoke command
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- apply the smallest generic pre-init change needed to let `plo` open `user.plo` from the RAM-backed loader image and rerun the smoke command

## Scope

In scope:

- add `phfs ram0 4.0 raw` to the generic AArch64 pre-init script
- refresh the copied buildroot as needed
- rerun `timeout 10s ./scripts/aarch64a53-generic-qemu.sh` in `phoenix-dev`
- record the earliest post-fix result

Out of scope:

- broader NVM layout changes
- `plo` code changes
- `phoenix-rtos-tests` target additions
- Raspberry Pi-specific code
- fixing any later runtime issue beyond documenting it

## Expected Repositories

- coordination repo
- `phoenix-rtos-project`
- `plo`

## Expected Files Or Subsystems

- `phoenix-rtos-project/_targets/aarch64a53/generic/preinit.plo.yaml`
- `phoenix-rtos-project/_targets/aarch64a53/generic/user.plo.yaml`
- `docs/status.md`
- tracking files and manifest updates for this step
- smoke output captured from the copied buildroot in `phoenix-dev`

## Acceptance Criteria

- the generic pre-init mounts `ram0` as raw PHFS before calling `user.plo`
- the unchanged smoke command is rerun successfully
- the result records whether `plo` now opens `user.plo` or what the next earliest runtime failure is

## Validation Plan

- Review:
  inspect the generic pre-init and comparable RAM-backed PHFS target patterns as needed during result analysis
- Build:
  refresh the copied buildroot if needed
- Emulator:
  run `timeout 10s ./scripts/aarch64a53-generic-qemu.sh` inside the copied buildroot in `phoenix-dev`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-qemu-phfs-fix-scope.md`

## Notes

- Risks:
  the result must stay as one pre-init PHFS setup change plus one rerun and must not silently turn into broader generic bring-up
- Dependencies:
  completed implementation step `STEP-0064`
- User-visible control point before next step:
  after this rerun lands, the next slice should be the smallest runtime-fix step implied by the earliest observed post-PHFS result
