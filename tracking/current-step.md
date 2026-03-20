# Current Step

## Metadata

- Step ID: `STEP-0138`
- Title: Instrument kernel name-service visibility for `devfs`
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- determine whether the blocked later `lookup("devfs", ...)` call switches from a no-root fast failure into a different kernel name-service path, such as a late `/` registration or a root-mediated lookup

## Scope

In scope:

- `sources/phoenix-rtos-kernel/proc/name.c`
- add narrow markers only for:
  - `/` registration state changes
  - `devfs` registration state changes
  - `lookup("devfs", ...)` branch selection / return path
- keep the markers tightly filtered so they do not turn into a broad kernel trace flood
- validate on both the generic and Pi 4 DTB-backed QEMU lanes

Out of scope:

- broad `dummyfs` lifecycle refactoring
- loader-script or service-order changes
- broad `pl011-tty` redesign
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration
- broad tracing in unrelated kernel paths
- broad refactoring of `create_dev()` semantics

## Expected Repositories

- `sources/phoenix-rtos-filesystems`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/proc/name.c`
- relevant generic and Pi 4 QEMU smoke notes
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the generic lane exposes whether `/` registration changes between the first and later `lookup("devfs", ...)` calls
- the generic lane exposes whether the later `lookup("devfs", ...)` call is still a no-root fast failure, a cached-`devfs` hit, or a root-mediated lookup
- neither lane regresses from current known-good startup output

## Validation Plan

- Review:
  confirm that the patch stays local to `proc/name.c` and only traces `/` and `devfs` state transitions
- Build:
  rebuild the affected kernel and project lanes in `phoenix-dev`
- Emulator:
  rerun:
  - generic `virt`
  - Pi 4 DTB-backed `raspi4b`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-dummyfs-root-lookup-visibility.md`

## Notes

- Risks:
  avoid turning bounded visibility markers into long-lived logging churn
- Dependencies:
  completed `STEP-0136` root-versus-devfs visibility result
- User-visible control point before next step:
  after this step lands, the next bounded move should come from concrete kernel name-service state rather than more dummyfs-side inference
