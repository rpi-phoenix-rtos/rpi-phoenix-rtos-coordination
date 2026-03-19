---
name: phoenix-rpi-hw-test
description: Use when designing, extending, or running the real-device and emulator test loop for the Phoenix RTOS Raspberry Pi port, including UART capture, flashing, board power control, smoke tests, soak tests, and failure classification.
---

# Phoenix RPi Hardware Test

Use this skill when the task is about build/test automation or diagnosing test results.

## Read First

1. `docs/status.md`
2. `docs/testing-automation.md`
3. `docs/host-macos-apple-silicon.md`
4. `docs/source-artifacts.md`

Read the relevant platform file if the failing test is platform-specific.

## Workflow

1. Separate host/build failures from DUT/runtime failures.
2. Run the fastest reliable validation first.
3. Prefer this order:
   - host/static checks
   - generic emulator
   - Raspberry Pi emulator
   - real hardware smoke
   - real hardware extended tests
4. Prefer Pi 4 network boot as the steady-state hardware loop once the lab is stable enough; keep SD or USB media as the fallback recovery path.
5. On this workstation, keep USB serial and power control on the macOS host unless there is a proven reason to move them.
6. Preserve raw UART logs.
7. Summarize failures with a clear class:
   - build
   - image assembly
   - firmware load
   - `plo`
   - kernel boot
   - shell startup
   - runtime regression
   - lab infrastructure

## Key Existing Phoenix Test Assets

- `phoenix-rtos-tests/runner.py`
- `phoenix-rtos-tests/trunner/dut.py`
- `phoenix-rtos-tests/trunner/harness/plo.py`
- `phoenix-rtos-tests/trunner/host.py`

Extend them instead of creating a parallel framework unless there is a strong reason not to.

## Minimum Smoke Test

Every new boot path should at least verify:

1. loader prompt or loader handoff
2. kernel banner
3. shell prompt
4. one file/command sanity check
5. reboot

## Mandatory Logging

Keep:

- build identifier
- image identifier or hash
- UART log
- exact firmware/config used
- board model/revision if known

## Documentation Rule

If a test exposes a recurring failure mode, add it to the docs so the next agent does not rediscover it.
