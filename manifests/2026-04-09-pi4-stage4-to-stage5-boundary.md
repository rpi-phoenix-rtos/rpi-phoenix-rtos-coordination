# 2026-04-09: Pi 4 stage-4 to stage-5 boundary from `IMG_0005.mov`

## Summary

The next Pi 4 hardware retry on the post-stage-`4` split image did not move
the failure later than the newly added post-register-clear checkpoint. The new
video `IMG_0005.mov` still most strongly indicates that the board enters
generic AArch64 `plo _start` but dies before checkpoint `5`.

## Important Observation

Although the operator expected a `60 fps` clip, `ffprobe` reports the actual
video stream as `30.01 fps`. Future timing-sensitive LED analysis should verify
the real encoded frame rate instead of trusting the capture setting label.

## Extracted ACT Windows

Using the left-side ACT region against the steady right-side power-light
background, the strongest visible activity windows were:

- `0.79s - 5.86s`
- `6.89s - 7.29s`
- `8.09s - 8.12s` (weak / near-threshold)
- `10.12s - 10.52s`
- `11.36s - 11.72s`
- `14.59s - 14.96s`
- `15.79s - 16.19s`
- `16.99s - 17.39s`

After about `17.39s`, no further ACT activity was detected in the clip.

## Interpretation

Durable inference:

- the result still fits entry into checkpoint `4` (`plo _start`)
- the result still does **not** fit completion of checkpoint `5`
- therefore the current failure is most likely inside earliest generic
  AArch64 `plo _start`, between stage `4` and the post-register-clear
  checkpoint `5`

This is stronger than the previous result because the current image already
contained the stage-`5` marker, yet the video still did not extend into its
expected time range.

## Consequence

The next bounded diagnostic move should not add more later EL-path markers.
It should split the current `_start` register-clearing block itself, for
example with one additional checkpoint partway through the `mov xN, #0` series
and one at its end, then rebuild and retry.
