# 2026-04-10: Pi 4 Stage-3 to Stage-4 Next-Step Scope

## Input

- current standardized decode from `IMG_7137.mov`:
  - stage `3` reached
  - stage `4` not observed
- preserved interpretation rule:
  - initial ACT chatter during firmware SD-card reads is preamble noise

## Conclusion

The next smallest useful code step is not wider telemetry churn inside later
`plo` code. The remaining uncertainty is now narrower:

1. `plo` is not actually present at `0x40080000` when the armstub branches
2. `plo` is present there, but execution still fails before the stage-`4`
   inline veneer is observed

## Next Step

- add one armstub-side fixed-target memory verification before the branch
- compare the branch target against a deliberate `plo` entry signature
- if feasible within the same bounded step, preserve the firmware-patched
  `kernel_entry32` slot as a secondary clue

## Why This Is Next

- stage `3` already proves the armstub reaches the final pre-branch point
- stage `4` still not appearing means the problem is at or immediately after
  the raw handoff seam
- target-memory verification is the shortest path to distinguish wrong load
  address from failed execution after branch
