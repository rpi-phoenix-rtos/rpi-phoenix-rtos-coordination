# Roadmap to First Full Pi 4 Boot

This roadmap is a forward-looking plan synthesized from the current project
state (as of 2026-04-23). It is deliberately coarse — each phase below must
still be expanded into a concrete `tracking/current-step.md` before any code
is touched. The roadmap is aspirational; `docs/inprogress/status.md` is authoritative
for current state.

## Ground truth we are building from

- Map relocation in `syspage_init()` completes. Markers
  `NYOPSTUZbcdeFGVWXabcdefgmklmno` are reached reliably.
- UART output is clean after MMU enable (virtual-address path).
- Active blocker: **TD-04 — program relocation** in
  `sources/phoenix-rtos-kernel/syspage.c` hangs at marker `o`
  (`hal_syspageRelocate(syspage_common.syspage->progs)` entry or first
  iteration of the `prog = prog->next` loop).
- Sibling repo `phoenix-rtos-kernel` is on `agent/rpi4-program-reloc`,
  tagged `known-good/2026-04-19-map-relocation-complete`.
- Baseline rollback manifest: `manifests/2026-04-23-claude-setup-baseline.md`.

## Definition of "first full boot"

A run that, from power-on:

1. Firmware loads `plo`, `plo` chains into the kernel (already working).
2. Kernel completes `syspage_init()` and `_hal_init()`.
3. A Phoenix-registered console device produces output (not the raw UART
   markers currently used for bring-up).
4. The initial process is spawned from a program image in the syspage
   (dummyfs-root is already present in the built image, see the `plo`
   program table in recent rebuild logs).
5. That process reaches its main loop or a shell prompt without panic.

Anything beyond this (networking, storage, USB, Pi 5) is out of scope for
"first boot" and must not be bundled into these steps.

## Phase plan

Each phase should be one step (or at most a very small ordered set of
steps) in the sense of `docs/knowledge/execution-control.md`. Snapshot an integration
manifest at the end of every phase.

### Phase A — Close TD-04: diagnose the program-relocation hang

Active step. The hang is at marker `o`. The working hypothesis list, in
decreasing order of likelihood:

1. `progs` pointer is malformed or self-referential after relocation
   (same shape as the earlier map-entry bug that produced TD-04's first
   workaround).
2. `hal_syspageRelocate` itself loops when given a pointer into an
   unmapped region.
3. Memory access violation silently caught with no exception reporting
   (see recent restoration of `_early_vector_table` — use its diagnostic
   output if the hang reproduces).
4. Double-relocation aliasing (the map-loop code relocates `map->entries`
   twice; if the program loop copies that pattern it may regress in a
   different shape).

Discipline:

- Try **one** hypothesis per rebuild. Each new marker should be justified
  in writing in `tracking/current-step.md`.
- Prefer a QEMU gdbstub session before adding source-level markers (the
  AGENTS.md rule). Pi 4 emulation fidelity is limited, so this is worth
  trying even if it falls back to markers in the end.
- Every disproved marker must be removed before the phase closes.

Exit criteria:

- The cause of the hang is written down in plain language.
- Either a clean fix (preferred) or a documented second TD-04 shortcut
  that adds a *test and terminate* guard around the program loop instead
  of skipping it entirely.
- Marker `Y` reached at end of `syspage_init()`.

Rollback: `scripts/restore-integration-state.sh manifests/2026-04-23-claude-setup-baseline.md`.

### Phase B — Reach HAL init (marker `f`)

After `syspage_init()` returns, `main()` continues into `_hal_init()`.
The current source path between those should be short and mostly
sequential. Expect:

- BSS zeroing, if not yet applied in `_init.S`.
- `_hal_init()` does GIC, timer, and console setup.

Exit criteria:

