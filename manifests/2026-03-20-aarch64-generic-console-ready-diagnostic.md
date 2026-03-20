# Manifest: Generic Console-Ready Diagnostic

- Date: `2026-03-20`
- Step: `STEP-0093`
- Result: `completed`

## Scope

- add a direct PL011 console-ready banner after successful `_PATH_CONSOLE` registration
- rebuild the needed generic artifacts
- rerun the generic QEMU smoke lane

## Upstream Repositories

### `phoenix-rtos-devices`

- Commit: `82b9fa7`

## Files

- `phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev`
- rebuilt `phoenix-rtos-devices all`
- rebuilt the generic project/image lane with:
  `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh host project image`
- reran the generic QEMU smoke lane with:
  `timeout 12s ./scripts/aarch64a53-generic-qemu.sh`
- reran a longer timing check with:
  `timeout 20s ./scripts/aarch64a53-generic-qemu.sh`

## Validation Evidence

- both smoke runs still show:
  - `pl011-tty: started`
- neither smoke run shows:
  - `pl011-tty: console ready`
- this proves that the current runtime path reaches `pl011_init()` but does not reach successful `_PATH_CONSOLE` registration within the current boot window

## Notes

- the next smallest unknown is whether the code reaches successful `/dev/tty0` registration before failing or stalling
- the follow-up should stay in `pl011-tty` and add one localized `/dev/tty0` registration diagnostic before touching shell or kernel code

## Selected Next Step

- define the first `/dev/tty0` registration diagnostic after the missing console-ready banner
