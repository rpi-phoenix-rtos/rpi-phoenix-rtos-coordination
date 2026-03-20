# Current Step

## Metadata

- Step ID: `STEP-0070`
- Title: Define first generic userspace build-unblock step after kernel banner milestone
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- define the smallest repo-local change that starts removing the temporary generic-QEMU build-lane workarounds after the first visible kernel-banner milestone

## Scope

In scope:

- inspect the remaining generic-target build blockers in `libphoenix`, `phoenix-rtos-filesystems`, `phoenix-rtos-devices`, and `phoenix-rtos-utils`
- choose the smallest first repo to unblock without dragging in board-specific device assumptions
- keep the step planning-only and stop before code changes

Out of scope:

- broad generic userspace enablement across multiple repos
- Pi 4 board-specific code
- implementation code in this planning step
- Raspberry Pi-specific code
- `phoenix-rtos-tests` target additions

## Expected Repositories

- coordination repo
- `phoenix-rtos-project`
- `plo`

## Expected Files Or Subsystems

- `libphoenix/arch/aarch64/reboot.c`
- `phoenix-rtos-filesystems/_targets/`
- `phoenix-rtos-devices/_targets/`
- `phoenix-rtos-utils/_targets/`
- `docs/status.md`
- tracking files and manifest updates for this step
- build-target inventory findings captured from the working tree

## Acceptance Criteria

- the result names the smallest first repo-local generic build-unblock step to apply next
- the result explains why that repo is preferred over the other current blockers
- the step remains planning-only

## Validation Plan

- Review:
  inspect the remaining generic-target blockers and the contents of the existing `aarch64a53-zynqmp` target makefiles
- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-qemu-stdout-path-fix.md`

## Notes

- Risks:
  the result must stay as one build-unblock planning step and must not silently turn into multi-repo implementation work
- Dependencies:
  completed implementation step `STEP-0069`
- User-visible control point before next step:
  after this planning step lands, the next slice should be the selected first repo-local generic userspace build-unblock change
