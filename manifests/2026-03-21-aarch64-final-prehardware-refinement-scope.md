# 2026-03-21: scope the smallest final pre-hardware Pi 4 trial refinement

## Scope

- Step: `STEP-0294`
- Goal: decide whether one more small pre-hardware refinement is worth doing
  before the first manual Pi 4 board trial

## Repositories Touched

- coordination repo

## Decision

The highest-value remaining refinement is not another tiny image or checklist
change. It is a focused external-reference review of Circle:

- validate whether Circle contains immediately reusable sequencing for early HDMI
  output on Raspberry Pi 4
- validate whether Circle makes USB keyboard support a realistic near-term step
  for the current no-UART lab

## Why This Was Selected

At this point the project already has:

- a flashable Pi 4 SD image
- validated QEMU shell smoke
- validated `plo` HDMI marker visibility
- a no-UART real-board lab shape

The most useful remaining decision before the first manual board trial is
therefore architectural:

- whether the next work should stay on HDMI visibility
- or pivot toward USB keyboard

Circle is one of the strongest Raspberry Pi bare-metal references for exactly
that question.

## Result

- `STEP-0294` is complete
- the selected next step is a detailed Circle review with implementation-grade
  notes for Phoenix

