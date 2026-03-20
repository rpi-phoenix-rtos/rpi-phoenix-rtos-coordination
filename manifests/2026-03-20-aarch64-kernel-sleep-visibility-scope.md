# Manifest: Scope Kernel Sleep / Wakeup Programming Visibility

- Date: `2026-03-20`
- Step: `STEP-0141`
- Status: `completed`

## Goal

- define the smallest next kernel-side diagnostic step that can prove whether the stalled retry path reaches sleep enqueue and wakeup programming in the common thread manager

## Decision

The next implementation step is bounded to:

- `sources/phoenix-rtos-kernel/proc/threads.c`
- add tightly filtered, one-time kernel markers around:
  - `proc_threadNanoSleep()`
  - `_threads_programWakeup()`
  - `threads_timeintr()`
- keep all timing, retry, and scheduler semantics unchanged

## Why This Step

- `STEP-0140` proved that the first bounded retry path enters `usleep(100000)` and never returns on both QEMU lanes
- `libphoenix/unistd/sys.c` forwards that path into `nsleep()`
- `syscalls_nsleep()` then enters `proc_threadNanoSleep()`
- `proc_threadNanoSleep()`, `_proc_threadSleepAbs()`, `_threads_programWakeup()`, and `threads_timeintr()` are the narrowest shared place where we can distinguish:
  - never enqueued for sleep
  - enqueued but wakeup not programmed
  - wakeup programmed but timer interrupt never delivered

## Explicitly Deferred

- changing retry duration or replacing `usleep()`
- broad AArch64 timer backend refactoring
- changing device-startup order
- real-hardware-only debugging

## Acceptance Criteria

- the next implementation patch stays local to `proc/threads.c`
- the marker plan can distinguish sleep enqueue, wakeup programming, and missing timer interrupt delivery
- the scope preserves the generic `virt` and Pi 4 DTB-backed QEMU lanes as the current validation targets

## Selected Next Step

- implement filtered sleep / wakeup visibility markers in `proc/threads.c` and rerun both QEMU lanes
