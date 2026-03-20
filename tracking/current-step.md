# Current Step

## Metadata

- Step ID: `STEP-0183`
- Title: Add the first buildable `aarch64a72-generic-rpi4b` scaffold
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the first buildable A72-capable Pi 4 target scaffold without widening into runtime behavior changes yet

## Scope

In scope:

- add the minimum target and project scaffolding in:
  - `phoenix-rtos-build/makes/include-target.mk`
  - `phoenix-rtos-project/_targets/aarch64a72/generic/*`
  - `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/*`
- preserve the current A53 generic lanes
- validate that `TARGET=aarch64a72-generic-rpi4b` builds with the current Pi 4 DTB input
- update manifests and docs with the result

Out of scope:

- runtime semantic changes in kernel or loader
- generic QEMU A72 project scaffolding
- renaming existing target directories or projects wholesale
- changing Pi 4 image layout
- changing DTB content or selection
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- `phoenix-rtos-project`
- `phoenix-rtos-build`
- coordination repo

## Expected Files Or Subsystems

- A72 target allowlist / target-family admission
- generic A72 target scaffold
- Pi 4 A72 project scaffold
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- `TARGET=aarch64a72-generic-rpi4b` builds successfully
- the existing `aarch64a53-generic-qemu` build still succeeds
- the existing `aarch64a53-generic-rpi4b` build still succeeds
- the result is specific enough to justify the first A72 runtime validation step

## Validation Plan

- Review:
  inspect the touched target allowlist and scaffold files for minimality
- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  not required for this scaffold step
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-a72-scaffold-scope.md`

## Notes

- Risks:
  keep the scaffold step purely structural and avoid slipping runtime fixes into it
- Dependencies:
  completed `STEP-0182` first buildable A72 scaffold scoping
- User-visible control point before next step:
  after this step lands, the next bounded move should be the first A72 runtime validation step
