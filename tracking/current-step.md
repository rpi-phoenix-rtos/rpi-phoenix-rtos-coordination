# Current Step

## Metadata

- Step ID: `STEP-0175`
- Title: Keep generic secondary cores parked across kernel handoff
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- keep non-boot CPUs trapped in the generic AArch64 loader during the kernel handoff so the current single-core generic target is no longer handed multiple loader CPUs on `-smp 4` lanes

## Scope

In scope:

- change only the generic AArch64 loader secondary-core trap path in:
  - `plo/hal/aarch64/generic/_init.S`
- keep the current core-0 handoff path intact
- preserve current generic `virt` single-core behavior
- validate the changed handoff behavior on:
  - generic `virt -smp 4`
  - Pi 4 `raspi4b -smp 4` with the official firmware DTB
- update manifests and docs with the result

Out of scope:

- changes outside the generic loader secondary-core path
- changing Pi 4 image layout
- changing DTB content or selection
- semantic EL-handoff changes for core 0
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- `plo`
- coordination repo

## Expected Files Or Subsystems

- generic loader secondary-core trap / handoff path
- generic `virt -smp 4` post-handoff behavior
- Pi 4 `raspi4b -smp 4` post-handoff behavior
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the generic loader no longer releases trapped non-boot CPUs into `hal_exitToEL1()`
- generic `virt -smp 4` still reaches the kernel banner after the change
- Pi 4 `raspi4b -smp 4` is rerun after the change and its new boundary is documented
- the result is specific enough to justify the next smallest follow-up step

## Validation Plan

- Review:
  inspect the generic kernel CPU-count assumption and the generic loader secondary-core release path
- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - generic `virt` with `-smp 4`
  - Pi 4 `raspi4b` with `-smp 4`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-generic-single-core-handoff-scope.md`

## Notes

- Risks:
  keep the change narrowly limited to the current generic single-core target assumptions and avoid widening into a generic SMP design
- Dependencies:
  completed `STEP-0174` single-core handoff experiment scoping
- User-visible control point before next step:
  after this step lands, the next bounded move should be justified either as a Pi 4 kernel-entry visibility step or as the first Pi 4 MMIO / DT handoff follow-up, based on the observed `raspi4b` result
