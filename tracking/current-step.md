# Current Step

## Metadata

- Step ID: `STEP-0018`
- Title: Add unattended long-running session workflow
- Status: `in_progress`
- Date: `2026-03-20`
- Milestone / phase: `Phase 1`

## Objective

- add explicit unattended-session workflow rules so future long-running overnight sessions can continue step-by-step without waiting at every normal step boundary

## Scope

In scope:

- add a dedicated unattended-mode workflow document
- wire unattended mode into the main repo reading and control documents
- document hard stop conditions for overnight sessions
- update the operator runbook with unattended-session prerequisites
- keep this as a coordination-repo workflow step only

Out of scope:

- technical Phoenix implementation work
- new upstream repo code changes

## Expected Repositories

- coordination repo

## Expected Files Or Subsystems

- `AGENTS.md`
- `docs/execution-control.md`
- `docs/session-playbook.md`
- `docs/manual-operator-instructions.md`
- `docs/unattended-agent-mode.md`
- tracking files and manifest updates

## Acceptance Criteria

- unattended mode is explicitly documented and opt-in
- the repo now defines when the agent may auto-continue and when it must stop
- the operator runbook documents the known prerequisites for unattended runs

## Validation Plan

- Build:
  not applicable
- Emulator:
  not applicable
- Hardware:
  not applicable

## Rollback / Baseline

- Known-good manifest or commit set:
  `manifests/2026-03-20-aarch64-timer-sysreg-helpers.md`

## Notes

- Risks:
  the workflow change must not weaken the existing step-by-step control model
- Dependencies:
  existing execution-control and session-playbook docs
- User-visible control point before next step:
  resume the technical AArch64 timer track only after the unattended workflow is committed cleanly
