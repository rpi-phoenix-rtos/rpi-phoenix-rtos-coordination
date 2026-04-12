# Current Step

## Metadata

- Step ID: `STEP-0475`
- Title: Await the next Pi 4 board retry with extended HDMI visibility
- Status: `in_progress`
- Date: `2026-04-12`
- Milestone / phase: `Phase 1`

## Objective

- verify that the extended HDMI logs (libklog status, tty0 registration) are visible on the screen
- confirm the 10-blink userspace heartbeat on the ACT LED
- capture a successful UART log using the 103448 baud rate (if 115200 remains broken)
- observe the first Phoenix shell (psh) output on HDMI or UART

## Scope

In scope:
- analysis of the next real-device trial results
- verification of HDMI-mirrored logs
- verification of userspace-driven LED signals

Out of scope:
- broad kernel or driver changes before seeing the next feedback

## Acceptance Criteria

- the ACT LED blinks 10 times (userspace started)
- HDMI console shows more than just the initial banner (libklog/tty progress)
- a readable UART log is captured (either 115200 or 103448)

## Validation Plan

- analyze the next log and video/screenshot
- use the results to choose between baud-rate refinement, devfs/dummyfs debugging, or shell integration

## Rollback / Baseline

- latest diagnostic image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
  (SHA-256: `07a81008c414f2c5f67743bf8a6bd27b9f857e40a0237cea0faa8b66735ff799`)

## Notes

- the board is confirmed to reach userspace
- HDMI is now the primary high-confidence diagnostic channel
- the 103448 baud rate choice by firmware is currently the leading theory for "broken" 115200 logs
