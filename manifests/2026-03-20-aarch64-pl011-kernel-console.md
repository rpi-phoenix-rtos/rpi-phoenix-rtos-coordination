# Manifest: AArch64 PL011 Kernel Console

- Date: `2026-03-20`
- Step: `STEP-0053`
- Result: `completed`

## Scope

- add a reusable AArch64 PL011 helper layer
- replace the generic kernel console stub with a DTB-backed PL011 transmit path
- keep the first PL011 step transmit-only and assume preconfigured UART state

## Upstream Repositories

### `phoenix-rtos-kernel`

- Commit: `f90c424b`

## Files

- `phoenix-rtos-kernel/hal/aarch64/pl011.c`
- `phoenix-rtos-kernel/hal/aarch64/pl011.h`
- `phoenix-rtos-kernel/hal/aarch64/generic/Makefile`
- `phoenix-rtos-kernel/hal/aarch64/generic/console.c`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev` with:
  `./scripts/prepare-buildroot.sh --copy-components`
- validated the generic kernel target in the copied buildroot with the Phoenix AArch64 toolchain on `PATH`
- kept using a temporary empty `board_config.h` shim through `PROJECT_PATH` because a real generic project target still does not exist

## Validation Command

`TARGET=aarch64a53-generic PROJECT_PATH="$tmpdir" make -C phoenix-rtos-kernel all`

## Validation Evidence

- the new common `hal/aarch64/pl011.c` helper compiled successfully
- the generic console object compiled successfully after switching from the stub to the PL011 helper
- the generic kernel target still linked as `phoenix-aarch64a53-generic.elf`

## Notes

- this step does not yet initialize PL011 clocks, baud rate, or interrupts
- the current helper intentionally assumes the UART is already configured by the environment
- that assumption is acceptable for the current boot-first path because it minimizes risk before the first `virt` and Pi 4 boot lanes exist

## Selected Next Step

- define the first `plo`-side generic AArch64 scaffold required before a real `aarch64a53-generic-qemu` project target can exist
