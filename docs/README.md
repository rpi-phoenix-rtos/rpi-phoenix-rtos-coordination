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

- `host-macos-apple-silicon.md`
  Host-environment strategy for this specific workstation, including macOS-vs-Linux-VM task split and setup guidance.

- `manual-operator-instructions.md`
  Human-facing runbook for all currently known manual prerequisites, physical setup steps, and operator-provided inputs.

- `code-quality-and-upstreaming.md`
  Coding-style, readability, warning, review, and upstreamability rules for future implementation work.

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

- `raspberry-pi-device-tree-reference.md`
  Raspberry Pi-specific DTS, firmware-DTB, alias, overlay, and UART notes that
  directly affect Pi 4 bring-up and DTB debugging.

- `raspberry-pi-bare-metal-reference-notes.md`
  External bare-metal Pi 4 reference findings from `rpi4-osdev` and Circle,
  with notes on what is immediately useful and what should not be cargo-culted
  into the Phoenix port.

- `circle-reference-review.md`
  Detailed implementation-oriented review of Circle's Pi 4 mailbox/framebuffer
  and USB-keyboard paths, with explicit sequencing guidance for Phoenix.

## Skill Playbooks

The `skills/` directory contains local project playbooks for future agents. Read [`../skills/README.md`](../skills/README.md) first, then open the specific `SKILL.md` that matches the task.

## Tracking

Implementation progress is tracked in the `tracking/` directory:

- `tracking/current-step.md`
- `tracking/step-history.md`
- `tracking/step-template.md`
