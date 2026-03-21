# Manifest: Pi 4 Firmware-Artifact Scope

- Date: `2026-03-21`
- Step: `STEP-0269`
- Focus: choose the smallest no-hardware step that advances the Pi 4 boot path

## Current Inputs Already Present

- staged Pi 4 tree under:
  - `_boot/aarch64a72-generic-rpi4b/rpi4b/`
- current staged files:
  - `config.txt`
  - `kernel8.img`
  - `loader.disk`
  - `bcm2711-rpi-4-b.dtb`

## Selected Next Step

- add one helper that assembles a firmware-visible Pi 4 boot tree by combining:
  - the staged Phoenix Pi 4 files
  - a provided Raspberry Pi firmware directory containing files such as
    `start4.elf` and `fixup4.dat`

## Why This Step

- it moves the project closer to real-device boot without requiring hardware
- it stays artifact-focused and does not widen into SD-card flashing or network
  boot setup
- it uses the current build outputs rather than introducing a new boot layout
