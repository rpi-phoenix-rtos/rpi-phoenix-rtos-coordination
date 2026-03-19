# Session Playbook

This file defines how future Codex sessions should operate so that long implementation efforts remain coherent even after context compaction or multi-session handoff.

## 1. Session Start Checklist

Before touching code, re-read:

1. `AGENTS.md`
2. `docs/status.md`
3. `docs/repository-work-breakdown.md`
4. `docs/git-repository-strategy.md`
5. `docs/host-macos-apple-silicon.md`
6. `docs/manual-operator-instructions.md`
7. `docs/code-quality-and-upstreaming.md`
8. the relevant platform note:
   - `docs/platforms/raspberry-pi-4.md`
   - `docs/platforms/raspberry-pi-5.md`
9. `docs/testing-automation.md`
10. `docs/source-artifacts.md`

Then determine:

- which upstream Phoenix repository the task belongs to
- whether the task is common AArch64 work, Pi 4 work, Pi 5 work, or test/lab work
- whether the change should first be validated in QEMU, on hardware, or both
- what the commit boundary for this step will be
- whether the work should run on the macOS host or in the Linux VM

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

1. read the exact upstream Phoenix files involved
2. read the exact Linux or BSD reference files involved
3. update the plan in plain language
4. make the smallest coherent change
5. simplify the patch until it matches nearby Phoenix style and remains easy to review
6. run the fastest validation lane available
7. capture artifacts and classify failures
8. commit each touched upstream repository once the step succeeds
9. update the docs or integration manifest if any new fact or constraint was discovered

## 4. Context-Compaction Recovery

If the session becomes long or context is compacted, do not rely on chat memory.

Re-read at least:

1. `docs/status.md`
2. `docs/repository-work-breakdown.md`
3. `docs/git-repository-strategy.md`
4. `docs/host-macos-apple-silicon.md`
5. `docs/testing-automation.md`
6. the relevant platform note
7. any document you updated earlier in the same session

Also re-open the specific upstream source files currently being mirrored or ported.

## 5. Documentation Update Rule

Update documentation during the same session when you learn:

- a required boot setting
- a DT node or compatibility string that the code depends on
- an interrupt number, register block, DMA limit, or memory constraint
- a firmware dependency or workaround
- a lab automation caveat
- a failure mode that is likely to recur

Preferred destinations:

- concise state and next steps:
  `docs/status.md`
- architecture or phased plan:
  `docs/implementation-dossier.md`
- manual or operator-facing setup:
  `docs/manual-operator-instructions.md`
- style, review, and quality rules:
  `docs/code-quality-and-upstreaming.md`
- board-specific facts:
  `docs/platforms/*.md`
- important links or exact code paths:
  `docs/source-artifacts.md`

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

## 9. Session Close Checklist

Before ending a session:

1. update `docs/status.md`
2. update any more specific document that changed in meaning
3. update `docs/manual-operator-instructions.md` if any new manual step or operator requirement was discovered
4. update `docs/code-quality-and-upstreaming.md` if a new reliable style or quality rule was learned
5. add any new upstream links or code paths to `docs/source-artifacts.md`
6. update or create an integration manifest if code changed
7. commit the coordination-repo updates
8. note what was validated and what was not validated
9. note the next smallest sensible task

## 10. Long-Run Project Rule

Prefer durable progress over flashy milestones.

That means:

- generalize common AArch64 code before piling on board hacks
- make the test loop stronger as early as possible
- finish Pi 4 foundations before serious Pi 5 implementation
- document anything that a future agent would otherwise have to rediscover
