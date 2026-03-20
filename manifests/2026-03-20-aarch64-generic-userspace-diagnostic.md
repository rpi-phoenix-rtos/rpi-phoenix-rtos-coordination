# Manifest: Generic Userspace-Start Diagnostic

- Date: `2026-03-20`
- Step: `STEP-0091`
- Result: `completed`

## Scope

- add a direct PL011 startup banner to `pl011-tty`
- rebuild the needed generic artifacts
- rerun the generic QEMU smoke lane

## Upstream Repositories

### `phoenix-rtos-devices`

- Commit: `5de90aa`

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

- the generic QEMU smoke output now includes:
  - `pl011-tty: started`
- this proves that the generic runtime path reaches userspace and that `pl011-tty` starts on the non-secure QEMU PL011

## Notes

- the next smallest unknown is whether `pl011-tty` reaches `/dev/console` registration and full console readiness
- once the runtime path is fully understood, this startup banner can be revisited and potentially removed

## Selected Next Step

- define the first console-ready diagnostic step after `pl011-tty: started`
