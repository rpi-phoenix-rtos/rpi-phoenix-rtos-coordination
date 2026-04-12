# Current Step

## Metadata

- Step ID: `STEP-0473`
- Title: Await the next Pi 4 board retry on the hardened kernel-entry image
- Status: `in_progress`
- Date: `2026-04-12`
- Milestone / phase: `Phase 1`

## Objective

- verify that the unified 115200 baud UART lane provides readable kernel output
- confirm that the ACT LED turns solid ON, signaling successful kernel entry
- observe the first Phoenix kernel banner or identify the next failure site (e.g. DTB parsing, MMU setup)

## Scope

In scope:
- analysis of the next real-device UART capture at 115200 baud
- analysis of the next ACT LED video to confirm the heartbeat signal
- classification of the resulting boot stage reached

Out of scope:
- broad code changes before seeing the next log
- unrelated peripheral drivers

## Acceptance Criteria

- a 115200 baud UART log is captured through the kernel entry boundary
- the ACT LED behavior is documented (expected: solid ON after plo jump)
- the kernel banner is seen OR a new early-boot crash is pinpointed

## Validation Plan

- analyze the next `rpi4b-uart-YYYYMMDD-HHMMSS.log`
- decode the next `IMG_XXXX.mov`
- use the evidence to choose the next boot-hardening or driver-integration step

## Rollback / Baseline

- latest hardened image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  (SHA-256: `19928dd6cdf7fcdd6214aa9289cc38b3d232f5d29536bb9a9d4a95cdd86353db`)

## Notes

- the board is confirmed to reach `plo` kernel jump (3 squares on HDMI)
- the 115200 baud unification via `init_uart_baud` removes the need for dual-profile capture
- the ACT LED heartbeat provides a high-confidence proof of kernel entry independent of UART
