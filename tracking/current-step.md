# Current Step

## Metadata

- Step ID: `STEP-0446`
- Title: Split the Pi 4 failure inside earliest `plo _start` register clearing
- Status: `in_progress`
- Date: `2026-04-09`
- Milestone / phase: `Phase 1`

## Objective

- localize the real Pi 4 failure more tightly between checkpoint `4`
  (`plo _start` entry) and checkpoint `5` (after general-purpose register
  clearing), then rebuild the SD image for the next board retry

## Scope

In scope:

- using the new `IMG_0005.mov` hardware result to classify the highest
  completed checkpoint on the current `1..12` image
- adding only the next earliest `_start` checkpoints needed inside the current
  register-clearing block
- rebuilding, re-exporting, and re-verifying the Pi 4 SD image
- updating the runbook and status docs with the narrower interpretation

Out of scope:

- changes later than `currentEL` sampling or EL dispatch
- unrelated USB, framebuffer, shell, or later-runtime work

## Expected Repositories

- `plo`
- coordination repo

## Expected Files Or Subsystems

- `/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S`
- `/Users/witoldbolt/phoenix-rpi/tracking/current-step.md`
- `/Users/witoldbolt/phoenix-rpi/tracking/step-history.md`
- `/Users/witoldbolt/phoenix-rpi/docs/status.md`
- `/Users/witoldbolt/phoenix-rpi/docs/testing-automation.md`
- `/Users/witoldbolt/phoenix-rpi/manifests/2026-04-09-pi4-stage4-to-stage5-split.md`

## Acceptance Criteria

- at least one narrower checkpoint is added between stage `4` and the current
  post-register-clear stage `5`
- the refreshed image passes the current strongest no-hardware regressions
  and the canonical SD export / verification path
- the next hardware retry can distinguish whether the fault is before the
  first half of the register-clearing block or after it

## Validation Plan

- Build:
  - rebuild the Pi 4 A72 image in `phoenix-dev`
- Emulator:
  - generic QEMU shell smoke
  - direct Pi 4 QEMU serial sanity on the real-device build
- Hardware:
  - export and verify the refreshed SD image, ready for the next board retry

## Rollback / Baseline

- Known-good manifest or commit set:
  `/Users/witoldbolt/phoenix-rpi/manifests/2026-04-09-pi4-post-stage4-el-dispatch-split.md`

## Notes

- `IMG_0005.mov` is actually `30.01 fps` according to `ffprobe`, not `60 fps`.
- The new video still most strongly fits failure before stage `5`:
  - visible later green-on windows end at about `17.39s`
  - no later activity extends into the timing range expected for completion of
    checkpoint `5`
  - the current failure therefore appears to be inside earliest generic
    AArch64 `plo _start`, after stage `4` and before the post-register-clear
    stage `5`
- The current telemetry checkpoint map is:
  - `1`: armstub primary-core entry
  - `2`: armstub after early timer / GIC preparation
  - `3`: armstub just before the fixed-address jump to `plo`
  - `4`: earliest generic AArch64 `plo` `_start`
  - `5`: after general-purpose register clearing
  - `6`: after `currentEL` sampling, before EL dispatch
  - `7`: `start_el3`
  - `8`: `start_el2`
  - `9`: `start_el1`
  - `10`: `start_common`
  - `11`: core-0 branch to `_startc`
  - `12`: unexpected-EL trap path
- Current refreshed exported image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- Current validated SHA-256:
  `d1e0fd5b2e3817d4e0d2ad339b63be34fb96d17f2d8a05d4e318d52a02952c20`
