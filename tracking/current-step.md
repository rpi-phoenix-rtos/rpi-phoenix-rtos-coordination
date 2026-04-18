# Current Step

## Metadata

- Step ID: `STEP-0515`
- Title: `Validate the rollback to the last objectively better Pi 4 MMU seam`
- Status: `in_progress`
- Date: `2026-04-18`
- Milestone / phase: `Phase 1`

## Objective

- validate on the real Pi 4 that the selective rollback returns the board to
  the last objectively better hardware seam, `... X3NO`
- stop burning retries on the weaker `3C` baseline now that the tracker shows a
  clear last-better checkpoint in git history
- use the rollback image as the new comparison point for any fresh follow-up
  ideas

## Scope

In scope:

- flashing and booting the already-built image
- UART capture and log interpretation on the real Pi 4
- if needed, one tightly bounded follow-up based on the first hardware result
- tracker and manifest updates recording the real-board evidence

Out of scope:

- restarting broad probe churn from the weaker `3C` baseline
- unrelated cleanup in `plo`, armstub, DTB parsing, or user-space services
- speculative LED instrumentation unless UART becomes insufficient again

## Acceptance Criteria

- a real Pi 4 UART log is captured on the rollback image
- that log either restores the earlier `... X3NO` seam or proves that even the
  known-better historical path no longer reproduces
- the docs record the exact image SHA, log path, and resulting next step

## Validation Plan

- flash image:
  - `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  - SHA-256 `be8c2773306870a5b66b75f64677d68d0a344f01ee348d2e1598aea969ca4fb1`
- capture UART with:
  - `/Users/witoldbolt/phoenix-rpi/scripts/capture-rpi4b-uart.sh`
- summarize with:
  - `/Users/witoldbolt/phoenix-rpi/scripts/summarize-rpi4b-uart-log.py`

## Rollback / Baseline

- recent neutral hardware retries:
  - `phoenix-rtos-kernel 6cd294fd`
    image `5ac0d1290867556a78fe19bad048b1cfe98e8c5328053c2d588ed0d8691006fe`
    log `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-115137.log`
  - `phoenix-rtos-kernel 136b4cae`
    image `f44385750b37adc49bb279156e812e561c61ec8d31b983fae457215cd0fab469`
    log `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260418-220352.log`
- current rollback baseline just completed:
  - `phoenix-rtos-kernel` restored to the `c0fd7ff7` `_init.S` lineage and
    committed in this session
  - `phoenix-rtos-project` restored to the `5218c40` Pi 4 early-UART mapping
    define and committed in this session
  - image SHA-256 `be8c2773306870a5b66b75f64677d68d0a344f01ee348d2e1598aea969ca4fb1`
- target seam to restore:
  - `A2`, `KLM`, `X1`, `X2`, `X3`, `NO`

## Notes

- the stale-image theory has already been disproved for this artifact chain
- `STEP-0513` and `STEP-0514` are both closed as hardware-neutral MMU
  experiments
- this step intentionally uses git history to return to the last objectively
  better hardware seam before trying fresh ideas again
