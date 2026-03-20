# Manifest: Generic `/dev/tty0` Diagnostic

- Date: `2026-03-20`
- Step: `STEP-0095`
- Result: `completed`

## Scope

- add a direct PL011 `/dev/tty0` registration banner
- rebuild the needed generic artifacts
- rerun the generic QEMU smoke lane

## Upstream Repositories

### `phoenix-rtos-devices`

- Commit: `923e1a3`

## Files

- `phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev`
- rebuilt `phoenix-rtos-devices all`
- rebuilt the generic project/image lane with:
  `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh host project image`
- reran the generic QEMU smoke lane with:
  `timeout 12s ./scripts/aarch64a53-generic-qemu.sh`

## Validation Evidence

- the smoke output still includes:
  - `pl011-tty: started`
- the smoke output still does not include:
  - `pl011-tty: tty0 ready`
  - `pl011-tty: console ready`
- this proves that the current runtime path does not reach successful `/dev/tty0` registration within the current smoke window

## Notes

- local source inspection shows that `create_dev()` waits for `devfs` lookup, then sends synchronous create messages into the `/dev` namespace
- local `dummyfs -D` source inspection shows that it daemonizes and only later signals readiness after synchronous mounting, making a startup-order race a plausible next hypothesis

## Selected Next Step

- define the first generic startup-timing test after the missing `/dev/tty0` banner
