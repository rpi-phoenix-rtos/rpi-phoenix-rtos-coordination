# Manifest: AArch64 `plo` Generic Scaffold Scope

- Date: `2026-03-20`
- Step: `STEP-0054`
- Result: `completed`

## Scope

- inspect the current AArch64 `plo` surface and identify the smallest generic scaffold needed before a real `aarch64a53-generic-qemu` project target can exist
- keep the step compile-oriented and stop before implementing the new `plo` files
- select a direct validation lane that does not depend on real hardware or a full project target

## Findings

- `phoenix-rtos-project` is blocked on a generic AArch64 `plo` target because `build.project` expects target-local loader artifacts, and `plo` itself also needs a target-local linker template under `plo/ld/`
- the first generic AArch64 `plo` scaffold needs these files:
  - `phoenix-rtos-plo/hal/aarch64/generic/Makefile`
  - `phoenix-rtos-plo/hal/aarch64/generic/config.h`
  - `phoenix-rtos-plo/hal/aarch64/generic/_init.S`
  - `phoenix-rtos-plo/hal/aarch64/generic/hal.c`
  - `phoenix-rtos-plo/hal/aarch64/generic/console.c`
  - `phoenix-rtos-plo/hal/aarch64/generic/interrupts.c`
  - `phoenix-rtos-plo/hal/aarch64/generic/timer.c`
  - `phoenix-rtos-plo/ld/aarch64a53-generic.ldt`
- the initial scaffold should stay QEMU-`virt`-oriented: one RAM region, one MMIO region for GIC and PL011, one simple periodic timer path, and a direct EL jump path
- the first validation lane should be a direct copied-buildroot `plo` build in `phoenix-dev`, still using a temporary empty `board_config.h` shim via `PROJECT_PATH` until the real generic project target exists

## Validation Command

`TARGET=aarch64a53-generic PROJECT_PATH="$tmpdir" make -C plo base_noimg`

## Why `plo` Must Move Before The Generic Project Target

- `phoenix-rtos-project` cannot produce a real generic AArch64 boot image until `plo` has:
  - a matching AArch64 generic HAL platform directory
  - a matching target-local linker template
  - a buildable loader artifact name for `aarch64a53-generic`
- without those pieces, adding only a generic project directory would create target metadata with no loader binary behind it

## Selected Next Step

- implement the first generic AArch64 `plo` scaffold and validate it with a direct `make -C plo base_noimg` lane in `phoenix-dev`
