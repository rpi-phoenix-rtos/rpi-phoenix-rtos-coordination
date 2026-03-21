# Manifest: `/dev/console` Shared Path Scope

- Date: `2026-03-21`
- Step: `STEP-0244`
- Status: `completed`

## Goal

- choose the smallest next step after both fast lanes proved
  `open("/dev/console") -> -ENOENT`

## Evidence Reviewed

Source paths:

- `sources/libphoenix/unistd/file.c`
- `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`
- `sources/phoenix-rtos-utils/psh/psh.c`
- `sources/phoenix-rtos-utils/psh/pshapp/pshapp.c`

Key findings:

- `psh_pshapp()` uses `_PATH_CONSOLE`
- `psh_ttyopen()` now proves the failing operation is `open()`, not `isatty()`
- `pl011-tty` already registers `_PATH_CONSOLE`, not a bare `"console"` name
- `create_dev()` strips a `/dev` prefix before sending `mtCreate` to `devfs`,
  so `create_dev(&oid, _PATH_CONSOLE)` should already target the intended
  `console` node under the `devfs` namespace

## Conclusion

- the next blocker is not a trivial wrong-string bug in `psh` or `pl011-tty`
- the smallest useful next split is to expose the first shared
  `lookup("/dev/console")` result inside the kernel path-resolution path

## Selected Next Step

- add one bounded kernel-side trace for the first `psh` lookup of
  `/dev/console`
