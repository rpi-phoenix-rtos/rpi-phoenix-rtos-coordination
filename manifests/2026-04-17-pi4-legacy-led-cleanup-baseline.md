# Pi 4 Legacy LED Cleanup Baseline

Date: `2026-04-17`

## Purpose

Record the first reproducible Pi 4 image after the remaining legacy GPIO42 /
ACT-LED diagnostics were removed from the committed bring-up path.

## Source Repositories

- `phoenix-rtos-project`
  - path:
    `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project`
  - commit:
    `45e277d`
  - change:
    remove legacy GPIO42 diagnostics from the Pi 4 custom armstub and remove
    the obsolete `PLO_RPI_ACTLED_DIAG` board-config block

- `phoenix-rtos-kernel`
  - path:
    `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel`
  - commit:
    `1b55a92f`
  - change:
    remove the Pi 4 kernel-entry ACT-LED assertion from
    `hal/aarch64/_init.S`

- `phoenix-rtos-filesystems`
  - path:
    `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-filesystems`
  - commit:
    `1884043`
  - change:
    remove the `dummyfs` Stage-5 GPIO42 signal from `dummyfs/srv.c`

- `plo`
  - path:
    `/Users/witoldbolt/phoenix-rpi/sources/plo`
  - commit:
    `7664e6f`
  - change:
    remove the remaining Pi 4 ACT-LED telemetry machinery from earliest
    generic AArch64 `plo` startup

## Coordination Repository

- path:
  `/Users/witoldbolt/phoenix-rpi`
- tracker-reconciliation baseline before this cleanup:
  `8eefc01`

## Validation

- source-repo `git diff --check`:
  - `phoenix-rtos-project`: pass
  - `phoenix-rtos-kernel`: pass
  - `phoenix-rtos-filesystems`: pass
  - `plo`: pass
- fast rebuild:
  - `./scripts/rebuild-rpi4b-fast.sh --qemu-sanity`
  - result: pass
  - selected scope: `core project image`
- observed QEMU sanity markers:
  - `call: exec go!`
  - `go: enter`
  - `hal: jump exit el1`
  - `A3`
  - `KLMconsole: pl011 init done`
- canonical export:
  - pass
- FAT-aware SD-image verification:
  - pass

## Image Artifact

- path:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- sidecar:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img.meta.txt`
- SHA-256:
  `eff8ca6193da33baeeb5af6c7fee3deefbd6a6243388b5cc708544bab2dd210e`

## Warnings Surfaced

- no compiler warnings were emitted in the touched repos during the fast
  rebuild
- operator-facing docs still referenced the older LED-stage and dual-profile
  UART workflow at the start of this step; those docs were updated in the same
  session so later board retries do not follow stale guidance

## Next Step

Run the next real Pi 4 retry on this reproducible cleanup image, using UART at
`115200` as the primary observability lane and `postswitch` only as a fallback
if the firmware still overrides the configured baud rate.
