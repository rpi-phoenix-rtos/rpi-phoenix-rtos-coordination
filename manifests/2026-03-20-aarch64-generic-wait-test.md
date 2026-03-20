# Manifest: Generic `wait` Test

- Date: `2026-03-20`
- Step: `STEP-0097`
- Result: `abandoned`

## Scope

- insert a short `wait 500` between `dummyfs;-N;devfs;-D` and `pl011-tty` in the generic `user.plo`
- rebuild the needed generic artifacts
- rerun the generic QEMU smoke lane

## Upstream Repositories

- no upstream source changes were kept

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev`
- rebuilt the generic project/image lane with:
  `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh host project image`
- reran the generic QEMU smoke lane with:
  `timeout 12s ./scripts/aarch64a53-generic-qemu.sh`
- inspected the local loader implementation in:
  `plo/cmds/wait.c`

## Validation Evidence

- the QEMU output changed to:
  - `Waiting for input,   400 [ms]`
  - `Can't get data from console.`
  - `Please reset plo and set console to device.`
- local source inspection in `plo/cmds/wait.c` confirms that `wait` polls for console input with `lib_consoleGetc()` and is not a passive sleep primitive

## Conclusion

- `wait` cannot be used as a non-interactive timing delay in the current generic fast lane because the generic loader path does not expose the needed console input device
- the experimental `user.plo` change was reverted and not committed upstream

## Selected Next Step

- define the first Pi 4-specific no-hardware scaffold step after the generic QEMU runtime boundary
