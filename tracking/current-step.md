# Current Step

## Metadata

- Step ID: `STEP-0136`
- Title: Instrument root-dummyfs lookup visibility
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- determine whether the blocked later `lookup("devfs", ...)` call is still waiting on the root dummyfs instance rather than on the later non-filesystem `devfs` instance

## Scope

In scope:

- `sources/phoenix-rtos-filesystems/dummyfs/srv.c`
- extend the existing bounded `dummyfs` marker set so it can distinguish:
  - the root dummyfs instance
  - the non-filesystem `devfs` instance
- add or relabel only the minimum markers needed around:
  - root-instance `initialized`
  - root-instance first `mtLookup` receive / response
  - preserved non-filesystem `devfs` startup markers
- keep the patch local, bounded, and diagnostic in nature
- validate on both the generic and Pi 4 DTB-backed QEMU lanes

Out of scope:

- broad `dummyfs` lifecycle refactoring
- loader-script or service-order changes
- broad `pl011-tty` redesign
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration
- broad refactoring of `create_dev()` semantics

## Expected Repositories

- `sources/phoenix-rtos-filesystems`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-filesystems/dummyfs/srv.c`
- relevant generic and Pi 4 QEMU smoke notes
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the generic lane exposes whether the blocked later `lookup("devfs", ...)` path reaches the root dummyfs instance
- the generic lane distinguishes root dummyfs activity from the already-observed later non-filesystem `devfs` startup
- neither lane regresses from current known-good startup output

## Validation Plan

- Review:
  confirm that the patch stays local to `dummyfs` visibility labels and does not change mount order or namespace policy
- Build:
  rebuild the affected filesystem and project lanes in `phoenix-dev`
- Emulator:
  rerun:
  - generic `virt`
  - Pi 4 DTB-backed `raspi4b`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-dummyfs-nonfs-startup-visibility.md`

## Notes

- Risks:
  avoid turning bounded visibility markers into long-lived logging churn
- Dependencies:
  completed `STEP-0134` non-filesystem `dummyfs` startup visibility result
- User-visible control point before next step:
  after this step lands, the next bounded move should come from concrete root-versus-devfs lookup evidence rather than more timing speculation
