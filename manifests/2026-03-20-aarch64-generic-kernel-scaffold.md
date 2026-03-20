# Manifest: AArch64 Generic Kernel Scaffold

- Date: `2026-03-20`
- Step: `STEP-0051`
- Result: `completed`

## Scope

- add the first generic AArch64 kernel platform directory
- add the matching generic AArch64 kernel include directory
- provide the minimal platform callbacks and a temporary stub console
- select the common AArch64 timer file as the default timer implementation for the generic platform

## Upstream Repositories

### `phoenix-rtos-kernel`

- Commit: `0abf2935`

## Files

- `phoenix-rtos-kernel/hal/aarch64/generic/Makefile`
- `phoenix-rtos-kernel/hal/aarch64/generic/config.h`
- `phoenix-rtos-kernel/hal/aarch64/generic/generic.c`
- `phoenix-rtos-kernel/hal/aarch64/generic/console.c`
- `phoenix-rtos-kernel/include/arch/aarch64/generic/generic.h`
- `phoenix-rtos-kernel/include/arch/aarch64/generic/syspage.h`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev` with:
  `./scripts/prepare-buildroot.sh --copy-components`
- validated the kernel target in the copied buildroot with the Phoenix AArch64 toolchain on `PATH`
- because there is not yet an `aarch64a53-generic-qemu` project directory, the validation used a temporary empty `board_config.h` shim via `PROJECT_PATH`

## Validation Command

`TARGET=aarch64a53-generic PROJECT_PATH="$tmpdir" make -C phoenix-rtos-kernel all`

## Validation Evidence

- the generic platform objects compiled and linked into `phoenix-aarch64a53-generic.elf`
- the generic platform now provides the missing startup/link symbols that were previously satisfied only by `zynqmp`
- the generic platform selects `hal/aarch64/gtimer_timer.o` as the default timer implementation
- the generic console remains a temporary stub and does not yet provide runtime UART output

## Notes

- the initial direct kernel-build attempt exposed two real generic-platform gaps:
  `ASID_BITS` in `config.h`, and platform definitions for `nCpusStarted` plus `_interrupts_gicv2_classify`
- the kernel build also still depends on project-provided `board_config.h`, so a real generic project target remains necessary before this lane can become fully self-contained

## Selected Next Step

- define the first reusable PL011 kernel-console step for the generic AArch64 path
