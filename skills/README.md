# Local Skill Index

These project-local skills are lightweight playbooks for future implementation sessions. Open the relevant `SKILL.md` manually when the task matches.

## Available Skills

- `skills/phoenix-rpi-bringup/SKILL.md`
  Use for implementation and review of low-level bring-up, AArch64 HAL work, `plo`, DTB parsing, timers, interrupts, storage, networking, PCIe, USB, and board drivers.

- `skills/phoenix-rpi-hw-test/SKILL.md`
  Use for real-device testing, UART capture, power-control flow, flashing, smoke tests, soak tests, and regression automation on actual hardware.

- `skills/phoenix-rpi-knowledge-base/SKILL.md`
  Use for documentation maintenance, indexing new findings, preserving context, and enriching the long-lived knowledge base.

- `skills/phoenix-rpi-regression-analysis/SKILL.md`
  Use when boot regressions, hardware-only failures, or QEMU versus hardware discrepancies need structured diagnosis.

## Selection Guidance

Choose the smallest skill that matches the main output:

- code change:
  `phoenix-rpi-bringup`
- test execution and lab debugging:
  `phoenix-rpi-hw-test`
- docs and source indexing:
  `phoenix-rpi-knowledge-base`
- failure diagnosis:
  `phoenix-rpi-regression-analysis`

For mixed sessions, use more than one only if the boundary is clear.
