# Manifest: Pi 4 Optional DTB Staging Hook

- Date: `2026-03-20`
- Step: `STEP-0103`
- Status: `validated`

## Scope

- add the first optional Pi 4 DTB staging hook in `phoenix-rtos-project`
- keep the default `aarch64a53-generic-rpi4b` build self-contained when no DTB is provided
- allow an explicitly provided Pi 4 DTB to be staged into the firmware-facing boot tree

## Repositories And SHAs

- `phoenix-rtos-project`
  - commit: `539bd46`
  - summary: `project: add optional rpi4b dtb staging hook`

## Files

- `sources/phoenix-rtos-project/_projects/aarch64a53-generic-rpi4b/build.project`

## Validation

### 1. Default no-DTB build

Command run in `phoenix-dev` from a copied disposable buildroot:

```sh
LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh host core project image
```

Result:

- build succeeded
- staged boot tree contained:
  - `_boot/aarch64a53-generic-rpi4b/rpi4b/config.txt`
  - `_boot/aarch64a53-generic-rpi4b/rpi4b/kernel8.img`
- no DTB file was staged by default

### 2. Explicit DTB-path staging

Command run in `phoenix-dev` from a separate copied disposable buildroot:

```sh
tmpdtb=$(mktemp /tmp/rpi4b-step0103-XXXXXX.dtb)
printf 'step0103-dtb\n' > "$tmpdtb"
RPI4B_DTB_PATH="$tmpdtb" LIBPHOENIX_DEVEL_MODE=n TARGET=aarch64a53-generic-rpi4b ./phoenix-rtos-build/build.sh host core project image
```

Result:

- build succeeded
- staged boot tree contained:
  - `_boot/aarch64a53-generic-rpi4b/rpi4b/config.txt`
  - `_boot/aarch64a53-generic-rpi4b/rpi4b/kernel8.img`
  - `_boot/aarch64a53-generic-rpi4b/rpi4b/bcm2711-rpi-4-b.dtb`
- the staged DTB bytes matched the synthetic input (`73 74 65 70 30 31 30 33 2d 64 74 62 0a`)

## Notes

- the hook remains project-local and introduces no new mandatory external dependency
- the next concrete Pi 4 boot blocker is now the generic loader's EL3-only entry and EL3-only `hal_exitToEL1` path
