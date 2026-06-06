# Unattended Agent Mode

This document defines how the project should operate when the user explicitly authorizes a long-running unattended session.

The goal is:

- keep the same step-by-step discipline
- keep the same commit, manifest, and tracking boundaries
- allow the agent to continue across normal step boundaries without waiting for the user
- stop automatically before the work becomes risky, ambiguous, or dependent on manual intervention

Unattended mode is a workflow mode, not permission to widen scope.

## 1. Activation Rule

Unattended mode is active only when the user explicitly authorizes it.

Examples:

- "continue unattended"
- "keep working overnight"
- "do not stop at normal step boundaries unless blocked"

Without explicit authorization, the default rule remains:

- stop at normal step boundaries
- present the next proposed step before continuing

## 2. What Does Not Change

Unattended mode does not relax the core control rules.

These still apply:

- only one active step at a time
- every step must have scope, out-of-scope, acceptance criteria, and validation
- every successful step must end with:
  - commits in each touched upstream repo
  - a coordination-repo update
  - a manifest or durable closure record
- important findings must be written to the repo docs, not left only in chat

## 3. What Changes In Unattended Mode

When unattended mode is active, the agent may automatically continue to the next small step after closing the current one if all of the following are true:

1. the current step was validated and committed cleanly
2. the next step stays in the same project phase or milestone
3. the next step stays in the same board focus:
   - common AArch64 work
   - Pi 4 work
   - Pi 5 work
4. the next step does not require manual operator action
5. the next step does not require real hardware access if the unattended run is currently in a no-hardware lane
6. the next step has a clear non-hardware validation lane
7. the next step can be explained in a short paragraph and reviewed as one concept

If those conditions are not met, the unattended run must stop or fall back to a planning-only step.

## 4. Preferred Unattended Progress Pattern

The preferred overnight pattern is:

1. close the current step cleanly
2. if the next step is obvious and safe:
   - create the next step record
   - implement it
   - validate it
   - commit it
3. if the next step is not obvious but still safe to analyze:
   - create a planning step
   - write the result into the tracking docs and manifest
   - then continue into the next implementation step only if it is now tightly bounded

When in doubt, choose a planning step instead of a speculative code step.

## 5. Hard Stop Conditions

An unattended run must stop when any of the following becomes true:

- manual operator action is required
- real hardware access is required and no hardware lane is already active and documented
- validation fails twice for the same step without a clear fix
- the next step would widen across milestones, boards, or major subsystem families without an explicit planning step
- the work would require destructive or high-risk actions not already approved
- the tree becomes unexpectedly dirty in a way that might conflict with the active step
- the next step depends on unresolved architectural policy rather than implementation detail
- the agent cannot state a concrete acceptance criterion for the next step

## 6. Safe Overnight Boundaries

For this project, unattended overnight work is best suited to:

- coordination-repo documentation and tracking updates
- common AArch64 cleanup
- DTB parser work
- build-glue cleanup
- small kernel-only refactors
- QEMU-free build-validation steps
- boot-first planning and generic `virt` bring-up steps that still have a clear non-hardware validation lane

Overnight unattended work is not yet suited to:

- real Raspberry Pi hardware testing
- image flashing or recovery actions
- lab reconfiguration
- bootloader EEPROM changes
- steps that depend on new manual wiring or operator-supplied settings

## 7. Repo-Touch Policy In Unattended Mode

Prefer one upstream implementation repo plus the coordination repo at a time.

Good unattended step shapes:

- `phoenix-rtos-kernel` plus coordination repo
- `plo` plus coordination repo
- `phoenix-rtos-build` plus coordination repo

Avoid multi-repo implementation jumps overnight unless the first step is explicitly a planning step that bounds that transition.

## 8. Validation Rule In Unattended Mode

The agent must keep using the strongest available non-manual lane.

Current preferred no-hardware lane:

- refresh the copied buildroot:
  `./scripts/prepare-buildroot.sh --copy-components`
- validate with the current AArch64 lane in `phoenix-dev`

If a new step has no reliable no-hardware validation lane, unattended mode must stop before implementing it.

## 9. Tracking Rule In Unattended Mode

At every step boundary, the agent must still update:

- `tracking/current-step.md`
- `tracking/step-history.md`
- `manifests/*.md`
- `docs/inprogress/status.md`

When a new unattended-only workflow constraint is discovered, also update:

- this file
- `docs/knowledge/session-playbook.md`
- `docs/knowledge/execution-control.md`
- `docs/knowledge/manual-operator-instructions.md` if the operator must do something differently

## 10. Resume Rule

The next session should be able to resume unattended work only from repo state, not chat memory.

That means:

- the latest committed `tracking/current-step.md` must be authoritative
- the last completed step must be visible in `tracking/step-history.md`
- the latest validated state must exist in a manifest
- `docs/inprogress/status.md` must summarize the current technical position

## 11. User Override Rule

The user can always tighten unattended mode later.

Examples:

- "continue unattended, but stop at every repo change"
- "continue overnight, but stop before any new runtime code"
- "continue only in common AArch64 kernel work"

If the user gives a tighter rule, it overrides the default unattended policy above.
