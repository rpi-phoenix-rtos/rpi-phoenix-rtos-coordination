# Manifest: Generic AArch64 Utils Target

- Date: `2026-03-20`
- Step: `STEP-0073`
- Result: `completed`

## Scope

- add the generic AArch64 target file in `phoenix-rtos-utils`
- keep the default component set aligned with the existing board-agnostic `psh` target file
- validate the repo directly on the generic target in `phoenix-dev`

## Upstream Repositories

### `phoenix-rtos-utils`

- Commit: `3678869`

## Files

- `phoenix-rtos-utils/_targets/Makefile.aarch64a53-generic`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev`
- validated the repo directly with:
  `TARGET=aarch64a53-generic-qemu PROJECT_PATH=/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_projects/aarch64a53-generic-qemu make -C phoenix-rtos-utils all`

## Validation Evidence

- `phoenix-rtos-utils` now builds successfully for the generic target
- the repo produces the expected board-agnostic utility:
  - `psh`

## Notes

- like the filesystem step, the direct repo validation uses the generic project path so `board_config.h` is available when needed
- the next smallest blocker is no longer a target-file alias in an obviously board-agnostic repo; the next clean slice is the hard `libphoenix` AArch64 reboot guard

## Selected Next Step

- define the smallest generic AArch64 `libphoenix` reboot-support step
