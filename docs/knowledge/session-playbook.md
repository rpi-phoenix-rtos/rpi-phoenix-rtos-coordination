# Session Playbook

This file defines how future implementation sessions should operate so that long efforts remain coherent even after context compaction or multi-session handoff.

## 1. Session Start Checklist

Before touching code, re-read:

1. `AGENTS.md`
2. `docs/inprogress/status.md`
3. `docs/knowledge/repository-work-breakdown.md`
4. `docs/knowledge/git-repository-strategy.md`
5. `docs/knowledge/host-macos-apple-silicon.md`
6. `docs/knowledge/manual-operator-instructions.md`
7. `docs/knowledge/code-quality-and-upstreaming.md`
8. `docs/knowledge/execution-control.md`
9. `docs/knowledge/unattended-agent-mode.md`
10. `tracking/current-step.md`
11. the relevant platform note:
   - `docs/knowledge/raspberry-pi-4.md`
   - `docs/knowledge/raspberry-pi-5.md`
12. `docs/knowledge/testing-automation.md`
13. `docs/knowledge/source-artifacts.md`

Then determine:

- which upstream Phoenix repository the task belongs to
- whether the task is common AArch64 work, Pi 4 work, Pi 5 work, or test/lab work
- whether the change should first be validated in QEMU, on hardware, or both
- what the commit boundary for this step will be
- whether the work should run on the macOS host or in the Linux VM
- whether the work fits the currently active step without widening scope
- whether the disposable local buildroot needs to be refreshed with `scripts/prepare-buildroot.sh` before any `phoenix-rtos-project` build
- whether the task needs the linked or copied buildroot mode
- whether the task can use `scripts/rebuild-rpi4b-fast.sh` instead of a full
  clean rebuild
- whether the user has explicitly authorized unattended continuation
- whether a QEMU-side runtime question should be answered with a bounded gdbstub
  session before any source-level debug instrumentation is added

## 2. Session Scoping Rule

Keep each implementation session narrow.

Good scopes:

- generalize one DTB parser subsystem
- add one board console path
- add one test target
- bring up one driver family

Bad scopes:

- "finish Pi 4"
- "add all low-speed peripherals"
- "do storage, network, and USB in one run"

## 3. Working Loop

For each substantial task:

1. confirm `tracking/current-step.md` exists and matches the intended scope
2. read the exact upstream Phoenix files involved
3. read the exact Linux or BSD reference files involved
4. if the blocker is in a QEMU lane, decide whether a bounded gdbstub session
   can answer it before any code is changed
5. update the plan in plain language
6. make the smallest coherent change
7. simplify the patch until it matches nearby Phoenix style and remains easy to review
8. run the fastest validation lane available
9. capture artifacts and classify failures
10. for Pi 4 SD-card images exported from `phoenix-dev`, use only
    `scripts/export-rpi4b-sdimg.sh`; if it fails, fix that helper instead of
    trying an alternate transfer method
10a. for iterative Pi 4 build loops, prefer
    `scripts/rebuild-rpi4b-fast.sh`
    and let it choose `project image`, `core project image`, or
    `clean host core project image` based on dirty sibling repos; use the full
    clean path only when the helper's scope or the situation clearly requires it
10b. for Pi 4 UART capture on the macOS host, prefer
    `scripts/capture-rpi4b-uart.sh`
    plus
    `scripts/summarize-rpi4b-uart-log.py`
    instead of ad hoc serial commands; the canonical helper now prefers `tio`
    automatically when it is installed and falls back to `picocom` only when
    needed
10c. if a build, packaging, DTB, download, or helper step emits warnings or
    recoverable errors, surface them, classify them, and tighten the process so
    later sessions do not keep reproducing them silently
11. commit each touched upstream repository once the step succeeds
12. update the docs, tracker, or integration manifest if any new fact or constraint was discovered

## 3A. Unattended Loop

When the user has explicitly authorized unattended work, the session may continue beyond a normal step boundary only if the rules in `docs/knowledge/unattended-agent-mode.md` are satisfied.

That means:

1. finish the active step cleanly
2. commit all touched upstream repos
3. commit the coordination-repo closeout
4. define the next small step in `tracking/current-step.md`
5. continue only if the new step:
   - stays in the same safe lane
   - needs no manual action
   - has a clear validation path

