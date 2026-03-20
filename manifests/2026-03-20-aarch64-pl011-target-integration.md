# Manifest: Generic PL011 Target Integration

- Date: `2026-03-20`
- Step: `STEP-0083`
- Result: `completed`

## Scope

- add `pl011-tty` to the generic AArch64 devices target defaults
- keep the change inside `phoenix-rtos-devices`
- validate `phoenix-rtos-devices all` for the generic target

## Upstream Repositories

### `phoenix-rtos-devices`

- Commit: `e0b01b7`

## Files

- `phoenix-rtos-devices/_targets/Makefile.aarch64a53-generic`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev`
- validated the repo directly with:
  `TARGET=aarch64a53-generic-qemu PROJECT_PATH=/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_projects/aarch64a53-generic-qemu make -C phoenix-rtos-devices all`

## Validation Evidence

- the generic devices target now selects `pl011-tty` by default
- `phoenix-rtos-devices all` remains green for `aarch64a53-generic-qemu`

## Notes

- the first PL011 userspace console component is now part of the generic devices build lane
- the next smallest blocker is no longer in `phoenix-rtos-devices`; it is the missing generic target board-config wiring that gives the driver a real PL011 base address and clock

## Selected Next Step

- define the first generic PL011 board-config wiring step
