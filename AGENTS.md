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
5. `docs/linux-host-bootstrap.md` — primary dev host (Linux x86-64). Read
   `docs/host-macos-apple-silicon.md` only if working on a macOS+Lima
   workstation; the rpi4 workflow has been on Linux since 2026-05-20.
6. `docs/manual-operator-instructions.md`
7. `docs/code-quality-and-upstreaming.md`
8. `docs/execution-control.md`
9. `docs/unattended-agent-mode.md`
10. `tracking/current-step.md`
11. `docs/platforms/raspberry-pi-4.md`
12. `docs/testing-automation.md`
13. `docs/session-playbook.md`
14. `docs/source-artifacts.md`

Read `docs/platforms/raspberry-pi-5.md` when the task touches Pi 5 or RP1.
Read `skills/README.md` when choosing a local project skill.

## Local Skills

This repository defines project-local skills under `skills/`. They are not part of any global skill registry, so agents must open them manually when relevant.

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
- When Pi 4 firmware files are needed for staged boot media or real-device tests, agents may fetch the required files from the Raspberry Pi firmware repository boot tree:
  `https://github.com/raspberrypi/firmware/tree/master/boot`
- Keep the Phoenix boot model intact where possible:
  `Raspberry Pi firmware -> plo -> syspage -> kernel -> user-space servers/drivers`
- Work in narrow, phase-gated steps. Do not advance to the next major step until the current step has explicit success criteria, validation evidence, and documentation updates.
- There must be only one active implementation step at a time, tracked in `tracking/current-step.md`.
- After every successful implementation step, commit the relevant changes in every touched upstream repository and then commit the coordination-repo documentation or manifest update that records the tested integration state.
- Manage Phoenix as multiple sibling git repositories, not as a rewritten monorepo. Keep repository coordination in this repo through documentation and manifest files.
- Use the disposable local buildroot prepared by `scripts/prepare-buildroot.sh` for `phoenix-rtos-project` builds; do not turn nested submodule clones into the primary working model.
- For iterative Pi 4 bring-up rebuilds, prefer `scripts/rebuild-rpi4b-fast.sh`
  over ad hoc `build.sh clean host core project image` loops. Reserve the full
  clean rebuild for build-infra changes, upstream-sync churn, toolchain/sysroot
  changes, or suspected stale-build state.
- If the user explicitly authorizes unattended work, the agent may continue across normal step boundaries only under the rules in `docs/unattended-agent-mode.md`; those rules do not relax step sizing, validation, or commit discipline.
- On this workstation, treat Linux as the authoritative build and emulation environment. Use macOS natively for coordination, editing, and hardware control; use a Linux VM for Phoenix builds and most QEMU runs unless a task is explicitly documented as safe on the host.
- For Pi 4 SD-card images exported from `phoenix-dev` to the host, use only
  `scripts/export-rpi4b-sdimg.sh`. Do not improvise with ad hoc `scp`, `sftp`,
  `rsync`, `dd`, `cat`, or manual Lima copy commands. If export reliability is
  ever in doubt, fix that helper and the corresponding docs instead of trying a
  different transfer path.
- For Pi 4 UART capture on the macOS host, use only
  `scripts/capture-rpi4b-uart.sh` together with
  `scripts/summarize-rpi4b-uart-log.py`. Do not improvise with ad hoc
  `screen`, `cu`, raw `picocom`, or one-off shell pipelines unless the
  canonical helper is first proven insufficient and the docs are updated in the
  same session.
- When debugging runtime behavior under QEMU, prefer GDB through the QEMU gdbstub before changing source code to add probes, traces, or debug prints. Only add source-level runtime instrumentation after documenting why the GDB-first path is insufficient for the current question.
- **Probe parity rule:** every new diagnostic probe added to plo or kernel
  must be tested in QEMU first (`scripts/qemu-shell-smoke.sh rpi4b`), then on
  real Pi 4 hardware (`scripts/test-cycle-netboot.sh`), with the two outputs
  compared in `tracking/current-step.md`. Markers that match across QEMU and
  hardware describe properties of the code; markers that diverge describe
  properties of real silicon — and that diff is where the diagnostic signal
  lives. See `docs/testing-automation.md` for the full workflow. This rule
  was learned the expensive way: the iter-7/8 syspage corruption looked like
  a code bug for several sessions, until the QEMU comparison proved the
  copy logic was correct and isolated the failure to a Cortex-A72 cache
  coherency anomaly that QEMU's functional model does not reproduce.
- If a code change is introduced only to probe, diagnose, or verify a hypothesis and that hypothesis turns out false, remove that diagnostic code before the step is closed or committed. Keep only the code changes that are actually required by the confirmed fix or design.
- Optimize all future code for readability and upstreamability: keep changes small, consistent with nearby Phoenix code, warning-clean, and free of gratuitous formatting churn.
- Treat warnings and non-fatal errors from tools as first-class signals. Surface
  them to the user in the same session, decide whether they are likely
  significant, and either fix the process so they stop happening or document why
  they are currently tolerated.
- Pay special attention to DTS, DTSI, and DTB handling. Prefer authoritative
  final-form DTB blobs from trusted upstream sources or already-built artifacts
  over ad hoc local DTS preprocessing whenever that is practical. If DTB or DTS
  manipulation is still required, surface every warning and tighten the helper
  so later sessions do not silently repeat questionable transformations.
