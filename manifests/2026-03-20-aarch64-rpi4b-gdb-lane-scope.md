# Manifest: Pi 4 QEMU GDB Lane Scope

- Date: `2026-03-20`
- Step: `STEP-0219`
- Status: `completed`

## Goal

- decide whether the current Pi 4 QEMU blocker should be investigated through
  another diagnostic print patch or through a debugger-assisted QEMU lane

## Evidence Reviewed

Tooling in `phoenix-dev`:

- `gdb-multiarch` was not installed initially
- `gdb-multiarch` `15.1` is now installed in the VM
- the current Pi 4 build contains symbolized ELFs:
  - `_build/aarch64a72-generic-rpi4b/prog/phoenix-aarch64a72-generic.elf`
  - `_build/aarch64a72-generic-rpi4b/prog/plo-aarch64a72-generic.elf`

Symbol checks:

- `phoenix-aarch64a72-generic.elf` resolves:
  - `_hal_interruptsInit`
  - `dtb_getGIC`
  - `interrupts_getHighestPending`
- `plo-aarch64a72-generic.elf` resolves:
  - `hal_cpuJump`
  - `hal_exitToEL1`

External documentation:

- official QEMU system-emulation GDB docs describe the gdbstub workflow with
  `-S` and `-gdb`
- official GDB remote-debug docs confirm the standard `target remote` flow

## Decision

- adopt a debugger-assisted QEMU lane for the current Pi 4 blocker
- keep it narrowly scoped:
  - launch the current Pi 4 `raspi4b` QEMU lane paused under the gdbstub
  - connect with `gdb-multiarch`
  - use the symbolized kernel ELF
  - inspect the GIC distributor and CPU-interface bases plus the current
    `GICC_HPPIR` / `GICC_AHPPIR` state at the live stall boundary

## Why This Is The Right Next Step

- it can inspect multiple runtime values in one run without another code patch
- the current blocker is already deep enough that symbolized kernel debugging
  is practical
- the immediate question is observational:
  whether Phoenix is using the intended GIC bases and what the live CPU
  interface state actually looks like on Pi 4

## Selected Next Step

- establish the bounded Pi 4 `raspi4b` QEMU gdbstub workflow and use it for the
  first live GIC-base and CPU-interface inspection
