# Execution Control and Progress Tracking

This document defines how implementation progress is tracked, reported, and constrained so the work stays on-rails.

The design goal is simple:

- one small step at a time
- one active step at a time
- explicit acceptance criteria
- explicit evidence before closure
- visible user control at every step boundary

## 1. Core Control Model

The implementation is controlled through four durable artifacts:

1. `tracking/current-step.md`
   the only active implementation step
2. `tracking/step-history.md`
   the ledger of completed, blocked, or abandoned steps
3. `manifests/*.md`
   the exact multi-repo integration state tied to validated code
4. `docs/inprogress/status.md`
   the short project-level state and next milestone summary

If these artifacts are kept current, the agent is much less likely to drift.

## 2. Hard Rule: Only One Active Step

At any given time, there must be at most one active implementation step.

That means:

- only one step may be marked `in_progress`
- all code changes in a session must fit inside that step's scope
- if the work needs to widen, the current step must be closed or rewritten before continuing

The agent must not quietly accumulate unrelated work under one vague goal.

## 3. Step Lifecycle

Every implementation step should move through this lifecycle:

1. `planned`
2. `approved` or `ready`
3. `in_progress`
4. `validated`
5. `committed`
6. `closed`

Optional terminal states:

- `blocked`
- `abandoned`

## 4. What Every Step Must Define Up Front

Before implementation code starts, `tracking/current-step.md` must define:

- step ID
- short title
- status
- owner/session date
- milestone or phase
- objective
- in-scope items
- out-of-scope items
- touched repositories expected
- expected files or subsystems
- explicit acceptance criteria
- validation plan
- rollback or known-good baseline

If those items are missing, the step is not ready.

If the step is about a runtime blocker in a QEMU lane, it should also define:

- whether a bounded gdbstub session can answer the question before code changes
- why any planned source-level debug instrumentation is necessary if the
  debugger-first path is being skipped
- how any temporary diagnostic code will be removed if the tested hypothesis is
  disproved

## 5. Acceptance Criteria Rules

Acceptance criteria must be concrete and testable.

Good examples:

- "`plo` prints a UART banner on Pi 4 within 5 seconds of power-on"
- "generic AArch64 target builds warning-clean in Linux VM"
- "kernel reaches shell prompt and survives 10 reboot cycles"

Bad examples:

- "basic boot works"
- "driver mostly done"
- "looks okay"

## 6. User Control Points

This is how you keep control of the implementation.

### Control point A: before a step starts

You can inspect `tracking/current-step.md` and verify:

- the goal is small enough
- the acceptance criteria are concrete
- the step is in the right repo or repos
- the out-of-scope list is strict enough

### Control point B: when a step is finished

You can inspect:

- `tracking/step-history.md`
- the updated `manifests/*.md`
- the commit SHAs recorded there
- the reported validation evidence

### Control point C: before moving to the next milestone

The agent should not silently jump to a larger milestone.

When a step completes and the next step changes milestone, subsystem, or board focus, the agent should pause and present the next proposed step rather than bundling it into the current one.

## 7. Reporting Rules For Future Sessions

Every implementation session should report progress in three layers.

### Layer 1: short live updates

In chat:

- what step is active
- what repository is being touched
- what validation is being run
- any warnings or recoverable errors emitted by the active tools
- whether those warnings are being fixed, tolerated, or escalated

### Layer 2: durable step record

In `tracking/current-step.md` while working:

- current status
- exact scope
- latest findings that change the step

### Layer 3: closure record

When the step ends:

- move the result into `tracking/step-history.md`
- record tested SHAs in a manifest
- update `docs/inprogress/status.md`

## 8. Rules That Prevent Drift

The agent must not:

- start a second active step while one is still `in_progress`
- silently widen scope from one subsystem to several
- move from Pi 4 work to Pi 5 work inside one step
- close a step without explicit validation evidence
- commit code without updating tracking artifacts

The agent may, when explicitly authorized by the user, bias the next-step selection toward the fastest path to the first Pi 4 boot. That is a prioritization rule, not a permission to widen steps.

## 9. Step Size Policy

A step is the right size if it can be explained in a short paragraph and reviewed without cross-referencing half the project.

Recommended examples:

- add one DTB parser capability
- add one AArch64 build target
- add one early UART path
- add one smoke test target

Too large:

- "finish Pi 4 bring-up"
- "add storage, network, and USB"
- "do everything needed for `plo`"

## 10. Required Files And Their Roles

### `tracking/current-step.md`

Must always reflect the real active step.

### `tracking/step-history.md`

Must contain:

- step ID
- title
- result
- commit SHAs or manifest reference
- validation summary
- next recommended step

### `manifests/*.md`

Must contain:

- exact tested repo SHAs
- validation lanes used
- artifact locations when relevant

## 11. Start-of-Session Rule

Before touching implementation code:

1. read the current tracking files
2. confirm the active step is still the right one
3. if there is no active step, create one
4. if the desired work does not fit the active step, close or replace it first

## 12. End-of-Session Rule

Before ending an implementation session:

1. update `tracking/current-step.md`
2. if the step is finished, add an entry to `tracking/step-history.md`
3. update or create a manifest if code changed
4. update `docs/inprogress/status.md`
5. clearly state the next smallest step
6. explicitly mention any warnings or non-fatal tool errors seen in the session
   and what changed so they do not silently recur

## 13. User-Friendly Operating Mode

If you want maximum control, use this policy:

- the agent may work freely within the active step
- the agent must stop at step boundaries
- the agent must propose the next step before starting it

That gives you coarse-grained approval without forcing constant interruptions.

## 14. Explicit Unattended Mode

If the user explicitly authorizes unattended work, the agent may continue past a normal step boundary without waiting for user approval, but only under the rules in `docs/knowledge/unattended-agent-mode.md`.

The key constraint is:

- unattended mode changes the pause behavior at step boundaries
- it does not change step size, validation, commit, or documentation requirements

In unattended mode the agent should still prefer:

- one upstream implementation repo at a time
- one small concept per step
- a planning step before any significant subsystem or repo-family transition
