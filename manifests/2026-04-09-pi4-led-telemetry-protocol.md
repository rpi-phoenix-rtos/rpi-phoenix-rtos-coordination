# 2026-04-09 Pi 4 LED Telemetry Protocol

## Summary

The old Pi 4 no-UART GPIO42 diagnostics were replaced with one structured
telemetry protocol spanning the custom Pi 4 armstub and the earliest generic
AArch64 `plo` path.

The goal is to localize the highest completed boot checkpoint from a single
high-framerate ACT-LED video, instead of moving one one-off probe at a time.

## Checkpoint Map

- `1`: armstub primary-core entry
- `2`: armstub after early timer / GIC preparation
- `3`: armstub just before the fixed-address jump to `plo`
- `4`: earliest generic AArch64 `plo` `_start`
- `5`: `plo` EL3 path selected
- `6`: `plo` EL2 path selected
- `7`: `plo` EL1 path selected
- `8`: `plo` `start_common`
- `9`: `plo` core-0 branch to `_startc`

Each checkpoint emits one pulse group separated by a longer off gap.

## Touched Repositories

- `plo`
  - `1f2c93d` `aarch64: add pi4 structured led telemetry`
- `phoenix-rtos-project`
  - `057757f` `project: add pi4 staged led telemetry`
- coordination repo

## Validation

### Build

- `./scripts/prepare-rpi4b-dtb.sh`
- `limactl shell -y phoenix-dev -- /bin/bash -lc 'cd /Users/witoldbolt/phoenix-rpi && ./scripts/prepare-buildroot.sh --copy-components /home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy'`
- `limactl shell -y phoenix-dev -- /bin/bash -lc 'set -euo pipefail; export PATH=/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix/bin:$PATH; cd /home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy; RPI4B_DTB_PATH=/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh clean host core project image'`
- `limactl shell -y phoenix-dev -- /bin/bash -lc 'set -euo pipefail; export PATH=/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix/bin:$PATH; cd /home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy; TARGET=aarch64a53-generic-qemu ./phoenix-rtos-build/build.sh clean host core project image'`

### Emulator Sanity

- `./scripts/qemu-shell-smoke.sh generic`
  - passed
- direct Pi 4 QEMU serial sanity on the real-device build:
  - reached `go: enter`
  - reached `go: hal done`
  - reached `hal: jump exit el1`
  - reached `A3`
  - reached `KLM`
  - then hit the known later direct-QEMU `Exception #37` on the unpatched
    real-device DTB lane

### Image Assembly And Export

- `./scripts/assemble-rpi4b-bootfs.sh`
- `./scripts/assemble-rpi4b-bootfs-img.sh`
- `./scripts/assemble-rpi4b-sdimg.sh`
- `./scripts/export-rpi4b-sdimg.sh`
- exported host image:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256: `6d6b4d7dd84f237f3e8dab1764f8be34b29b4e4d46d6f92ad30aee1869a2acdc`

## Notes

- The first rebuild attempt failed because the armstub pre-header code grew
  past the firmware metadata `.org` region.
- That was fixed by reducing each armstub checkpoint call site to a single
  `bl gpio42_stageN` instruction and moving the stage-number setup into helper
  wrappers after the `0x100` armstub header area.
