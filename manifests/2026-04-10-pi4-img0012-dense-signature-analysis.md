# Pi 4 IMG_0012 Dense Signature Analysis

Date: `2026-04-10`

## Goal

- decode the first board retry on the dense armstub signature-map image
- replace the old stage-`3` boundary with the exact highest dense seam stage

## Input

- video:
  `/Users/witoldbolt/Downloads/IMG_0012.mov`
- current layout:
  `pi4_dense_armstub_signature_map_2026_04_10`

## Decoder Result

- real clip rate:
  about `59.92 fps`
- auto-detected ACT LED ROI:
  `156,162,286,269`
- best contiguous Phoenix run:
  - `2`: armstub after timer/GIC
  - `3`: armstub before fixed jump
  - `23`: late seam entry
  - `24`: fixed target address loaded
- next expected stage not seen:
  - `25`: first signature word read
- no later valid:
  - `25`
  - `31`
  - `0`
  in the main contiguous run

## Unmatched Valid Bursts

- one earlier unmatched burst decodes as:
  - `16`
- one later unmatched burst decodes as:
  - `27`

These are preserved as noise or secondary candidates, not as the primary run,
because they do not extend the contiguous `2 -> 3 -> 23 -> 24` sequence.

## Interpretation

- the board now gets materially farther than the earlier stage-`3` boundary
- the live failure seam is now:
  - after `24`: fixed target address loaded
  - before `25`: first signature word read
- that means the current best next step is no longer more global armstub or
  `plo` probing
- the next step should target the first fixed-target read band directly

## Next Step

- split or harden the first-read band so the next retry can distinguish:
  - fault on the first `ldr`
  - success of the first `ldr` but loss before stage `25`
  - a decoder miss for stage `25` with real later progress
