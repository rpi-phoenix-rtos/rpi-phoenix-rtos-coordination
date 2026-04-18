# Current Step

## Metadata

- Step ID: `STEP-0514`
- Title: `Validate the deterministic TTBR0 Pi 4 bootstrap image on real hardware`
- Status: `in_progress`
- Date: `2026-04-18`
- Milestone / phase: `Phase 1`

## Objective

- validate on the real Pi 4 that the new deterministic TTBR0 bootstrap map
  moves the board beyond the long-standing `3C` boundary
- test a simpler low-memory-first MMU bootstrap model after the identity-first
  branch-sequencing change proved neutral on hardware
- keep the software baseline frozen unless the first retry on this image also
  proves ineffective

## Scope

In scope:

- flashing and booting the already-built image
- UART capture and log interpretation on the real Pi 4
- if needed, one tightly bounded follow-up based on the first hardware result
- tracker and manifest updates recording the real-board evidence

Out of scope:

- restarting broad probe churn without new evidence
- unrelated cleanup in `plo`, armstub, DTB parsing, or user-space services
- speculative LED instrumentation unless UART becomes insufficient again

## Acceptance Criteria

- a real Pi 4 UART log is captured on the deterministic-TTBR0 image
- that log either proves progress beyond `3C` or establishes a new, better
  bounded failure seam
- the docs record the exact image SHA, log path, and resulting next step

## Validation Plan

- flash image:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256 `f44385750b37adc49bb279156e812e561c61ec8d31b983fae457215cd0fab469`
- capture UART with:
  - `/Users/witoldbolt/phoenix-rpi/scripts/capture-rpi4b-uart.sh`
- summarize with:
  - `/Users/witoldbolt/phoenix-rpi/scripts/summarize-rpi4b-uart-log.py`

## Rollback / Baseline

- previous neutral hardware retry:
  - `phoenix-rtos-kernel 6cd294fd`
  - image SHA-256 `5ac0d1290867556a78fe19bad048b1cfe98e8c5328053c2d588ed0d8691006fe`
  - log:
    `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-115137.log`
- current baseline just completed:
  - `phoenix-rtos-kernel 136b4cae`
  - image SHA-256 `f44385750b37adc49bb279156e812e561c61ec8d31b983fae457215cd0fab469`
- long-standing observed failing boundary:
  - `A2`, `KLM`, `X1`, `X2`, `3C`, then silence

## Notes

- the stale-image theory has already been disproved for this artifact chain
- `STEP-0513` is now closed by the neutral real-board result:
  the identity-first branch-sequencing change did not move the hardware
  boundary at all
- the new strategy is to simplify the TTBR0 bootstrap map itself, not to keep
  changing only TTBR1 activation order
