---
name: phoenix-rpi-bringup
description: Use when implementing or reviewing low-level Phoenix RTOS bring-up work for Raspberry Pi 4 or Raspberry Pi 5, including plo, AArch64 HAL work, DTB parsing, timers, interrupts, SMP, storage, PCIe, USB, networking, and board-specific drivers.
---

# Phoenix RPi Bring-Up

Use this skill when making implementation changes for the Raspberry Pi port.

## Read First

1. `docs/status.md`
2. `docs/implementation-dossier.md`
3. `docs/host-macos-apple-silicon.md`
4. `docs/platforms/raspberry-pi-4.md`
5. `docs/platforms/raspberry-pi-5.md` only if the task touches Pi 5
6. `docs/source-artifacts.md`

## Workflow

1. Confirm whether the task belongs to Pi 4 or Pi 5.
2. Prefer the smallest change that improves native Phoenix bring-up or testability.
3. Keep the final design aligned with:
   `Raspberry Pi firmware -> plo -> syspage -> kernel -> user-space drivers`
4. If the current code is too `zynqmp`-specific, fix that before adding Raspberry Pi-specific hacks.
5. On this workstation, prefer Linux VM execution for builds and normal QEMU work.
6. Validate first in a fast emulator lane if possible, then on real hardware.
7. Update the docs if you discover new constraints, addresses, boot requirements, or regressions.

## Hard Rules

- Do not start major Pi 5 work before Pi 4 is stable unless the task is explicitly Pi 5-only preparation.
- Do not rely permanently on UEFI or firmware-preserved hardware state.
- Do not close hardware tasks based only on QEMU results.
- Do not leave important architecture findings only in chat history.

## High-Priority Bring-Up Order

### Pi 4

1. generic AArch64 cleanup
2. DTB parser generalization
3. `plo` UART boot
4. kernel single-core boot
5. architectural timer
6. storage
7. GPIO/I2C/SPI
8. Ethernet
9. PCIe
10. xHCI

### Pi 5

1. reuse common AArch64 work
2. basic boot and UART
3. PCIe path to RP1
4. RP1 wrapper
5. RP1 low-speed I/O
6. RP1 Ethernet
7. RP1 xHCI

## Useful Reference Types

- Phoenix kernel and loader AArch64 source paths listed in `docs/source-artifacts.md`
- Raspberry Pi official docs for boot/config behavior
- Raspberry Pi Linux DT and driver source
- FreeBSD/NetBSD source for comparative behavior

## When To Stop And Document

Update the docs if you learn any of these:

- required `config.txt` changes
- DTB node assumptions
- interrupt numbers
- timer source choices
- DMA constraints
- memory layout constraints
- firmware-dependency workarounds
