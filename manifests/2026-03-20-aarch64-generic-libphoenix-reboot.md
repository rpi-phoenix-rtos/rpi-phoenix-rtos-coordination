# Manifest: Generic AArch64 libphoenix Reboot Support

- Date: `2026-03-20`
- Step: `STEP-0075`
- Result: `completed`

## Scope

- make `libphoenix` AArch64 reboot support generic
- keep the change limited to `libphoenix/arch/aarch64/reboot.c`
- validate `libphoenix` directly on `aarch64a53-generic-qemu` in `phoenix-dev`

## Upstream Repositories

### `libphoenix`

- Commit: `0b20a2a`

## Files

- `libphoenix/arch/aarch64/reboot.c`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev`
- validated the repo directly with:
  `TARGET=aarch64a53-generic-qemu make -C libphoenix all V=1`

## Validation Evidence

- `libphoenix` now builds successfully for `aarch64a53-generic-qemu`
- the reboot helper no longer hard-errors on generic AArch64 targets
- the implementation now selects the correct platform-control layout for:
  - `__CPU_ZYNQMP`
  - `__CPU_GENERIC`

## Notes

- the change stays intentionally narrow and does not broaden into generic device or project-policy work
- the next clean discovery step is to rerun the broader generic `host project image` lane and record the next smallest blocker instead of guessing

## Selected Next Step

- define the next generic full-build unblock after `libphoenix`
