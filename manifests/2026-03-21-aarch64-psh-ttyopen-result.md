# Manifest: `psh_ttyopen()` Failure Visibility

- Date: `2026-03-21`
- Step: `STEP-0243`
- Status: `completed`

## Goal

- expose the first shared `psh_ttyopen("/dev/console")` failure reason without
  changing shell policy or console-device selection

## Implementation

Changed repository:

- `sources/phoenix-rtos-utils`

Changed file:

- `sources/phoenix-rtos-utils/psh/psh.c`

Upstream commit:

- `phoenix-rtos-utils 26eb2e2`

Bounded change:

- added a one-shot trace helper in `psh_ttyopen()`
- the helper prints only the first negative stage and errno-shaped result:
  `psh: tty open fail <stage> <err>`

## Validation

### Build guardrails

- refreshed the copied buildroot with:
  `./scripts/prepare-buildroot.sh --copy-components`
- generic build:
  `TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image`
- Pi 4 build:
  `RPI4B_DTB_PATH=.../bcm2711-rpi-4-b.dtb RPI4B_QEMU_MEMORY_SIZE=80000000 TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image`
- both succeeded in `phoenix-dev`

### Runtime verification

#### Generic `virt`

- within the 30-second QEMU window, now prints:
  - `psh: tty open`
  - `psh: tty open fail open -2`
  - `psh: app done`

#### Pi 4 `raspi4b`

- within the same 30-second QEMU window, now prints:
  - `psh: tty open`
  - `psh: tty open fail open -2`
  - `psh: app done`

## Result

- both fast lanes now prove the same next blocker:
  `open("/dev/console", O_RDWR) -> -ENOENT`
- the next useful step is no longer inside `psh_ttyopen()` itself
- the next step should inspect or trace the shared `/dev/console` path
  resolution and registration contract

## Next Step

- scope the smallest `/dev/console` lookup and registration split on the shared
  fast lane
