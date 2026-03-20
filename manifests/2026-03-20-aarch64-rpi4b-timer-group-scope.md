# Manifest: Pi 4 Timer Group Scope

- Date: `2026-03-20`
- Step: `STEP-0205`
- Status: `completed`

## Goal

- select the smallest post-`PPISR` follow-up on the Pi 4 timer-to-GIC seam

## Evidence Reviewed

Current generic `virt` lane evidence:

- `gic: timer handler set grp 0 en 1`
- `gtimer: pending 1`
- `gtimer: ppi pending 0`
- `gic: timer dispatch`

Current Pi 4 A72 patched-lane evidence:

- `gic: timer handler set grp 1 en 1`
- `gtimer: pending 0`
- `gtimer: ppi pending 0`
- no `gic: timer dispatch`

Cross-check from Circle:

- Pi 4 timer identity and IRQ `30` already match the expected physical timer path
- Circle confirms the current Phoenix timer-source choice is not the likely gap

## Selected Next Experiment

- make the timer IRQ group board-overridable and force the Pi 4 A72 lane to
  Group 0 for one bounded experiment

## Why This Is The Right Next Step

- it changes exactly one visible runtime variable
- it stays on the current timer-to-GIC seam
- it does not require scheduler, VM, or DTB changes
- it directly tests the only strong remaining difference currently visible in
  the logs: generic timer registration ends in Group 0 while Pi 4 ends in
  Group 1

## Selected Implementation Shape

- use `board_config.h` to avoid a board-specific hardcode in common kernel code
- keep the default unchanged for other targets
- use the override only for the Pi 4 A72 lane in this experiment

## Selected Next Step

- implement the bounded Pi 4 timer-group override experiment and validate it on
  the generic `virt` lane and the Pi 4 A72 patched lane
