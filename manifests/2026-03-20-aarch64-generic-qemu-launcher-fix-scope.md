# Manifest: Generic AArch64 QEMU Launcher Fix Scope

- Date: `2026-03-20`
- Step: `STEP-0060`
- Result: `completed`

## Scope

- inspect the launcher-level failure from the first generic QEMU smoke run
- compare the new generic launcher with existing Phoenix QEMU launcher scripts
- choose the smallest fix that allows the smoke lane to reach QEMU startup

## Upstream Repositories

- none

## Findings

- `scripts/aarch64a53-generic-qemu.sh` is currently tracked as mode `100644`
- existing Phoenix QEMU launcher scripts such as:
  - `scripts/aarch64a53-zynqmp-qemu.sh`
  - `scripts/ia32-generic-qemu.sh`
  are tracked as mode `100755`

## Selected Fix

- change only the tracked mode of `scripts/aarch64a53-generic-qemu.sh` from non-executable to executable

## Notes

- this is preferred over changing the smoke command or launcher contents because the current failure happens before QEMU starts and comparable launchers in the same repo are already executable
- if the rerun still fails after this mode fix, the next step should record the earliest true QEMU or boot-path failure

## Selected Next Step

- set the generic QEMU launcher executable and rerun the unchanged smoke command
