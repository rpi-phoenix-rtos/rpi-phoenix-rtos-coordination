# Manifest: AArch64 Generic Kernel Scaffold Scope

- Date: `2026-03-20`
- Step: `STEP-0050`
- Result: `completed`

## Scope

- inspect the AArch64 kernel platform surface required for a new generic subfamily
- choose the smallest file set that should make `aarch64a53-generic` compile as a kernel target
- define the validation command for that scaffold

## Findings

- the AArch64 kernel platform currently expects platform-provided implementations of:
  `_hal_platformInit`, `_hal_cpuInit`, `hal_cpuGetCount`, `hal_cpuReboot`, `hal_wdgReload`, `hal_platformctl`, and `hal_cpuSmpSync`
- the AArch64 kernel console is also platform-provided, so a new generic subfamily needs at least a temporary console implementation
- the platform include surface is driven by `config.h`, which in turn needs a matching `platformctl_t` header and `hal_syspage_t` definition
- the common AArch64 timer file already exists, so the generic platform can select it as the default timer implementation instead of introducing another timer backend

## Selected File Set

- `phoenix-rtos-kernel/hal/aarch64/generic/Makefile`
- `phoenix-rtos-kernel/hal/aarch64/generic/config.h`
- `phoenix-rtos-kernel/hal/aarch64/generic/generic.c`
- `phoenix-rtos-kernel/hal/aarch64/generic/console.c`
- `phoenix-rtos-kernel/include/arch/aarch64/generic/generic.h`
- `phoenix-rtos-kernel/include/arch/aarch64/generic/syspage.h`

## Selected Behavior

- use `interrupts_gicv2.o` for the generic platform
- select `hal/aarch64/gtimer_timer.o` as the default timer implementation for the generic platform
- keep the first generic console as a temporary no-output stub
- keep platform control minimal, with no attempt yet to support real reboot or peripheral control
- keep the first generic CPU-count model simple enough for a compile target, not a full runtime model

## Selected Validation Lane

- in `phoenix-dev` with the Phoenix AArch64 toolchain on `PATH`:
  `TARGET=aarch64a53-generic make -C phoenix-rtos-kernel clean all`

## Why This Was Selected

- it is the smallest coherent kernel-only step that turns the new build-layer target into a compilable kernel target
- it avoids widening prematurely into PL011, `virt`, `plo`, or `phoenix-rtos-project`
- it reuses the common timer work already completed instead of inventing another temporary timer path

## Selected Next Step

- implement the first kernel-side generic platform scaffold in `phoenix-rtos-kernel`
