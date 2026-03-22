# STEP-0355

## Title

Scope the smallest Pi 4 `/dev/console` startup-race fix after the xHCI validation pass

## Date

2026-03-22

## Outcome

The next bounded non-xHCI step is now fixed:

- keep the just-completed xHCI work as-is
- address the Pi 4 QEMU shell regression separately as a shell-startup timing
  issue
- keep the fix as small as possible, ideally inside `psh` startup retry policy
  rather than broad namespace, kernel, or driver redesign

## Why This Step

The post-xHCI QEMU validation showed:

- generic shell smoke still passes
- Pi 4 HDMI smoke still passes
- Pi 4 shell smoke regressed before `(psh)%`

Bounded GDB on the live Pi 4 `raspi4b` lane narrowed the failure further:

- `psh_ttyopen("/dev/console")` is entered
- `open("/dev/console", O_RDWR)` is attempted five times
- each `open()` returns `-1`
- an earlier GDB pass in the same lane showed `resolve_path("/dev/console")`
  returning a non-NULL canonical path on the first attempt

That means the active failure has moved past libphoenix pathname resolution and
is now consistent with a Pi 4 startup race around live console availability,
most likely between the early shell and the `/dev` bind / console-open path.

## Validation Used For Scoping

- generic shell smoke
- Pi 4 shell smoke
- Pi 4 HDMI smoke
- bounded Pi 4 QEMU gdbstub sessions on the built `psh` ELF

## Next Step

- implement the smallest bounded `psh_ttyopen()` retry-policy refinement for
  the Pi 4 shell startup race
