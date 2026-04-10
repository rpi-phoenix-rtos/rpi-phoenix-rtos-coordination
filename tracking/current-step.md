# Current Step

## Metadata

- Step ID: `STEP-0451`
- Title: Await the next Pi 4 board retry on the stage-`3 -> 4` handoff-hardened image
- Status: `in_progress`
- Date: `2026-04-10`
- Milestone / phase: `Phase 1`

## Objective

- run the next real Pi 4 retry on the handoff-hardened image
- determine whether the fixed-address armstub branch now reaches the first
  generic `plo` instruction on real hardware
- distinguish:
  - still failing before generic `plo`
  - entering generic `plo` but failing immediately after the inline stage `4`
  - or progressing farther into the existing stage-code map

## Scope

In scope:

- flashing the refreshed image
- recording one new close ACT-LED video
- decoding whether inline stage `4` now appears

Out of scope:

- broader EL-path, USB, framebuffer, or DTB work before the next video

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- `/Users/witoldbolt/phoenix-rpi/docs/pi4-first-hardware-trial.md`
- `/Users/witoldbolt/phoenix-rpi/docs/manual-operator-instructions.md`
- `/Users/witoldbolt/phoenix-rpi/tracking/current-step.md`

## Acceptance Criteria

- the operator flashes the refreshed image
- the next video is sufficient to answer whether stage `4` now appears
- the resulting analysis narrows the seam beyond the old stage `3 -> 4` ambiguity

## Validation Plan

- current code/image baseline already validated:
  - Pi 4 A72 rebuild: pass
  - generic AArch64 rebuild: pass
  - generic QEMU shell path still reaches runtime and `help`
  - direct Pi 4 QEMU serial sanity: pass
  - canonical export: pass
  - FAT-aware verifier: pass

## Rollback / Baseline

- Known-good manifest or commit set:
  `/Users/witoldbolt/phoenix-rpi/manifests/2026-04-10-pi4-stage34-handoff-hardening.md`

## Notes

- The previous `IMG_7135.mov` result decoded only stages `1`, `2`, and `3`.
- The active response in this image is:
  - preserve the primary armstub path argument registers
  - insert `dsb sy; ic iallu; dsb sy; isb` immediately before `br 0x40080000`
  - replace the old helper-call stage `4` emission with an inline direct-GPIO
    stage `4` emitter at the first `_start` instruction in generic `plo`
- Current refreshed exported image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- Current validated SHA-256:
  `4b9c967c9381e8935998a19eb1a976c43b440dd57da4c5fab489763f729a6835`
