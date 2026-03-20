# Current Step

## Metadata

- Step ID: `STEP-0067`
- Title: Generate generic QEMU `system.dtb`, load it in `user.plo`, and rerun smoke command
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- apply the smallest generic QEMU DTB handoff change needed to satisfy the AArch64 kernel early-init contract and rerun the smoke command

## Scope

In scope:

- generate `virt,secure=on,gic-version=2` DTB output into `${PREFIX_ROOTFS}/etc/system.dtb` during the generic QEMU project build
- add `blob {{ env.BOOT_DEVICE }} /etc/system.dtb ddr` to the generic AArch64 user script
- refresh the copied buildroot as needed
- rebuild the generic project/image artifacts in `phoenix-dev`
- rerun `timeout 10s ./scripts/aarch64a53-generic-qemu.sh` in `phoenix-dev`
- record the earliest post-fix result

Out of scope:

- broader DTB-passing redesign between `plo` and the kernel
- `plo` or kernel source changes
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
- `phoenix-rtos-project/_projects/aarch64a53-generic-qemu/build.project`
- `docs/status.md`
- tracking files and manifest updates for this step
- smoke output captured from the copied buildroot in `phoenix-dev`

## Acceptance Criteria

- the generic QEMU project produces `${PREFIX_ROOTFS}/etc/system.dtb` during the current project/image lane
- the generic user script loads `system.dtb` before `go!`
- the rerun records whether the kernel now reaches visible early output or what the next earliest runtime failure is

## Validation Plan

- Review:
  inspect the generic QEMU build path, AArch64 kernel DTB requirement, and current user-script handoff as needed during result analysis
- Build:
  rebuild the generic project/image artifacts in `phoenix-dev`
- Emulator:
  run `timeout 10s ./scripts/aarch64a53-generic-qemu.sh` inside the copied buildroot in `phoenix-dev`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-qemu-dtb-fix-scope.md`

## Notes

- Risks:
  the result must stay as one generic project-local DTB handoff fix plus one rerun and must not silently turn into broader kernel or loader redesign
- Dependencies:
  completed implementation step `STEP-0066`
- User-visible control point before next step:
  after this rerun lands, the next slice should be the smallest runtime-fix step implied by the earliest observed post-DTB result
