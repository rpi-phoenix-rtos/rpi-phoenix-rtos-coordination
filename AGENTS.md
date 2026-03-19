# Phoenix RTOS on Raspberry Pi: Agent Guide

## Purpose

This repository is the working knowledge base and execution scaffold for building a full Phoenix RTOS port to:

- Raspberry Pi 4 Model B first
- Raspberry Pi 5 later, after Pi 4 is stable

The repository currently contains documentation and agent playbooks, not the implementation itself.

## Mandatory Reading Order

Before making code changes in future sessions, read these files in order:

1. `docs/status.md`
2. `docs/implementation-dossier.md`
3. `docs/repository-work-breakdown.md`
4. `docs/git-repository-strategy.md`
5. `docs/host-macos-apple-silicon.md`
6. `docs/manual-operator-instructions.md`
7. `docs/code-quality-and-upstreaming.md`
8. `docs/execution-control.md`
9. `tracking/current-step.md`
10. `docs/platforms/raspberry-pi-4.md`
11. `docs/testing-automation.md`
12. `docs/session-playbook.md`
13. `docs/source-artifacts.md`

Read `docs/platforms/raspberry-pi-5.md` when the task touches Pi 5 or RP1.
Read `skills/README.md` when choosing a local project skill.

## Local Skills

This repository defines project-local skills under `skills/`. They are not part of Codex's global skill registry, so agents must open them manually when relevant.

Use them as follows:

- `skills/phoenix-rpi-bringup/SKILL.md`
  Use for kernel, loader, DTB, MMU, timer, interrupt, console, storage, PCIe, USB, networking, or driver bring-up tasks.

- `skills/phoenix-rpi-hw-test/SKILL.md`
  Use for real-device testing, UART capture, power control, image flashing, smoke loops, soak tests, and lab automation.

- `skills/phoenix-rpi-knowledge-base/SKILL.md`
  Use when updating docs, indexing new findings, importing external references, or preserving context between long sessions.

- `skills/phoenix-rpi-regression-analysis/SKILL.md`
  Use when a new change breaks boot, regressions appear in hardware tests, or a discrepancy between QEMU and real hardware needs diagnosis.

## Project Rules

- Do not start with Raspberry Pi 5 unless the task explicitly requires Pi 5-specific preparation or documentation.
- Prefer native Phoenix bring-up over UEFI-assisted boot for the final design.
- A temporary firmware-assisted or state-inheriting debug path is acceptable only if it is clearly documented as transitional.
- Treat Raspberry Pi firmware behavior, EEPROM settings, QEMU support status, and Linux/BSD support matrices as temporally unstable. Re-check online before depending on them.
- Keep the Phoenix boot model intact where possible:
  `Raspberry Pi firmware -> plo -> syspage -> kernel -> user-space servers/drivers`
- Work in narrow, phase-gated steps. Do not advance to the next major step until the current step has explicit success criteria, validation evidence, and documentation updates.
- There must be only one active implementation step at a time, tracked in `tracking/current-step.md`.
- After every successful implementation step, commit the relevant changes in every touched upstream repository and then commit the coordination-repo documentation or manifest update that records the tested integration state.
- Manage Phoenix as multiple sibling git repositories, not as a rewritten monorepo. Keep repository coordination in this repo through documentation and manifest files.
- On this workstation, treat Linux as the authoritative build and emulation environment. Use macOS natively for coordination, editing, and hardware control; use a Linux VM for Phoenix builds and most QEMU runs unless a task is explicitly documented as safe on the host.
- Optimize all future code for readability and upstreamability: keep changes small, consistent with nearby Phoenix code, warning-clean, and free of gratuitous formatting churn.
- Do not bury important findings in chat history. Update the docs when new constraints, addresses, boot flows, test commands, or risks are discovered.
- If context becomes tight after a long session, re-read at least `docs/status.md`, `docs/repository-work-breakdown.md`, `docs/testing-automation.md`, and the relevant platform note before proceeding.

## Documentation Maintenance Rules

- Update `docs/status.md` after every substantial implementation session.
- Update `docs/manual-operator-instructions.md` whenever a new manual prerequisite, physical setup step, bootloader action, recovery procedure, or operator-only task is discovered.
- Update `docs/code-quality-and-upstreaming.md` whenever a new subsystem-specific style rule, review preference, or reliable quality check becomes known.
- Update `tracking/current-step.md` before starting implementation code, and update `tracking/step-history.md` when a step is closed.
- Update `docs/source-artifacts.md` whenever a new upstream document, repository, driver, or code path becomes important.
- When a document contains a statement that may age quickly, add an explicit `Re-verify:` note.
- Prefer citing exact upstream repo paths and official documentation URLs over vague prose.

## Current Strategic Position

- Phoenix already has reusable AArch64, `plo`, filesystem, and test infrastructure.
- The current AArch64 implementation is still heavily `zynqmp`-shaped and must be generalized before the Raspberry Pi port is clean.
- Pi 4 is the first target because Pi 5 introduces RP1 behind PCIe and is materially more complex.
- QEMU is useful for CPU/MMU/boot-path iteration but is not sufficient as the only validation target for Raspberry Pi peripherals.

## Expected Future Repository Contents

As implementation starts, expect future agents to add:

- target definitions for Raspberry Pi 4 and later Raspberry Pi 5
- AArch64 platform support in `phoenix-rtos-kernel` and `plo`
- device drivers in `phoenix-rtos-devices`
- test target integrations in `phoenix-rtos-tests`
- build and image generation glue in `phoenix-rtos-build` and `phoenix-rtos-project`

Until then, this repository remains the planning and execution guide.
