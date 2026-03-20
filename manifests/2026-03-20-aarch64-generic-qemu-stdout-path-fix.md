# Manifest: Generic AArch64 Stdout-Path Console Selection Fix

- Date: `2026-03-20`
- Step: `STEP-0069`
- Result: `completed`

## Scope

- add a DTB helper that prefers `chosen.stdout-path` when it identifies a parsed PL011 serial node
- switch the generic AArch64 kernel console to use that helper
- rebuild the generic QEMU kernel plus project/image lane and rerun the smoke command
- confirm the shared DTB change still builds on the existing ZynqMP AArch64 lane

## Upstream Repositories

### `phoenix-rtos-kernel`

- Commit: `4abd44c0`

## Files

- `phoenix-rtos-kernel/hal/aarch64/dtb.h`
- `phoenix-rtos-kernel/hal/aarch64/dtb.c`
- `phoenix-rtos-kernel/hal/aarch64/generic/console.c`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev`
- rebuilt the generic kernel explicitly with:
  `TARGET=aarch64a53-generic-qemu make -C phoenix-rtos-kernel all`
- rebuilt the generic project/image lane with:
  `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh host project image`
- reran:
  `timeout 10s ./scripts/aarch64a53-generic-qemu.sh`
- separately rebuilt the existing shared-lane target with:
  `TARGET=aarch64a53-zynqmp-qemu ./phoenix-rtos-build/build.sh clean host core project`

## Validation Evidence

- the generic QEMU lane now reaches visible kernel output after `plo`:
  - `Phoenix-RTOS loader v. 1.21`
  - `hal: Cortex-A53 Generic`
  - `cmd: Executing pre-init script`
  - `Phoenix-RTOS microkernel v. 3.3.1 rev. ######## +0`
- the shared AArch64 `aarch64a53-zynqmp-qemu` build still succeeds after the DTB API change

## Notes

- this is the first end-to-end generic AArch64 QEMU milestone where Phoenix visibly reaches kernel code after the `plo` handoff
- the apparent absence of later `hal:` lines is not necessarily a crash; the main banner uses `hal_consolePrint()`, while later lines in `main.c` go through `lib_printf()` and the kernel log path

## Selected Next Step

- define the first repo-by-repo generic userspace build-unblock step so the generic QEMU lane can move beyond a kernel-only smoke target
