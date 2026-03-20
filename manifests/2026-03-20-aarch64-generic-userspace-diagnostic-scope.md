# Manifest: Generic Userspace-Start Diagnostic Scope

- Date: `2026-03-20`
- Step: `STEP-0090`
- Result: `completed`

## Scope

- inspect the updated smoke result after packaging `dummyfs`, `pl011-tty`, and `psh`
- choose the smallest diagnostic that can distinguish “userspace not reached” from “userspace reached but silent”
- keep the next diagnostic repo-local where possible

## Upstream Repositories

- none

## Findings

- the packaged runtime image now contains the minimum expected userspace stack, but visible serial output still does not move
- a diagnostic in `user.plo` alone would not distinguish between “driver never started” and “driver started but produced no visible output”
- the smallest high-signal diagnostic is a raw PL011 banner emitted directly by `pl011-tty` immediately after the UART mapping and configuration succeed

## Notes

- this diagnostic is preferred over broader kernel or loader tracing because it stays repo-local in `phoenix-rtos-devices`
- once the runtime path is proven, the banner can be revisited and potentially removed

## Selected Next Step

- add a direct PL011 startup banner to `pl011-tty`, rebuild the needed artifacts, and rerun the generic QEMU smoke lane
