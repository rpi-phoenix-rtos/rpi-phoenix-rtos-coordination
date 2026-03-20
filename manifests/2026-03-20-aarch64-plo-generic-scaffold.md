# Manifest: AArch64 `plo` Generic Scaffold

- Date: `2026-03-20`
- Step: `STEP-0055`
- Result: `completed`

## Scope

- add the first generic AArch64 `plo` platform directory
- add the matching generic AArch64 loader linker template
- keep the first loader scaffold QEMU-`virt`-oriented and boot-first rather than feature-complete

## Upstream Repositories

### `phoenix-rtos-plo`

- Commit: `f26ca3e`

## Files

- `phoenix-rtos-plo/hal/aarch64/generic/Makefile`
- `phoenix-rtos-plo/hal/aarch64/generic/_init.S`
- `phoenix-rtos-plo/hal/aarch64/generic/config.h`
- `phoenix-rtos-plo/hal/aarch64/generic/console.c`
- `phoenix-rtos-plo/hal/aarch64/generic/hal.c`
- `phoenix-rtos-plo/hal/aarch64/generic/interrupts.c`
- `phoenix-rtos-plo/hal/aarch64/generic/timer.c`
- `phoenix-rtos-plo/hal/aarch64/generic/types.h`
- `phoenix-rtos-plo/ld/aarch64a53-generic.ldt`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev` with:
  `./scripts/prepare-buildroot.sh --copy-components`
- validated the generic loader target in the copied buildroot with the Phoenix AArch64 toolchain on `PATH`
- kept using a temporary empty `board_config.h` shim through `PROJECT_PATH` so this lane remains aligned with the current generic direct-build validation setup

## Validation Command

`TARGET=aarch64a53-generic PROJECT_PATH="$tmpdir" make -C plo base_noimg`

## Validation Evidence

- the new generic loader objects compiled successfully under `hal/aarch64/generic/`
- the linker template generated and linked `plo-aarch64a53-generic.elf`
- the final stripped artifact was produced successfully in the copied buildroot

## Notes

- this first generic loader path is intentionally EL3-centric for QEMU `virt`; the next project/runtime step should use a QEMU configuration that enters the loader in EL3 instead of widening this patch into a multi-EL startup refactor
- the first generic console step assumes preconfigured PL011 state and only provides the polling transmit path
- the first generic timer step is polling-based from the architectural counter; it does not yet use the architectural timer interrupt path inside `plo`
- the first generic GIC step is present so the loader has a coherent interrupt-controller scaffold before runtime boot work starts, but the step is still optimized for the earliest generic QEMU boot lane rather than for full loader feature parity

## Selected Next Step

- define the first `aarch64a53-generic-qemu` project entry-point step
