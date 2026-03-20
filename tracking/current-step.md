# Current Step

## Metadata

- Step ID: `STEP-0197`
- Title: Force the Pi 4 patched lane to the physical timer
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- run the smallest code experiment after the automated Pi 4 QEMU DTB memory
  hook by forcing the Pi 4 patched lane from the current virtual timer choice
  to the non-secure physical timer

## Scope

In scope:

- keep the experiment limited to timer-source selection
- prefer a bounded Pi 4 A72 diagnostic override over a permanent common-policy
  change on the first try
- validate the automated Pi 4 QEMU-patched lane after the source change
- update manifests and docs with the result

Out of scope:

- broader timer redesign
- GIC redesign
- scheduler or VM changes
- firmware-bundle or real-device work
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/hal/aarch64/dtb.c`
- timer-source selection policy or its bounded diagnostic override
- Pi 4 A72 patched-lane runtime evidence after the source change
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the generic lane remains healthy
- the automated Pi 4 patched lane clearly reports either:
  - resumed timer dispatch or tty wakeup progress
  - or a negative result that rules out timer-source choice as the blocker
- the result narrows the next step to one concrete interrupt-delivery or
  wakeup follow-up

## Validation Plan

- Review:
  inspect the timer-source change for minimality and keep it limited to this
  one bounded experiment
- Build:
  - `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  - run the generic `virt` fast lane
  - run the automated Pi 4 A72 `raspi4b` lane
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-post-dummyfs-timer-scope.md`

## Notes

- Risks:
  do not widen this into a permanent timer-policy rewrite before the bounded Pi
  4 experiment proves it is necessary
- Dependencies:
  completed `STEP-0196` timer follow-up scoping
- Source reminder:
  official Raspberry Pi kernel DTS files on `rpi-6.19.y` and `rpi-7.0.y` are currently identical for Pi 4 and keep `memory@0` bootloader-filled plus `stdout-path` on `serial1` (aux UART); Raspberry Pi documentation also confirms that firmware applies overlays and `dtparam`s before handing the merged DTB to the OS; this step specifically targets the root memory-node cell layout, not UART alias handling
- Architecture reminder:
  Raspberry Pi 4 Model B is based on BCM2711 with a quad-core Cortex-A72 CPU; treat `aarch64a53-generic-rpi4b` only as a temporary diagnostic lane and keep new target work centered on `aarch64a72-generic-rpi4b`
- User-visible control point before next step:
  after this step lands, the next bounded move should be exactly one follow-up
  based on whether the physical-timer experiment restores dispatch