- Marker `f` reached (per `docs/inprogress/status.md`'s recorded marker legend).
- No regression in Phase A markers across three consecutive rebuilds.

Risk: BSS mapping (TD-03) may bite here. If it does, either finish this
phase on a narrow workaround *and* promote TD-03 to the next phase, or
fold a minimal BSS mapping fix into this phase — decide based on how far
the workaround reaches.

### Phase C — GIC, generic timer, and console registration

Phoenix already has reusable AArch64 GIC and timer code per
`docs/knowledge/implementation-dossier.md`. Pi 4 uses GIC-400 (GICv2), which is
well-supported shape.

Targeted work:

- Confirm the GIC-400 node parses cleanly from our DTB.
- Bring up the ARM generic timer against the board's frequency.
- Register a PL011-based console through Phoenix's normal console
  registration path, replacing the raw UART marker mechanism for steady
  output (markers can remain for early boot until TD-05 is addressed).

Exit criteria:

- Kernel prints through the Phoenix console subsystem, not raw UART.
- Timer interrupts fire and advance the scheduler tick.
- One full boot path from power-on to this point succeeds three times.

### Phase D — First kernel thread and scheduler liveness

This is normally a minor step if the HAL is correct. Expect:

- `proc_init()`, `thread_init()`, first idle thread, scheduler enabled.
- A visible "scheduler running" marker through the console (not raw UART).

Exit criteria:

- Scheduler runs for at least 10 seconds with no panic or hang.
- IRQ counts incrementing on the console.

### Phase E — Load the first user program from syspage

The build already embeds `dummyfs-root` (and `psh` inside it) as a
syspage program. Lifting it to running code touches `vm/map.c` and the
proc-creation path. A known FIXME in `vm/map.c:763` (`disabled until
memory objects are created for syspage progs`) is likely in the critical
path and may become its own small step.

Exit criteria:

- `psh` or the equivalent first user process reaches user space and
  produces output through the console.
- The boot remains stable for at least 30 seconds after reaching user
  space.

### Phase F — Soak and tag v0.1

- Run 10 consecutive cold boots on real Pi 4 hardware. Capture every
  UART log.
- Tag the kernel repo `known-good/first-full-boot` at the validated
  commit.
- Write a milestone doc mirroring `MILESTONE-MAP-RELOCATION-COMPLETED.md`.

## Post-boot cleanup order

Once first boot is reached, the technical debt must be addressed before
any feature work. Priority:

1. **TD-05** (debug-marker consolidation) — lowest risk, biggest
   readability win; largely mechanical. Also a public-release
   prerequisite.
2. **TD-03** (syspage copy and BSS mapping) — correctness-critical, and
   likely partially paid down during Phase B–C anyway.
3. **TD-04** (program-loop cleanup) — if Phase A used a workaround,
   replace it with the proper relocation pattern plus defensive
   validation.
4. **TD-01** (SMP enable on A72) — gatekeeper for multi-core work.
5. **TD-02** (pre-MMU cache invalidation) — correctness review; unlikely
   to change behavior alone.
6. **TD-06** (DTB robustness) — needed before Pi 4B variants or Pi 5.

## Rollback discipline

At every phase boundary:

1. `scripts/snapshot-integration-state.sh <phase-name>`
2. Record the SHA in `tracking/step-history.md`.
3. Tag the kernel repo `known-good/<phase-name>` at the validated
   commit.

If a phase regresses:

1. `scripts/restore-integration-state.sh <previous manifest>`
2. Re-run the UART capture on known-good to confirm the baseline still
   reaches the previous marker set. (Hardware can drift independently —
   a regression is only a regression if the baseline still works.)

## Parallelism: what can and cannot run in parallel

- **Cannot** run in parallel: anything touching the boot path. The
  single-active-step rule (`docs/knowledge/execution-control.md`) still holds.
- **Can** run in parallel (non-code): documentation polish for public
  release, TD-05 inventory (listing every marker), hardware-lab
  improvements. None of these should produce commits that land in
  upstream repos while a boot-path step is active.

## Unknowns that could blow up the schedule

- Interaction between disabled SMP (TD-01) and Inner-Shareable memory
  attributes if re-enabled too early in Phase C.
- QEMU `raspi4b` machine fidelity: the direct QEMU sanity lane may not
  reliably reach Phase C on its own; real hardware will remain
  authoritative through Phase F.
- The `vm/map.c` FIXME in Phase E may expand into a wider syspage-to-VM
  refactor. If it does, split it out into an explicit step before
  writing code.
