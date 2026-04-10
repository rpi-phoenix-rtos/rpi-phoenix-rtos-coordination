# Current Step

## Metadata

- Step ID: `STEP-0456`
- Title: Verify fixed handoff target contents before the Pi 4 stage-`3 -> 4` branch
- Status: `in_progress`
- Date: `2026-04-10`
- Milestone / phase: `Phase 1`

## Objective

- distinguish the two remaining stage-`3 -> 4` possibilities:
  - `plo` is not actually present at `0x40080000`
  - `plo` is present there, but execution still fails before stage `4`
- preserve the new LED-analysis toolchain as the default readout path for the
  next board retry

## Scope

In scope:

- add one armstub-side target-memory verification step before the fixed branch
- compare the fixed target against a deliberate `plo` entry signature
- optionally record the firmware-patched `kernel_entry32` slot as a secondary
  clue if that can be done without widening the step too much

Out of scope:

- unrelated EL-path, framebuffer, DTB, or USB work
- redesigning the whole Pi 4 boot model before the target-memory question is
  answered

## Expected Repositories

- `phoenix-rtos-project`
- `plo`
- coordination repo

## Expected Files Or Subsystems

- `/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`
- `/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S`
- `/Users/witoldbolt/phoenix-rpi/scripts/rpi4_actled_probe_layout.py`
- `/Users/witoldbolt/phoenix-rpi/docs/status.md`
- `/Users/witoldbolt/phoenix-rpi/tracking/current-step.md`

## Acceptance Criteria

- the next hardware image can answer whether the fixed handoff target memory at
  `0x40080000` contains the expected `plo` entry signature before branching
- the next board video can distinguish:
  - missing or wrong load target
  - valid target contents but failed execution after branch

## Validation Plan

- rebuild Pi 4 image
- rerun the standard no-hardware gates
- use the current LED-analysis toolchain on the next board video

## Rollback / Baseline

- Known-good manifest or commit set:
  `/Users/witoldbolt/phoenix-rpi/manifests/2026-04-10-pi4-led-analysis-toolchain.md`

## Notes

- current confirmed decode from `IMG_7137.mov`:
  - stage `3`: armstub before fixed jump
  - no valid later stage `4`
- initial SD-read LED chatter remains firmware preamble noise and should be
  ignored unless it participates in a later valid contiguous Phoenix decode
