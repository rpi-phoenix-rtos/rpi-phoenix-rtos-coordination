# Current Step

## Metadata

- Step ID: `STEP-0201`
- Title: Read back the Pi 4 timer countdown after the first arm
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add one bounded first-arm countdown readback so the Pi 4 patched lane can be
  classified as either a dead timer or a counting timer that still never
  asserts

## Scope

In scope:

- keep the change diagnostic-only
- add one first-arm post-wait countdown readback in the existing timer trace
  path
- validate the generic `virt` guardrail lane and the Pi 4 A72 patched lane
- update manifests and docs with the result

Out of scope:

- scheduler or VM changes
- broad interrupt-controller changes
- permanent timer policy changes
- firmware-bundle or real-device work
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/hal/aarch64/gtimer_timer.c`
- timer arm, pending, and countdown readback logs on both lanes
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the generic lane remains healthy
- both lanes emit one bounded post-wait countdown readback after the first arm
- the Pi 4 result narrows the next step to one concrete follow-up before or at
  the timer-assert boundary

## Validation Plan

- Review:
  inspect the diagnostic change for minimality and keep it limited to one
  first-arm countdown readback
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
  `manifests/2026-03-20-aarch64-rpi4b-timer-countdown-scope.md`

## Notes

- Risks:
  do not widen this into active interrupt-controller or scheduler rework before
  the bounded countdown readback reports whether the timer is moving after the
  arm
- Dependencies:
  completed `STEP-0200` timer-countdown readback scoping
- Source reminder:
  official Raspberry Pi kernel DTS files on `rpi-6.19.y` and `rpi-7.0.y` are currently identical for Pi 4 and keep `memory@0` bootloader-filled plus `stdout-path` on `serial1` (aux UART); Raspberry Pi documentation also confirms that firmware applies overlays and `dtparam`s before handing the merged DTB to the OS; this step specifically targets the root memory-node cell layout, not UART alias handling
- Architecture reminder:
  Raspberry Pi 4 Model B is based on BCM2711 with a quad-core Cortex-A72 CPU; treat `aarch64a53-generic-rpi4b` only as a temporary diagnostic lane and keep new target work centered on `aarch64a72-generic-rpi4b`
- User-visible control point before next step:
  after this step lands, the next bounded move should be exactly one follow-up
  based on whether the timer countdown changed during the bounded post-arm
  window
