# 2026-03-21: refreshed Pi 4 SD-image export gap after firmware refinement

## Scope

- Step: `STEP-0289`
- Goal: refresh the host-visible Pi 4 SD image after the HDMI firmware
  refinement

## Result

- negative but bounded result

The direct export step did not work because the expected VM-local source image
was missing:

- expected:
  - `/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy/_boot/aarch64a72-generic-rpi4b/rpi4b-sd.img`
- observed:
  - file not present after the latest `build.sh project image` refresh

## What This Proves

- `TARGET=aarch64a72-generic-rpi4b ./phoenix-rtos-build/build.sh project image`
  refreshes the staged boot artifacts under `_boot/.../rpi4b/`
  and `loader.disk`
- but it does not recreate the full exported SD-card disk image
- that full disk image remains a separate artifact built by the dedicated
  helper:
  - `scripts/assemble-rpi4b-sdimg.sh`

## Next Step

- the smallest corrected sequence is now:
  1. rerun `scripts/assemble-rpi4b-sdimg.sh`
  2. rerun `scripts/export-rpi4b-sdimg.sh`
  3. record the refreshed host-visible checksum
