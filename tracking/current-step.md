# Current Step

## Metadata

- Step ID: `STEP-0185`
- Title: Validate the `aarch64a72-generic-rpi4b` Pi 4 QEMU lane
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- validate the new A72-capable Pi 4 target at runtime in QEMU before making further code changes

## Scope

In scope:

- rebuild `TARGET=aarch64a72-generic-rpi4b` with the official Pi 4 DTB input
- run the Pi 4 QEMU smoke lane against that image
- compare the earliest visible boundary with the current A53 diagnostic Pi 4 lane
- capture any new blocker precisely enough to justify the next narrow code step
- update manifests and docs with the result

Out of scope:

- runtime code changes in kernel or loader
- new target scaffolding
- changing Pi 4 image layout
- changing DTB content
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- Pi 4 A72 QEMU runtime evidence
- earliest visible boot boundary comparison against the A53 diagnostic lane
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the Pi 4 QEMU lane is executed successfully with the A72 image bundle
- the observed output is specific enough to locate the current earliest A72 runtime boundary
- the result is compared directly with the current A53 Pi 4 diagnostic lane
- the next step is narrowed to one concrete follow-up rather than a broad runtime rewrite

## Validation Plan

- Review:
  inspect the staged A72 Pi 4 boot bundle for the expected `kernel8.img`, `config.txt`, and DTB placement
- Build:
  - `LIBPHOENIX_DEVEL_MODE=n RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- Emulator:
  run `qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -smp 4 -m 2G` against the built A72 Pi 4 bundle with the official DTB
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-a72-runtime-validation-scope.md`

## Notes

- Risks:
  avoid mixing validation with runtime code changes; if the boundary is still too coarse, the next step should be one visibility patch only
- Dependencies:
  completed `STEP-0184` first A72 runtime validation scoping
- Architecture reminder:
  Raspberry Pi 4 Model B is based on BCM2711 with a quad-core Cortex-A72 CPU; treat `aarch64a53-generic-rpi4b` only as a temporary diagnostic lane and keep new target work centered on `aarch64a72-generic-rpi4b`
- User-visible control point before next step:
  after this step lands, the next bounded move should be one concrete runtime fix or visibility split, not a broad Pi 4 refactor
