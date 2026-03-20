# Manifest: `psh` First User-Schedule Visibility Scope

- Date: `2026-03-21`
- Step: `STEP-0234`
- Status: `completed`

## Goal

- choose the smallest below-stdio hook that can prove whether the spawned
  `psh` process ever reaches first user execution

## Evidence Reviewed

Current cross-lane result:

- both generic and Pi 4 spawn `psh`
- neither lane prints any `psh:` marker within 30 seconds

Relevant source paths reviewed:

- `sources/phoenix-rtos-kernel/proc/threads.c`
- `sources/phoenix-rtos-kernel/proc/process.c`

Important local hook:

- `_threads_schedule()` in `proc/threads.c` selects the next thread, switches to
  its process address space, and finally calls `hal_cpuRestore(context, selCtx)`
- just before that restore, the kernel still has:
  - `selected->process`
  - `selected->process->path`
  - the final user / supervisor distinction through
    `hal_cpuSupervisorMode(selCtx)`

## Selected Next Implementation Step

- add one bounded one-time marker in `_threads_schedule()` only when:
  - `selected->process != NULL`
  - `selected->process->path` is `psh`
  - `hal_cpuSupervisorMode(selCtx) == 0`
- emit the marker immediately before `hal_cpuRestore(context, selCtx)`

## Why This Is The Right Next Step

- it sits below shell-visible stdio
- it avoids broad scheduler tracing by filtering to one process name and one
  transition point
- it can distinguish:
  - `psh` never reaches user mode at all
  - `psh` does reach user mode and the silence is later inside user-space

## Selected Next Step

- implement bounded `psh` first-user-schedule visibility in:
  - `sources/phoenix-rtos-kernel/proc/threads.c`