If those conditions do not hold, stop with the repo in a clean tracked state.

## 4. Context-Compaction Recovery

If the session becomes long or context is compacted, do not rely on chat memory.

Re-read at least:

1. `docs/inprogress/status.md`
2. `docs/knowledge/repository-work-breakdown.md`
3. `docs/knowledge/git-repository-strategy.md`
4. `docs/knowledge/host-macos-apple-silicon.md`
5. `docs/knowledge/execution-control.md`
6. `docs/knowledge/unattended-agent-mode.md`
7. `tracking/current-step.md`
8. `docs/knowledge/testing-automation.md`
9. the relevant platform note
10. any document you updated earlier in the same session

Also re-open the specific upstream source files currently being mirrored or ported.

## 5. Documentation Update Rule

Update documentation during the same session when you learn:

- a required boot setting
- a DT node or compatibility string that the code depends on
- an interrupt number, register block, DMA limit, or memory constraint
- a firmware dependency or workaround
- a lab automation caveat
- a UART capture workflow caveat
- a failure mode that is likely to recur

Preferred destinations:

- concise state and next steps:
  `docs/inprogress/status.md`
- architecture or phased plan:
  `docs/knowledge/implementation-dossier.md`
- manual or operator-facing setup:
  `docs/knowledge/manual-operator-instructions.md`
- style, review, and quality rules:
  `docs/knowledge/code-quality-and-upstreaming.md`
- step scope, acceptance criteria, and closure state:
  `tracking/current-step.md` and `tracking/step-history.md`
- board-specific facts:
  `docs/platforms/*.md`
- important links or exact code paths:
  `docs/knowledge/source-artifacts.md`

## 6. Artifact Discipline

For any real build or test run, preserve or summarize:

- target name
- git revisions or checked-out commits of upstream repos if known
- built image names
- exact firmware files and `config.txt` used
- DTB used
- UART log
- failure classification

If the raw artifacts are too large to keep in chat, document their locations and the key outcomes.

## 7. Validation Order

Default validation order:

1. host-side build validation
2. generic AArch64 QEMU validation if applicable
3. Raspberry Pi-specific emulator validation if applicable
4. real hardware validation

Do not treat a task as complete unless the strongest relevant validation lane has passed.

Examples:

- DTB parser refactor: host tests plus QEMU may be enough initially
- `plo` Pi 4 console bring-up: real Pi 4 required
- GENET or xHCI: real hardware required

## 8. Failure Triage Rule

When something breaks, classify it before debugging:

- build system
- image assembly
- firmware load
- `plo`
- kernel
- shell/runtime
- driver/runtime peripheral
- hardware-lab infrastructure

This avoids spending kernel time on a relay, serial adapter, or bad image issue.

When the failure is inside a QEMU lane:

- prefer a bounded gdbstub inspection before adding debug prints or one-off
  trace code
- only add source-level probes after the debugger path has been shown
  insufficient for the specific boundary being investigated
- if a temporary source-level probe disproves its hypothesis, remove it before
  closing or committing the step

## 9. Session Close Checklist

Before ending a session:

1. update `docs/inprogress/status.md`
2. update any more specific document that changed in meaning
3. update `docs/knowledge/manual-operator-instructions.md` if any new manual step or operator requirement was discovered
4. update `docs/knowledge/code-quality-and-upstreaming.md` if a new reliable style or quality rule was learned
5. update `tracking/current-step.md` and `tracking/step-history.md` as needed
6. add any new upstream links or code paths to `docs/knowledge/source-artifacts.md`
7. update or create an integration manifest if code changed
8. commit the coordination-repo updates
9. note what was validated and what was not validated
10. note the next smallest sensible task
11. if the session was unattended, confirm the stop reason is obvious from the docs or tracker state

## 10. Long-Run Project Rule

Prefer durable progress over flashy milestones.

That means:

- generalize common AArch64 code before piling on board hacks
- make the test loop stronger as early as possible
- finish Pi 4 foundations before serious Pi 5 implementation
- document anything that a future agent would otherwise have to rediscover
