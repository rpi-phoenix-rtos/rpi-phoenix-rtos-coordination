# Manifest: `psh_ttyopen()` Shared Failure Scope

- Date: `2026-03-21`
- Step: `STEP-0242`
- Status: `completed`

## Goal

- choose the smallest next visibility split after both fast lanes recovered
  past the old rootfs stall

## Evidence Reviewed

Source paths:

- `sources/phoenix-rtos-utils/psh/psh.c`
- `sources/phoenix-rtos-utils/psh/pshapp/pshapp.c`

Runtime evidence:

- generic and Pi 4 now both reach:
  - `psh: root ready`
  - `psh: app run`
  - `psh: run enter`
  - `psh: tty open`
  - `psh: app done`
- neither lane reaches:
  - `psh: tty ready`
  - `psh: tcsetpgrp`
  - `psh: readcmd`

Relevant source facts:

- `psh_pshapp()` sets `psh_common.consolePath = _PATH_CONSOLE`
- `psh_run()` retries `psh_ttyopen(console)` five times
- `psh_run()` returns immediately if `psh_ttyopen()` still fails
- `psh: tty ready` is printed only after a successful `psh_ttyopen()`

## Conclusion

- the next blocker is no longer rootfs registration, process spawn, or first
  user execution
- the next smallest useful step is to expose the first `psh_ttyopen()` failure
  reason without changing shell policy or console-device selection

## Selected Next Step

- add one bounded `psh_ttyopen()` failure marker that reports the first shared
  negative result on the current fast lane
