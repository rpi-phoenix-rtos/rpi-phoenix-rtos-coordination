# Manifest: Generic `/dev/tty0` Diagnostic Scope

- Date: `2026-03-20`
- Step: `STEP-0094`
- Result: `completed`

## Scope

- inspect the new generic QEMU smoke result where `pl011-tty: console ready` is absent
- choose the smallest follow-up diagnostic that narrows the path between `pl011_init()` completion and successful console registration
- stop before implementing that diagnostic

## Findings

- the runtime path definitely reaches the end of `pl011_init()`
- the runtime path does not reach successful `_PATH_CONSOLE` registration within the current smoke window
- the smallest remaining split point is successful `/dev/tty0` registration, because it sits immediately before the `_PATH_CONSOLE` registration attempt

## Selected Next Step

- add a raw `pl011-tty: tty0 ready` banner immediately after successful `/dev/tty0` registration in `pl011-tty`
- rebuild the needed generic artifacts and rerun the generic QEMU smoke lane
