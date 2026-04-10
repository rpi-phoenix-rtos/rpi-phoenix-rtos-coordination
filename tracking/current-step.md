# Current Step

## Metadata

- Step ID: `STEP-0455`
- Title: Use tool-confirmed stage-`3 -> 4` seam to design the next Pi 4 boot fix
- Status: `in_progress`
- Date: `2026-04-10`
- Milestone / phase: `Phase 1`

## Objective

- use the new standardized LED-analysis toolchain result from `IMG_7137.mov`
  to choose the next smallest code change at the confirmed stage-`3 -> 4`
  handoff seam
- preserve the new tooling as the default path for future hardware-video
  iterations

## Scope

In scope:

- interpreting the confirmed current result:
  - stage `3` reached
  - stage `4` still not observed
- planning the next smallest code-side experiment at that seam

Out of scope:

- changing the probe tooling again unless a real defect is found in it
- unrelated USB, framebuffer, or DTB work

## Expected Repositories

- coordination repo
- likely next: `phoenix-rtos-project` and/or `plo`

## Expected Files Or Subsystems

- `/Users/witoldbolt/phoenix-rpi/scripts/analyze-rpi4-actled-video.py`
- `/Users/witoldbolt/phoenix-rpi/scripts/rpi4_actled_probe_layout.py`
- `/Users/witoldbolt/phoenix-rpi/scripts/interpret-rpi4-actled-analysis.py`
- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`
- `/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S`

## Acceptance Criteria

- next code-side experiment is chosen from the confirmed stage-`3 -> 4`
  boundary, not from vague LED eyeballing
- the toolchain remains the documented default for the next hardware retry

## Validation Plan

- use the current `IMG_7137.mov` decode as the baseline
- carry the tooling forward unchanged for the next board retry unless a tool
  defect is discovered

## Rollback / Baseline

- Known-good manifest or commit set:
  `/Users/witoldbolt/phoenix-rpi/manifests/2026-04-10-pi4-led-analysis-toolchain.md`

## Notes

- best contiguous decoded run from `IMG_7137.mov`:
  - stage `3`: armstub before fixed jump
- next missing expected stage:
  - stage `4`: fixed entry veneer
- one unmatched false-positive group currently decodes as stage `16`; the
  interpreter now treats that as noise instead of the primary result
