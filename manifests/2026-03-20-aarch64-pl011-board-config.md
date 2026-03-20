# Manifest: Generic QEMU PL011 Board Config

- Date: `2026-03-20`
- Step: `STEP-0085`
- Result: `completed`

## Scope

- wire the generic QEMU PL011 base and clock into `board_config.h`
- keep the change limited to the generic QEMU project board config
- validate the generic devices build with the populated board config

## Upstream Repositories

### `phoenix-rtos-project`

- Commit: `8ca94a7`

## Files

- `phoenix-rtos-project/_projects/aarch64a53-generic-qemu/board_config.h`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev`
- validated the generic devices lane with:
  `TARGET=aarch64a53-generic-qemu PROJECT_PATH=/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_projects/aarch64a53-generic-qemu make -C phoenix-rtos-devices all`

## Validation Evidence

- the generic QEMU project board config now defines:
  - `PL011_TTY_BASE  0x09000000u`
  - `PL011_TTY_CLOCK 24000000u`
- the generic devices lane remains green with the populated board config

## Notes

- the first PL011 userspace console path now has the runtime parameters it needs on the generic QEMU target
- the next smallest fast-lane step is to load the minimal userspace sequence in `user.plo` so `dummyfs` comes up before `pl011-tty`

## Selected Next Step

- define the first generic `user.plo` integration step for `dummyfs` and `pl011-tty`
