# Current Step

## Metadata

- Step ID: `STEP-0055`
- Title: Implement first `plo`-side generic AArch64 scaffold
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add the first compile-oriented generic AArch64 `plo` scaffold needed before a real `aarch64a53-generic-qemu` project target can be introduced

## Scope

In scope:

- add the first generic AArch64 `plo` platform directory and minimal loader HAL files
- add the first generic AArch64 `plo` linker template
- keep the scaffold QEMU-`virt`-oriented and single-path, with minimal console, timer, and interrupt support
- validate the new generic `plo` target through a direct `make -C plo base_noimg` lane in `phoenix-dev`

Out of scope:

- `phoenix-rtos-project` target or run-script additions
- `phoenix-rtos-tests` target additions
- Raspberry Pi-specific code
- full DTB-driven loader discovery or storage support

## Expected Repositories

- `phoenix-rtos-plo`
- coordination repo

## Expected Files Or Subsystems

- `phoenix-rtos-plo/hal/aarch64/generic/`
- `phoenix-rtos-plo/ld/aarch64a53-generic.ldt`
- `docs/status.md`
- tracking files and manifest updates for this step

## Acceptance Criteria

- `aarch64a53-generic` can build `plo` directly with `make -C plo base_noimg` in the copied buildroot
- the first generic `plo` scaffold provides the minimal HAL, `_init`, timer, interrupt, console, and linker-template coverage needed by the loader
- the result stays compile-oriented and does not widen into `phoenix-rtos-project` or test-target work

## Validation Plan

- Review:
  compare the new generic AArch64 `plo` scaffold with the existing `zynqmp` and other minimal `plo` platforms
- Build:
  refresh the copied buildroot and run a direct `plo` generic build with a temporary empty `board_config.h` shim via `PROJECT_PATH`
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-plo-generic-scaffold-scope.md`

## Notes

- Risks:
  the first generic `plo` scaffold must stay compile-oriented and must not widen into a full generic QEMU project in the same patch
- Dependencies:
  completed planning step `STEP-0054`
- User-visible control point before next step:
  after this implementation step lands, the next slice should be the first `aarch64a53-generic-qemu` project entry-point step
