---
name: phoenix-rpi-regression-analysis
description: Use when diagnosing a boot regression, driver regression, or hardware-vs-emulator mismatch in the Phoenix RTOS Raspberry Pi port, especially when the failure spans plo, kernel, DTB parsing, timers, interrupts, storage, networking, PCIe, or USB.
---

# Phoenix RPi Regression Analysis

Use this skill when something that used to work stops working, or when two environments disagree.

## Read First

1. `docs/status.md`
2. `docs/testing-automation.md`
3. `docs/host-macos-apple-silicon.md`
4. the relevant platform document
5. `docs/source-artifacts.md`

## Workflow

1. Reproduce the failure with the smallest possible test.
2. Classify the failing stage:
   - image/firmware
   - `plo`
   - kernel early boot
   - shell startup
   - runtime driver
3. Compare against the last known good behavior.
4. Check whether the issue is:
   - code regression
   - changed firmware behavior
   - changed QEMU behavior
   - lab/infrastructure problem
5. Prefer isolating one subsystem at a time:
   - DTB parsing
   - console
   - timer
   - interrupts
   - memory map
   - storage
   - PCIe
   - USB
   - network
6. If the failure is time-sensitive or firmware-dependent, re-check online sources before concluding.

## Special Attention Areas

- Pi 4:
  DMA constraints, PCIe quirks, VL805/xHCI chain, GENET behavior

- Pi 5:
  RP1 preserved-state illusions, PCIe reset behavior, MSI-X routing, firmware mailbox dependencies

## Documentation Rule

When the root cause is found, update the docs with:

- symptom
- trigger
- root cause
- durable mitigation
