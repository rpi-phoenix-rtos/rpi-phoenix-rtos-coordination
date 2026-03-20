# Manifest: Generic PLO Multi-EL Entry Scope

- Date: `2026-03-20`
- Step: `STEP-0104`
- Status: `completed`

## Goal

- define the smallest useful generic AArch64 `plo` step that removes the current EL3-only entry assumption from the Pi 4 boot path

## Source Findings

From `sources/plo/hal/aarch64/generic/_init.S`:

- `_start` reads `CurrentEL`, compares it only against `0xc`, and branches to `error_loop` for any non-EL3 entry
- `hal_exitToEL1` is also EL3-only because it always programs `ELR_EL3`, `SPSR_EL3`, and `SCR_EL3` before `eret`

From `sources/plo/hal/aarch64/generic/hal.c`:

- the generic loader path does not use the EL3-only MMU helper layer during normal startup
- `hal_cpuJump()` always reaches the assembly `hal_exitToEL1()` path once the loader wants to transfer control to the kernel

## Emulator Findings

Using the same copied-buildroot generic QEMU image:

- `virt,secure=on,gic-version=2`
  - known-good baseline
  - shows loader banner, kernel banner, and `pl011-tty: started`
- `virt,secure=off,gic-version=2`
  - QEMU CPU reset log reports `PSTATE=... EL1h`
  - current loader stays silent and never reaches the first banner
- `virt,secure=off,virtualization=on,gic-version=2`
  - QEMU CPU reset log reports `PSTATE=... EL2h`
  - current loader stays silent and never reaches the first banner

## Selected Next Step

- implement one loader-local patch in `sources/plo/hal/aarch64/generic/_init.S`
- make generic `plo` accept EL1, EL2, and EL3 entry
- preserve the current EL3 behavior
- add the matching `hal_exitToEL1` handling for:
  - EL3 -> EL1 by `eret`
  - EL2 -> EL1 by `eret`
  - EL1 -> kernel by direct branch with the same register ABI

## Planned Validation For The Next Step

- build:
  - `TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh host core project image`
  - `TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh host core project image`
- emulator:
  - `virt,secure=on`
  - `virt,secure=off`
  - `virt,secure=off,virtualization=on`
- success signal:
  - all three modes show visible loader output and kernel handoff output instead of hanging before the first banner
