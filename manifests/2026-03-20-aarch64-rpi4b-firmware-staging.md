# Manifest: Initial Pi 4 Firmware Staging

- Date: `2026-03-20`
- Step: `STEP-0101`
- Result: `completed`

## Scope

- stage the first Pi 4 firmware boot-tree artifacts
- add a project-local `config.txt`
- copy the raw `plo` image under a firmware-facing kernel filename

## Upstream Repositories

### `phoenix-rtos-project`

- Commit: `2445597`

## Files

- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`
- `phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/config.txt`

## Validation

- refreshed the VM-local copied buildroot in `phoenix-dev`
- rebuilt the Pi 4 scaffold with:
  `LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh host core project image`
- verified the staged boot-tree output under:
  `_boot/aarch64a53-generic-rpi4b/rpi4b/`

## Validation Evidence

- the build now stages:
  - `_boot/aarch64a53-generic-rpi4b/rpi4b/config.txt`
  - `_boot/aarch64a53-generic-rpi4b/rpi4b/kernel8.img`
- `config.txt` currently enables:
  - `arm_64bit=1`
  - `kernel=kernel8.img`
  - `kernel_address=0x40080000`
  - `boot_load_flags=0x1`
  - `enable_uart=1`
  - `uart_2ndstage=1`

## Notes

- this is only the first firmware-facing staging step
- the boot tree still lacks a staged Pi 4 DTB
- the current generic loader still assumes EL3 entry, which is a separate blocker from artifact staging

## Selected Next Step

- define the first optional Pi 4 DTB staging hook
