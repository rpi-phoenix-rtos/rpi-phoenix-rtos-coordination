# Current Step

## Metadata

- Step ID: `STEP-0142`
- Title: Instrument kernel sleep / wakeup visibility
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- determine whether the stalled retry path reaches sleep enqueue, wakeup programming, and timer-interrupt delivery inside the common thread manager

## Scope

In scope:

- `sources/phoenix-rtos-kernel/proc/threads.c`
- add tightly filtered, one-time kernel markers for:
  - the first relevant `proc_threadNanoSleep()` request
  - the corresponding `_threads_programWakeup()` programming path
  - the first `threads_timeintr()` delivery point
- keep the current retry timing and scheduler behavior unchanged
- validate on the generic `virt` lane first, then on the Pi 4 DTB-backed `raspi4b` lane

Out of scope:

- broad timer backend refactoring
- changing `pl011-tty` retry semantics
- changing device-start order
- changing scheduler policy
- real-hardware-only validation
- Pi 5 or RP1 work
- `phoenix-rtos-tests` integration

## Expected Repositories

- `sources/phoenix-rtos-kernel`
- coordination repo

## Expected Files Or Subsystems

- `sources/phoenix-rtos-kernel/proc/threads.c`
- relevant generic and Pi 4 QEMU smoke notes
- manifests and tracking updates for this implementation step

## Acceptance Criteria

- the generic lane exposes whether the blocked retry path reaches sleep enqueue
- the generic lane exposes whether `_threads_programWakeup()` runs for that sleep
- the generic lane exposes whether a corresponding timer interrupt reaches `threads_timeintr()`
- neither QEMU lane regresses from current known-good boot output

## Validation Plan

- Review:
  confirm the patch stays localized to `proc/threads.c` and only adds filtered visibility markers
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
  `manifests/2026-03-20-aarch64-pl011-retry-wake-visibility.md`

## Notes

- Risks:
  avoid turning early-boot sleep instrumentation into broad scheduler logging churn
- Dependencies:
  completed `STEP-0141` scope decision
- User-visible control point before next step:
  after this step lands, the next bounded move should come from direct evidence about whether the common sleep path fails before wakeup programming or after timer arming
