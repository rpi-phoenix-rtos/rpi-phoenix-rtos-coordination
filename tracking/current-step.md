# Current Step

## Metadata

- Step ID: `STEP-0146`
- Title: Instrument GIC timer registration / dispatch visibility
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- determine whether the selected common AArch64 timer IRQ is actually registered in GICv2 and whether it is ever dispatched before control should reach `threads_timeintr()`

## Scope

In scope:

- `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- add tightly filtered, one-time markers for:
  - timer-handler registration
  - first dispatch of `hal_timerIrq()`
- keep timer policy, IRQ routing policy, and scheduler behavior unchanged
- validate on the generic `virt` lane first, then on the Pi 4 DTB-backed `raspi4b` lane

Out of scope:

- timer-source policy changes
- broad GICv2 refactoring
- changing `pl011-tty` retry semantics
- changing scheduler policy
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- `sources/phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
- relevant generic and Pi 4 QEMU smoke notes
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the generic lane exposes that timer-handler registration reaches `hal_interruptsSetHandler()`
- the generic lane exposes whether the selected timer IRQ is ever dispatched
- the resulting output distinguishes “no registration” from “registered but never dispatched”
- neither QEMU lane regresses from current known-good boot output

## Validation Plan

- Review:
  confirm the patch stays localized to `hal/aarch64/interrupts_gicv2.c` and only adds filtered GIC timer markers
- Build:
  rebuild the affected generic and Pi 4 project lanes in `phoenix-dev`
- Emulator:
  rerun:
  - generic `virt`
  - Pi 4 DTB-backed `raspi4b`
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-gtimer-visibility.md`

## Notes

- Risks:
  avoid widening a bounded timer-dispatch diagnostic into a broader GIC refactor
- Dependencies:
  completed `STEP-0145` scope decision
- User-visible control point before next step:
  after this step lands, the next bounded move should come from direct evidence about GIC registration versus missing dispatch
