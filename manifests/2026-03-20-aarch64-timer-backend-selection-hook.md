# Manifest: AArch64 Timer-Backend Selection Hook

- Date: `2026-03-20`
- Step: `STEP-0022`
- Result: `completed`

## Scope

- add an explicit timer-backend object hook in the common AArch64 kernel Makefile
- move the current ZynqMP timer object selection behind that hook
- preserve the current runtime behavior and validate the existing `aarch64a53-zynqmp-qemu` build

## Touched Repositories

- `phoenix-rtos-kernel`
- coordination repo

## Upstream Commits

- `phoenix-rtos-kernel`: `4b7ac8cb` (`aarch64: add timer backend selection hook`)

## Validation

- Refreshed the copied buildroot in `phoenix-dev`:
  `./scripts/prepare-buildroot.sh --copy-components`
- Rebuilt the existing AArch64 QEMU lane in `phoenix-dev`:
  `TARGET=aarch64a53-zynqmp-qemu ./phoenix-rtos-build/build.sh clean host core project`
- Build result: success

## Key Findings

- The common AArch64 kernel Makefile now exposes a dedicated timer-backend object hook instead of hardwiring timer object selection entirely inside the platform Makefile.
- The current ZynqMP runtime path is preserved, because the platform still selects `hal/aarch64/zynqmp/timer.o` through the new hook.
- The next timer-related cleanup can focus on interrupt or runtime semantics rather than more build-glue reshuffling.

## Selected Next Step

- make the common AArch64 GICv2 handler-registration path stop applying CPU targeting to non-SPI interrupts so future PPI-backed timer handlers fit the interrupt layer cleanly
