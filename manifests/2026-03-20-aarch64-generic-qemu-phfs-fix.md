# Manifest: Generic AArch64 QEMU PHFS Pre-Init Fix

- Date: `2026-03-20`
- Step: `STEP-0065`
- Result: `completed`

## Scope

- add raw PHFS setup for `ram0` in the generic AArch64 pre-init script
- rebuild the generic QEMU project/image artifacts in `phoenix-dev`
- rerun the unchanged smoke command

## Upstream Repositories

### `phoenix-rtos-project`

- Commit: `c7d0c96`

## Files

- `phoenix-rtos-project/_targets/aarch64a53/generic/preinit.plo.yaml`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev`
- rebuilt the generic QEMU project/image lane with:
  `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh host project image`
- reran:
  `timeout 10s ./scripts/aarch64a53-generic-qemu.sh`

## Validation Evidence

- the previous `Can't open user.plo, on ram0` failure is gone
- the loader now reaches the pre-init handoff without reporting the earlier PHFS error
- the next silence appears after:
  - `Phoenix-RTOS loader v. 1.21`
  - `hal: Cortex-A53 Generic`
  - `cmd: Executing pre-init script`
  - `alias: Setting relative base address to 0x0000000000200000`

## Notes

- the raw PHFS setup was the missing piece for opening `user.plo` from the RAM-backed loader image
- the next blocker is no longer in generic `plo` script lookup; it is later in the kernel handoff path

## Selected Next Step

- define the smallest kernel-stage visibility or DTB handoff fix implied by the new silence after the pre-init handoff
