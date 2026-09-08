# Boot and fault tally for 2026-09-08 (computed from existing UART logs)

A stability figure for the presentation, derived from logs already on disk rather
than new Pi cycles. Every `test-cycle-*` invocation powers the board on exactly
once, so one UART log = one boot attempt.

Markers are the ones `scripts/uart-summary.sh` uses, so the numbers are
comparable to any single-run summary: boot success = the `(psh)%` prompt appears;
fault = `Exception|Data Abort|panic|\bfault\b|ESR=|ELR=|FAR=|EC=`.

## Boots

| metric | value |
|---|---|
| boots (one power-on per log) | **132** |
| reached the `(psh)%` prompt | **132 — 100.0%** |
| logs containing any fault pattern | 17 |

## Every fault log is attributed — none are unexplained

| faulting process | logs |
|---|---|
| `/usr/bin/supertuxkart` | 11 |
| `/bin/Xphoenix-glamor-daemon` | 6 |
| *unattributed* | **0** |

- The **11 SuperTuxKart** ones are all `--profile-time` runs hitting the known
  crash while formatting the per-kart report — it fires *after* the race and
  *after* the FPS line, is benchmark-only, and does not occur on the demo path
  (`--race-now` runs are clean). See `2026-09-08-stk-framerate-measurement-void.md`.
- The **6 glamor-daemon** ones are the owner's bug #4 (Data Abort on exiting
  Window Maker) being deliberately reproduced while it was being fixed. All six
  predate the fix.

## Bug #4's fix holds, quantified

Splitting X sessions (`xlaunch: starting server`) at the fix:

| | X sessions | with a glamor-daemon Data Abort |
|---|---|---|
| before the fix (< 01:44) | 6 | **6 (100%)** |
| after the fix (>= 01:44) | **18** | **0** |

## What this does and does not say

It says: the board booted 132/132, and no fault today lacks a known cause. It is
a stronger stability statement than any single gate run, because it is a large
sample accumulated across many different workloads.

It does **not** say the *SD boot* path is reliable — every one of these 132 boots
is netboot, since there is no card in the host reader. And it is a same-day
sample on one board, so it speaks to software reliability, not to hardware
longevity or thermal behaviour over a long session.

Reproduce: the tally is a short glob-and-count over
`artifacts/rpi4b-uart/rpi4b-uart-<date>-*.log`; the field to compare for a
time split is `basename.split('-')[4]` (index 3 is the *date* — getting that
wrong initially produced a bogus "6 of 24 post-fix sessions crashed", i.e. the
fix appearing not to hold).
