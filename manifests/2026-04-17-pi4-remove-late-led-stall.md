# 2026-04-17 Pi 4 Remove Late LED Stall

## Summary

The first real-board retry on the UART-continuity image proved that UART
continuity was already fixed, and that the active late boot regression had
moved forward into the remaining Pi 4 GPIO42 LED diagnostics.

Coordination repo commit:

- `81192db` `phase1: remove pi4 late led stall baseline`

The new real-board log:

- `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b-uart/rpi4b-uart-20260417-205209.log`

showed all of these on real hardware:

- `AS0`
- `TR0`
- `TR1`
- `TR2`
- `TR3`
- `hal: entry EL2`
- `Phoenix-RTOS loader v. 1.21`
- `call: exec go!`
- `go: enter`
- `go: devs done`
- `go: hal done`
- `hal: jump entry`

but it never reached:

- `hal: jump irq off`
- `hal: jump exit el1`

## Root Cause

`plo/hal/aarch64/generic/hal.c` prints `hal: jump entry` and then immediately
calls `video_markKernelJump()`.

At that point the active image still executed long GPIO42 pulse groups with
busy-wait delays in:

- `plo/hal/aarch64/generic/video.c`

and the kernel still carried more Pi 4 GPIO42 pulse delays in:

- `phoenix-rtos-kernel/hal/aarch64/_init.S`
- `phoenix-rtos-kernel/hal/aarch64/hal.c`
- `phoenix-rtos-kernel/main.c`

So the active late blocker was no longer firmware handoff or UART continuity.
It was the diagnostic LED path itself.

## Source Changes

### `phoenix-rtos-project`

- commit:
  - `32d5feb` `project/rpi4b: drop late gpio42 boot diagnostics`

- `_projects/aarch64a72-generic-rpi4b/board_config.h`
  - removed:
    - `PLO_RPI_GPIO_BASE_ADDRESS`
    - `PLO_RPI_LED_DIAG`

### `plo`

- commit:
  - `876268d` `plo/aarch64: remove pi4 late handoff led pulses`

- `hal/aarch64/generic/hal.c`
  - removed the final `video_markKernelHandoff()` call
- `hal/aarch64/generic/video.c`
  - removed the Pi 4 GPIO42 LED delay/pulse machinery
  - `video_markKernelJump()` now only updates the panel progress state
  - removed the no-longer-used `video_markKernelHandoff()` definitions

### `phoenix-rtos-kernel`

- commit:
  - `5e18814b` `kernel/aarch64: remove pi4 late boot led pulses`

- `hal/aarch64/_init.S`
  - removed the stage `9` GPIO42 pulse path
- `hal/aarch64/hal.c`
  - removed the Pi 4 GPIO42 pulse machinery and the `_hal_init()` pulse
- `main.c`
  - removed the stage `11` GPIO42 pulse call

## Validation

- `rg` confirmed the active Pi 4 LED diagnostic macros and calls were removed
  from the boot path
- `git diff --check` in all touched source repos: pass
- `./scripts/rebuild-rpi4b-fast.sh --scope core --qemu-sanity`: pass
- direct Pi 4 QEMU sanity still reaches:
  - `hal: jump exit el1`
  - `A3`
  - `KLMconsole: pl011 init done`
- canonical export: pass
- FAT-aware verify: pass

## Current Image

- path:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `69fe152d093a4cd5d36d250034d7ce726b7e70f4520a4b8cec50bedc4faf74a2`

## Next Expected Real-Board Boundary

With the LED stall removed, the next retry should tell whether the board now
progresses past:

- `hal: jump entry`
- `hal: jump irq off`
- `hal: jump exit el1`
- earliest kernel output
