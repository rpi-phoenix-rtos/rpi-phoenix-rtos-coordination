# Current Step

## Metadata

- Step ID: `STEP-0177`
- Title: Add earliest generic AArch64 kernel-entry visibility
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the smallest visibility-only marker that shows whether the Pi 4 lane reaches generic AArch64 kernel `_start` after the single `A3` handoff marker

## Scope

In scope:

- add only the minimum early-UART visibility glue in:
  - `phoenix-rtos-kernel/hal/aarch64/generic/config.h`
  - `phoenix-rtos-kernel/hal/aarch64/_init.S`
- reuse project `board_config.h` UART base values where available
- validate the new marker on:
  - generic `virt`
  - Pi 4 `raspi4b`
- update manifests and docs with the result

Out of scope:

- changing Pi 4 image layout
- changing DTB content or selection
- semantic kernel-init changes beyond visibility
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- generic AArch64 kernel earliest entry path
- board-overridable early UART visibility options
- generic `virt` and Pi 4 `raspi4b` post-`A3` kernel-entry notes
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the generic kernel emits the new earliest-entry marker on the working generic lane
- the Pi 4 lane is rerun and the presence or absence of the new marker is documented
- the result is specific enough to justify the next smallest follow-up step

## Validation Plan

- Review:
  inspect the touched kernel config and early init assembly for minimality
- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - generic `virt` with `-smp 1`
  - Pi 4 `raspi4b` with `-smp 4`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-kernel-entry-visibility-scope.md`

## Notes

- Risks:
  keep the change visibility-only and avoid coupling it to later DTB-driven console initialization
- Dependencies:
  completed `STEP-0176` earliest generic AArch64 kernel-entry visibility scoping
- User-visible control point before next step:
  after this step lands, the next bounded move should be chosen directly from whether the Pi 4 lane prints the earliest kernel-entry marker
