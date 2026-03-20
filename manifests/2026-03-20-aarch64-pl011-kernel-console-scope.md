# Manifest: AArch64 PL011 Kernel Console Scope

- Date: `2026-03-20`
- Step: `STEP-0052`
- Result: `completed`

## Scope

- inspect the current AArch64 generic console stub and the available serial DTB data
- choose the first reusable PL011 kernel-console file set
- choose the validation lane for that step

## Findings

- the AArch64 DTB layer already exposes serial base addresses via `dtb_getSerials()`
- there is currently no PL011 implementation anywhere in the Phoenix source tree
- the generic kernel target already builds, so replacing the generic console stub is now the narrowest step that improves the path toward `virt` and Raspberry Pi 4 boot

## Selected File Set

- `phoenix-rtos-kernel/hal/aarch64/pl011.c`
- `phoenix-rtos-kernel/hal/aarch64/pl011.h`
- `phoenix-rtos-kernel/hal/aarch64/generic/Makefile`
- `phoenix-rtos-kernel/hal/aarch64/generic/console.c`

## Selected Behavior

- add one reusable AArch64 PL011 helper layer for MMIO mapping and transmit-only output
- switch the generic kernel console from a stub to the first DTB-selected serial port
- explicitly assume preconfigured UART state for this first step instead of introducing baud, clock, or firmware-specific setup logic yet

## Selected Validation Lane

- in `phoenix-dev`, refresh the VM-local copied buildroot and rebuild `phoenix-rtos-kernel` for `TARGET=aarch64a53-generic`
- keep using the temporary empty `board_config.h` shim through `PROJECT_PATH` until a real generic project target exists

## Why This Was Selected

- it creates a reusable console helper that later Pi 4 and `virt` work can share
- it stays inside `phoenix-rtos-kernel`
- it avoids widening into `plo`, `phoenix-rtos-project`, or full PL011 initialization policy before the first console path exists

## Selected Next Step

- implement the first reusable PL011 kernel-console path in `phoenix-rtos-kernel`
