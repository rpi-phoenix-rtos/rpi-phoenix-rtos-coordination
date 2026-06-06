# Documentation Index

This directory is the long-lived knowledge base for the Phoenix RTOS Raspberry Pi port.

## Core Documents

- `status.md`
  Current project state, next milestones, and session handoff notes.

- `implementation-dossier.md`
  Main technical plan and architecture decisions for the port.

- `repository-work-breakdown.md`
  Repo-by-repo implementation map, likely change locations, and recommended execution order.

- `git-repository-strategy.md`
  Local multi-repo workflow, branch and commit policy, and integration manifest discipline.

- `linux-host-bootstrap.md`
  Primary host setup guide for the current Linux x86-64 dev box —
  toolchain, UART capture, dnsmasq+TFTP for netboot, meross-plug
  power control. Read this first.

- `host-macos-apple-silicon.md`
  Legacy host strategy for the macOS+Lima workstation the project
  ran on before the 2026-05-20 migration. Consult only when working
  on that machine.

- `manual-operator-instructions.md`
  Human-facing runbook for all currently known manual prerequisites, physical setup steps, and operator-provided inputs.

- `code-quality-and-upstreaming.md`
  Coding-style, readability, warning, review, and upstreamability rules for future implementation work.

- `known-limits.md`
  Hardware-bounded issues that affect mainline Linux equally —
  Phoenix cannot fix these in software. Read before re-investigating
  the USB VL805 or WiFi BCM43455 walls; cited Linux/forum sources
  inline. Companion to `TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` which
  is for Phoenix-internal shortcuts only.

- `execution-control.md`
  Process rules for step-by-step tracking, acceptance criteria, control points, and reporting.

- `unattended-agent-mode.md`
  Additional rules for explicitly authorized long-running unattended sessions, including auto-continue conditions and stop conditions.

- `platforms/raspberry-pi-4.md`
  Pi 4-specific boot, hardware, driver, and testing notes.

- `platforms/raspberry-pi-5.md`
  Pi 5-specific notes, focused on BCM2712, RP1, PCIe, and migration sequencing.

- `testing-automation.md`
  Build/test lab design, QEMU strategy, real-device automation, and regression workflow.

- `session-playbook.md`
  Operating procedure for long multi-session implementation work, including context-recovery guidance.

- `source-artifacts.md`
  Important links and exact upstream source paths.

- `publish-map.md`
  Canonical map of the coordination repo, Phoenix fork remotes, and the
  published long-running branch names used for future pushes and PRs.

- `raspberry-pi-device-tree-reference.md`
  Raspberry Pi-specific DTS, firmware-DTB, alias, overlay, and UART notes that
  directly affect Pi 4 bring-up and DTB debugging.

- `raspberry-pi-bare-metal-reference-notes.md`
  External bare-metal Pi 4 reference findings from `rpi4-osdev` and Circle,
  with notes on what is immediately useful and what should not be cargo-culted
  into the Phoenix port.

- `raspberry-pi-4-low-level-reference-survey.md`
  Consolidated low-level Pi 4 boot, MMIO, GIC, timer, DTB, GPIO, and source
  ranking notes drawn from official docs, Linux DTS, Circle, NuttX, and the
  curated external bare-metal references.

- `circle-reference-review.md`
  Detailed implementation-oriented review of Circle's Pi 4 mailbox/framebuffer
  and USB-keyboard paths, with explicit sequencing guidance for Phoenix.

- `pi4-first-hardware-trial.md`
  Focused first-board-trial checklist and result template for the current Pi 4
  HDMI plus USB-keyboard image.

## Skill Playbooks

The `skills/` directory contains local project playbooks for future agents. Read [`../skills/README.md`](README.md) first, then open the specific `SKILL.md` that matches the task.

## Tracking

Implementation progress is tracked in the `tracking/` directory:

- `tracking/current-step.md`
- `tracking/step-history.md`
- `tracking/step-template.md`
