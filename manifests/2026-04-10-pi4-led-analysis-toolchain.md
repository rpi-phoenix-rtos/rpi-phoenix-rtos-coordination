# 2026-04-10: Pi 4 LED Analysis Toolchain

## Scope

- replace the old ad hoc fixed-ROI decoder with a reusable Python workflow
- separate raw video parsing from probe-layout interpretation
- decode `IMG_7137.mov`, which matches the current fixed-entry-trampoline image

## Changes

- added raw analyzer:
  - `/Users/witoldbolt/phoenix-rpi/scripts/analyze-rpi4-actled-video.py`
- added current probe-layout source of truth:
  - `/Users/witoldbolt/phoenix-rpi/scripts/rpi4_actled_probe_layout.py`
- added JSON interpreter:
  - `/Users/witoldbolt/phoenix-rpi/scripts/interpret-rpi4-actled-analysis.py`

## Tooling Contract

- raw analyzer output is standardized JSON
- current stage meanings are no longer inferred ad hoc from chat
- future probe changes must update:
  - the assembly source
  - `scripts/rpi4_actled_probe_layout.py`
  in the same step

## Validation

- `python3 -m py_compile scripts/analyze-rpi4-actled-video.py scripts/interpret-rpi4-actled-analysis.py scripts/rpi4_actled_probe_layout.py`
  - pass
- `IMG_7137.mov` decoded through the new toolchain
  - best contiguous run:
    - stage `3` / `00011`
  - next missing expected stage:
    - stage `4` / `00100`
  - one unmatched false-positive group:
    - stage `16` / `10000`
- backward-compatibility sanity check:
  - `IMG_7136.mov` with the current layout still decodes the shared armstub
    prefix:
    - stage `1`
    - stage `2`
    - stage `3`
  - no later stage `4` is seen there either

## Current Conclusion

- the new tooling preserves the previously established result:
  - armstub stage `3` reached
  - fixed-entry `plo` stage `4` still not observed
- next code work should stay on the stage-`3 -> 4` seam
- preserved interpretation rule:
  - initial ACT LED chatter during firmware SD-card reads is pre-Phoenix
    preamble noise
  - ignore that activity unless it participates in a later valid contiguous
    Phoenix stage decode
