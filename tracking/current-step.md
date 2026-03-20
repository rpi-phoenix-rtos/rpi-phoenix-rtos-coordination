# Current Step

## Metadata

- Step ID: `STEP-0168`
- Title: Implement filtered post-`go!` Pi 4 visibility
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add narrowly filtered `plo` `go!` visibility so the Pi 4 official-DTB lane shows whether it blocks in `devs_done()`, in `hal_done()`, in `hal_cpuJump()`, or only after the jump attempt

## Scope

In scope:

- change only `plo/cmds/go.c`
- add raw `go:` markers for:
  - entry to `cmd_go()`
  - after `devs_done()`
  - after `hal_done()`
  - immediately before `hal_cpuJump()`
  - unexpected return from `hal_cpuJump()`
- rebuild and rerun:
  - generic `virt`
  - Pi 4 DTB-backed `raspi4b` with the official firmware DTB
- update manifests and docs with the exact new boundary

Out of scope:

- `plo/hal/aarch64/generic/hal.c`
- changing Pi 4 image layout
- changing DTB content or selection
- kernel-side changes
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- `plo`
- coordination repo

## Expected Files Or Subsystems

- `plo/cmds/go.c`
- Pi 4 QEMU handoff boundary notes
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the only upstream source change is in `plo/cmds/go.c`
- the generic `virt` lane still reaches the kernel banner after the new `go:` markers
- the Pi 4 lane now exposes a narrower post-`go!` boundary

## Validation Plan

- Review:
  confirm the markers are tightly filtered and limited to `cmd_go()`
- Build:
  rebuild:
  - `TARGET=aarch64a53-generic-qemu`
  - `RPI4B_DTB_PATH=$HOME/external/raspberrypi-firmware/boot/bcm2711-rpi-4-b.dtb TARGET=aarch64a53-generic-rpi4b`
- Emulator:
  rerun:
  - generic `virt`
  - Pi 4 DTB-backed `raspi4b`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-rpi4b-call-visibility.md`

## Notes

- Risks:
  avoid turning this into a broad loader / HAL trace instead of a `cmd_go()`-only split
- Dependencies:
  completed `STEP-0167` post-`go!` scope decision
- User-visible control point before next step:
  after this step lands, the next bounded move should come from the exact `go:` marker boundary, not from speculation about EL handoff internals
