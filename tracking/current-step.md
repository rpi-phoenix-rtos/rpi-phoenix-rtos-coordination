# Current Step

## Metadata

- Step ID: `STEP-0127`
- Title: Implement bounded `create_dev()` diagnostics in the shared registration path
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the smallest shared diagnostics that can show exactly where `create_dev("/dev/tty0")` blocks on both QEMU lanes

## Scope

In scope:

- `sources/libphoenix/unistd/file.c`
- add minimal diagnostics in `create_dev()`
- distinguish:
  - `devfs` lookup retry progress
  - directory/create message progress
  - final device-node create progress
- keep the patch reviewable and bounded
- validate on both the generic and Pi 4 DTB-backed QEMU lanes

Out of scope:

- broad Pi 4 peripheral-debug work
- new board-specific DTB work
- broad init-sequencing redesign across loader scripts and services
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration
- speculative functional fixes beyond the diagnostics themselves

## Expected Repositories

- `sources/libphoenix`
- coordination repo

## Expected Files Or Subsystems

- `sources/libphoenix/unistd/file.c`
- relevant generic and Pi 4 QEMU smoke notes
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the next QEMU run produces at least one new `create_dev()`-side marker after `pl011-tty: register tty0`
- the markers distinguish whether the current boundary is in lookup retry or in the create-message path
- neither lane regresses from current known-good startup output

## Validation Plan

- Review:
  confirm that the patch stays local to `create_dev()` and only adds diagnostics
- Build:
  rebuild the affected device and project lanes in `phoenix-dev`
- Emulator:
  rerun:
  - generic `virt`
  - Pi 4 DTB-backed `raspi4b`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-create-dev-diagnostic-scope.md`

## Notes

- Risks:
  avoid turning shared libc code into a large permanent debug scaffold
- Dependencies:
  completed `STEP-0126`
- User-visible control point before next step:
  after this step lands, the next bounded move should come from concrete new console output on the two QEMU lanes
