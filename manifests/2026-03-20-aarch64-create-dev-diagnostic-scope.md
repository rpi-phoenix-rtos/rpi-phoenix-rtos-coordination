# Manifest: `create_dev()` Diagnostic Scope

- Date: `2026-03-20`
- Step: `STEP-0126`
- Status: `completed`

## Goal

- choose the next smallest diagnostic step after proving that both QEMU lanes stop inside the first `create_dev("/dev/tty0")` call

## Evidence

- `pl011-tty` now emits `pl011-tty: register tty0` on both lanes
- neither lane emits `tty0 ready` or `tty0 failed`
- therefore the current live boundary is inside the shared `create_dev()` path rather than in the driver-local post-call handling

## Conclusion

- the next step should instrument the shared `create_dev()` flow in `libphoenix`
- the diagnostic should distinguish:
  - repeated `lookup("devfs", ...)` retries
  - blocking during directory creation
  - blocking during final device-node creation

## Selected Next Step

- add a bounded, high-signal `create_dev()` diagnostic step in the shared path used by both lanes
- prefer diagnostics that can surface on the existing serial captures with minimal code churn
