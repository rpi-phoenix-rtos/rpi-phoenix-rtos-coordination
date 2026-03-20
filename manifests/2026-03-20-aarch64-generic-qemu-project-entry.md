# Manifest: AArch64 Generic QEMU Project Entry

- Date: `2026-03-20`
- Step: `STEP-0057`
- Result: `completed`

## Scope

- add the first generic AArch64 target directory in `phoenix-rtos-project`
- add the first `aarch64a53-generic-qemu` project directory
- add the first QEMU `virt` launcher script for the new generic AArch64 path

## Upstream Repositories

### `phoenix-rtos-project`

- Commit: `975d017`

## Files

- `phoenix-rtos-project/_targets/aarch64a53/generic/build.project`
- `phoenix-rtos-project/_targets/aarch64a53/generic/nvm.yaml`
- `phoenix-rtos-project/_targets/aarch64a53/generic/preinit.plo.yaml`
- `phoenix-rtos-project/_targets/aarch64a53/generic/user.plo.yaml`
- `phoenix-rtos-project/_projects/aarch64a53-generic-qemu/build.project`
- `phoenix-rtos-project/_projects/aarch64a53-generic-qemu/board_config.h`
- `phoenix-rtos-project/scripts/aarch64a53-generic-qemu.sh`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev` with:
  `./scripts/prepare-buildroot.sh --copy-components`
- prebuilt the generic kernel directly into the `aarch64a53-generic-qemu` output tree
- then validated the project and image layer with the toolchain-supplied `libphoenix` sysroot path

## Validation Commands

`TARGET=aarch64a53-generic-qemu make -C phoenix-rtos-kernel all`

`LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh host project image`

## Validation Evidence

- the project build generated the first generic QEMU loader scripts under `_build/aarch64a53-generic-qemu/plo-scripts/`
- the project build produced:
  - `_boot/aarch64a53-generic-qemu/plo.elf`
  - `_boot/aarch64a53-generic-qemu/loader.disk`
- the new runtime script matches those artifact names and the selected RAM-backed `loader.disk` boot path

## Notes

- the first generic project entry intentionally keeps the user script kernel-only so the new QEMU lane is not blocked on a user-space PL011 driver
- the current validation lane is narrower than the eventual authoritative lane:
  - it prebuilds the kernel directly
  - it then runs `host project image`
  - it sets `LIBPHOENIX_DEVEL_MODE=n`
- that narrower validation path is currently necessary because the full generic `clean core project image` lane still lacks generic-target support in `libphoenix`, `phoenix-rtos-filesystems`, `phoenix-rtos-devices`, and `phoenix-rtos-utils`
- the first runtime script intentionally uses `virt,secure=on,gic-version=2` because the current generic `plo` startup path is EL3-centric

## Selected Next Step

- define the first emulated generic AArch64 smoke-lane step
