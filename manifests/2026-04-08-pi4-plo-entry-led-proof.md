# 2026-04-08 Pi 4 `plo` Entry LED Proof

## Scope

Implement the smallest next no-UART board-visible proof after the already
confirmed custom Pi 4 armstub, so the next real-board retry can distinguish:

- the board reached the custom armstub but not early `plo`
- the board reached early `plo` entry and the failure is later

## Starting Point

The latest real Pi 4 hardware result on the corrected exported image was:

- ACT LED on and steady
- blank screen
- no keyboard-visible reaction

That proved the custom Pi 4 armstub now executes, so the next useful split had
to be later than the armstub boundary.

## First Attempt Rejected

The first implementation attempt put the LED-state change directly into
`plo/hal/aarch64/generic/_init.S`.

That version was intentionally not kept:

- Pi 4 QEMU still stalled at the same early kernel-handoff boundary
- the change touched the ultra-early generic assembly entry path only as a
  hypothesis probe
- under the repository rule for false-hypothesis diagnostic code, that probe
  was removed before the step was closed

## Final Change

### `phoenix-rtos-project`

`_projects/aarch64a72-generic-rpi4b/board_config.h`

- added explicit GPIO42 register and bit constants for the Pi 4 board:
  - `PLO_RPI_GPIO_GPFSEL4`
  - `PLO_RPI_GPIO_GPSET1`
  - `PLO_RPI_GPIO_GPCLR1`
  - `PLO_RPI_GPIO42_SHIFT`
  - `PLO_RPI_GPIO42_OUTPUT`
  - `PLO_RPI_GPIO42_BIT`
  - `PLO_RPI_GPIO42_MASK`
- upstream commit:
  - `e061ca4`

### `plo`

`_startc.c`

- added a tiny board-local helper guarded by the Pi 4 GPIO macros
- the helper:
  - keeps GPIO42 configured as output
  - drives GPIO42 low
- called that helper immediately after clearing `.bss`, before init-array
  constructors and before `main()`
- upstream commit:
  - `a55b057`

This keeps the proof:

- later than the custom armstub
- still early in `plo`
- independent of UART, HDMI, timer interrupts, or keyboard input

## Intended Board Semantics

With the new image:

- ACT LED stays on:
  the board reached the custom armstub but did not reach `_startc()`
- ACT LED turns on first, then ends the attempt off:
  the board reached `_startc()` and the remaining failure is later in early
  `plo`

## Validation

### Build

- rebuilt `aarch64a72-generic-rpi4b` in `phoenix-dev`

### Fast lanes

- Pi 4 QEMU shell log still reaches:
  - `plo`
  - `go!`
  - `hal_exitToEL1()`
  - kernel `_start` markers `KLM`
- Pi 4 QEMU HDMI smoke still does not reach the later black runtime frame;
  it remains on the earlier loader-stage background
- this step did not resolve or worsen that existing Pi 4 QEMU boundary
- because the purpose of this step is a real-board LED split rather than a new
  runtime feature, the step was accepted with:
  - clean build
  - refreshed boot artifacts
  - re-verified exported SD image

### Artifact chain

- reran:
  - `scripts/assemble-rpi4b-bootfs.sh`
  - `scripts/assemble-rpi4b-bootfs-img.sh`
  - `scripts/assemble-rpi4b-sdimg.sh`
  - `scripts/export-rpi4b-sdimg.sh`
  - `scripts/verify-rpi4b-sdimg.sh`

## Current Artifact

- image:
  `/Users/witoldbolt/phoenix-rpi/artifacts/rpi4b/rpi4b-sd.img`
- SHA-256:
  `acea299fb225edb0293b4d022b9b19d984fe51627a168bd69c403442590b757d`

## Next Step

Flash the refreshed SD image and retry the real Pi 4 board boot. Use the final
ACT LED state to choose the next smallest early-boot implementation step.
