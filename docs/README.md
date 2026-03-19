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

## Skill Playbooks

The `skills/` directory contains local project playbooks for future agents. Read [`../skills/README.md`](../skills/README.md) first, then open the specific `SKILL.md` that matches the task.

## Tracking

Implementation progress is tracked in the `tracking/` directory:

- `tracking/current-step.md`
- `tracking/step-history.md`
- `tracking/step-template.md`
