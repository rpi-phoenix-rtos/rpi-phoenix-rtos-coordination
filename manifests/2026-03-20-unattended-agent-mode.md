# Manifest: Unattended Agent Mode Workflow

- Date: `2026-03-20`
- Step: `STEP-0018`
- Result: `completed`

## Scope

- add a documented unattended-session workflow that still preserves:
  - one active step at a time
  - explicit acceptance criteria
  - validation before closure
  - per-step upstream and coordination commits
- make that workflow visible from the main repo entry points and operator runbook

## Touched Repositories

- coordination repo only

## Touched Documents

- `AGENTS.md`
- `docs/README.md`
- `docs/execution-control.md`
- `docs/manual-operator-instructions.md`
- `docs/session-playbook.md`
- `docs/status.md`
- `docs/unattended-agent-mode.md`

## Result

- unattended mode is now explicitly defined as an opt-in workflow mode
- the agent may auto-continue across normal step boundaries only when the unattended-mode rules are satisfied
- explicit hard stop conditions are now documented so long-running sessions still stop before risky or manual-only work

## Key Rules Added

- unattended mode requires explicit user authorization
- it does not relax step-size, validation, commit, or documentation rules
- the agent may continue automatically only when the next step remains in the same safe lane and requires no manual action
- the run must stop when hardware, operator action, repeated validation failure, or unresolved architectural policy is reached

## Next Step

- resume the technical AArch64 timer track and finish scoping the first common generic timer backend step under the new unattended workflow
