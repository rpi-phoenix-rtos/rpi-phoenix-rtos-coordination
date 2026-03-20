# Current Step

## Metadata

- Step ID: `STEP-0120`
- Title: Validate the Pi 4 QEMU lane with an explicit DTB passed through `RPI4B_DTB_PATH`
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- prove or reject DTB-backed kernel handoff as the next Pi 4 QEMU blocker by rebuilding the current Pi 4 project with an explicit DTB and rerunning the `plo.elf` smoke

## Scope

In scope:

- generate a DTB from the current `raspi4b` QEMU model
- rebuild `aarch64a53-generic-rpi4b` with `RPI4B_DTB_PATH` pointing to that DTB
- rerun the Pi 4 `plo.elf` QEMU smoke from the rebuilt artifacts
- document whether the lane advances to kernel output or a more specific next blocker

Out of scope:

- broad Pi 4 peripheral-debug work
- broad kernel DTB-parser changes unless the validation proves they are immediately required
- project-source changes unless the validation proves a tiny helper patch is strictly required
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration
- changing the approved Pi 4 `plo.elf` QEMU handoff shape

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- Pi 4 staged DTB and loader payload artifacts in the disposable buildroot
- relevant Pi 4 QEMU smoke notes
- manifests and tracking updates for this validation step

## Acceptance Criteria

- the Pi 4 validation build is rerun with an explicit DTB
- the Pi 4 `plo.elf` QEMU lane yields a more informative post-alias result than the current no-kernel-output boundary
- the next step is selected from DTB-backed runtime evidence rather than from artifact comparison alone

## Validation Plan

- Review:
  confirm that the rebuilt Pi 4 payload now includes `system.dtb`
- Build:
  rebuild `aarch64a53-generic-rpi4b` in `phoenix-dev` with `RPI4B_DTB_PATH`
- Emulator:
  rerun the Pi 4 `raspi4b` `plo.elf` QEMU smoke and compare the result against `STEP-0118`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-dtb-backed-qemu-scope.md`

## Notes

- Risks:
  keep this as a validation step first; do not widen into a large project-helper patch before the DTB-backed result is known
- Dependencies:
  completed `STEP-0119`
- User-visible control point before next step:
  after this step lands, the next bounded move should come from the DTB-backed Pi 4 QEMU result rather than from generic comparison alone
