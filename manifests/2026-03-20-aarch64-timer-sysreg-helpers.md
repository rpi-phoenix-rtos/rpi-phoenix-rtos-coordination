# Manifest: AArch64 Timer Sysreg Helpers Step

- Date: `2026-03-20`
- Step: `STEP-0016`
- Result: `completed`

## Scope

- add a small reusable set of AArch64 architectural timer system-register helpers in `phoenix-rtos-kernel/hal/aarch64/aarch64.h`
- keep the step header-only and preparatory
- validate that the existing `aarch64a53-zynqmp-qemu` build still succeeds in `phoenix-dev`

## Touched Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Upstream Commits

- `phoenix-rtos-kernel`: `e58d0577` (`aarch64: add architectural timer helpers`)

## Validation

- Refreshed the copied buildroot in `phoenix-dev`:
  `./scripts/prepare-buildroot.sh --copy-components`
- Rebuilt the existing AArch64 QEMU lane in `phoenix-dev`:
  `TARGET=aarch64a53-zynqmp-qemu ./phoenix-rtos-build/build.sh clean host core project`
- Build result: success

## Key Findings

- The current AArch64 tree now has reusable helpers for the first architectural timer registers needed by a future generic ARM timer backend.
- Combined with the previous DTB timer metadata step and timer IRQ HAL split, the kernel now has the minimum plumbing needed to start defining a common generic timer backend.
- The next timer step is no longer blocked by missing register-access helpers.

## Selected Next Step

- define the first common AArch64 generic timer backend step, including:
  - the first chosen timer source policy
  - exact touched files
  - whether the backend lands as a reusable helper or as a directly selectable timer implementation
