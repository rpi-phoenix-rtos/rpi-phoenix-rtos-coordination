# Current Step

## Metadata

- Step ID: `STEP-0165`
- Title: Scope Pi 4 loader user-script execution visibility
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest next diagnostic step that distinguishes whether the Pi 4 `raspi4b` lane now stalls before opening `user.plo`, while reading it, or while executing its first commands after the official firmware DTB replaced the old stub

## Scope

In scope:

- review the generated Pi 4 pre-init and `user.plo` scripts from the validated official-DTB build
- review the narrow loader paths that own that boundary:
  - `plo/cmds/call.c`
  - `plo/cmds/kernel.c`
  - `plo/phfs/phfs.c`
  - `phoenix-rtos-project/_targets/aarch64a53/generic/preinit.plo.yaml`
- select one bounded visibility step that will expose which phase of `call ram0 user.plo` blocks on the Pi 4 lane
- update manifests and docs with the scoped next step

Out of scope:

- loader or kernel code changes
- changing Pi 4 image layout
- changing DTB content or selection
- rerunning the generic `virt` lane unless the scope review proves it is needed
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `plo` loader call / script / PHFS path notes
- Pi 4 QEMU loader-script boundary notes
- manifests and tracking updates for this planning step

## Acceptance Criteria

- the reviewed loader paths are explicitly recorded
- the next implementation step is narrowed to one loader visibility change
- the scoped next step is specific enough to expose which phase of `call ram0 user.plo` blocks on Pi 4

## Validation Plan

- Review:
  inspect the generated Pi 4 scripts and the narrow loader call / file-read code paths
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-official-dtb-validation.md`

## Notes

- Risks:
  avoid widening into generic loader refactors before the Pi 4 `call` boundary is explicitly split
- Dependencies:
  completed `STEP-0164` official firmware DTB validation
- User-visible control point before next step:
  after this step lands, the next bounded move should be a single `plo` visibility patch in the `call` path rather than another broad Pi 4 experiment