- When the user explicitly prioritizes the first Raspberry Pi 4 boot, use a boot-first fast lane:
  prefer the smallest steps that unlock common timer runtime validation, generic QEMU `virt`, PL011 console reuse, `plo` boot, and Pi 4 kernel handoff; defer generic cleanup that is not on that path.
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

## Multi-Agent Working Architecture

The bring-up effort runs as a **single orchestrating session that fans work out to background subagents in parallel**, then synthesizes results in one place. This section is the canonical layout. Agents must follow it.

### Roles

- **Orchestrator (the active session)** — the only entity that:
  - decides what gets analyzed, by which agent, in what order;
  - integrates findings from multiple agents into a single picture of project state;
  - operates the hardware (build, power-cycle, UART capture, log analysis) — there is one Pi, so hardware tests are inherently serial;
  - reads/writes the source tree, commits, and updates trackers.
  Subagents do not run hardware tests directly. They prepare patches, predictions, and instrumentation plans; the orchestrator queues and runs the resulting test cycle.

- **Analysis agents** (parallel, background, read-only or research) — fan out for any question that decomposes into independent sub-questions:
  - source-tree dives (Phoenix, Linux, FreeBSD, ATF, U-Boot, seL4, Xen)
  - documentation hunts (TRMs, errata sheets, peripheral datasheets, vendor advisories)
  - hypothesis evaluation (each agent owns one hypothesis end-to-end and returns a confirm/refute with cited evidence)
  - patch design (drafts a unified diff plus predicted UART-visible signature)

- **Forward-research agents** (parallel, background, long-horizon) — produce primary-source-cited markdown notes under `docs/research/` for subsystems we have not yet started but will need: GPU/DRM, GENET Ethernet, BCM43455 WiFi, BCM43455 Bluetooth, GPIO/pinctrl, RTC, thermal, power. Each note is a self-contained brief: register map, kernel DT bindings, driver entry points, known quirks, an "open questions" list. The orchestrator does not block on these; they are pre-loaded knowledge for when their step becomes active.

- **Hardware test runs** (sequential, orchestrator-only) — every test is one cycle through `scripts/rebuild-rpi4b-fast.sh` → `scripts/pi_power_off.sh` → `pi_power_on.sh` → `scripts/netboot-bridge-recover.sh` → `scripts/test-cycle-netboot.sh`. The orchestrator picks the **highest-information test** at any moment (one that falsifies the most hypotheses at once). Subagents must never invoke these scripts.

### Operating principles

1. **Fan out wide on independent questions.** If a problem decomposes into N independent sub-problems, spawn N agents in parallel in a single tool turn. Do not analyze sequentially what can be analyzed in parallel.
2. **Single decision hub.** Patch selection, hardware queueing, and commit decisions live with the orchestrator. Subagents never push work to hardware.
3. **Composite tests over serial tests.** Whenever multiple hypotheses share a fix block, build one image that mitigates all of them and run a single cycle. Each cycle is ~5–10 minutes; serial cycles are the limiting factor.
4. **Knowledge propagation.** When an analysis agent returns findings that change another running agent's premise, the orchestrator uses `SendMessage` to update the affected agent rather than letting it complete on stale assumptions.
5. **Forward research is always-on, never-blocking.** Background agents researching future subsystems run continuously in the background of any session. Their output is markdown under `docs/research/`. The orchestrator does not wait for them.
6. **Evidence > speculation.** Every claim from a subagent must cite a primary source (file path + line, official doc URL, errata number). Speculation without citation is rejected.
7. **Diagnostic instrumentation discipline.** When a hypothesis is confirmed or refuted, remove the instrumentation that proved it (per the existing project rule). Do not let `debug()` markers accumulate.

### Forward-research log layout

`docs/research/` is the durable home for forward-looking briefs. Each file is one subsystem:

- `docs/research/gpu-vc6.md` — VideoCore VI 3D + display pipeline
- `docs/research/ethernet-genet.md` — BCM2711 GENET MAC
- `docs/research/wifi-bcm43455.md` — BCM43455 WiFi (SDIO)
- `docs/research/bluetooth-bcm43455.md` — BCM43455 BT (UART-attached HCI)
- `docs/research/gpio-pinctrl.md` — BCM2711 GPIO + pin muxing
- `docs/research/rtc-thermal-power.md` — RTC, thermal, watchdog, power management

Each file should answer, with citations: (a) what hardware blocks are involved, (b) where Linux's driver lives, (c) the minimum subset Phoenix needs to claim "working", (d) known quirks/errata, (e) open questions for the orchestrator to resolve when the corresponding step becomes active.

### Reasonable parallelism upper bound

Practical sweet spot is **3–8 concurrent background agents**. Beyond that, synthesis cost in the orchestrator outweighs analysis throughput. Forward-research agents count toward this budget but typically run long enough that they never queue against immediate-debug agents.

## Expected Future Repository Contents

As implementation starts, expect future agents to add:

- target definitions for Raspberry Pi 4 and later Raspberry Pi 5
- AArch64 platform support in `phoenix-rtos-kernel` and `plo`
- device drivers in `phoenix-rtos-devices`
- test target integrations in `phoenix-rtos-tests`
- build and image generation glue in `phoenix-rtos-build` and `phoenix-rtos-project`

Until then, this repository remains the planning and execution guide.
