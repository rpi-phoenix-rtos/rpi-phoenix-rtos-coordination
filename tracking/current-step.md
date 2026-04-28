# Current Implementation Step

## Step: Analyze E1 probe data — characterise the iter-7/8 entry-pointer corruption

**Status**: IN PROGRESS

**Date**: 2026-04-29

**Phase**: Phase A (close TD-04 prerequisite — program relocation)

### Predecessor steps (this session)

1. **Iter-8 Q-marker safety break** — kernel `syspage.c` map-entry
   sub-loop now bounded; if it ever exceeds 32 iterations it emits
   `Q` and breaks rather than hanging the boot.
2. **E1 probe instrumentation** — both plo and kernel now emit
   pre-/post-handoff syspage diagnostics. Plo reads its own emitted
   syspage twice and reports any DDR drift; kernel hashes the syspage
   immediately after handoff and traces the entry pointer at every
   iteration of the map-entry sub-loop.
3. **Netboot test cycle infrastructure** — full automated DHCP+TFTP
   setup inside the phoenix-dev VM, host-side wrappers, DHCP watchdog
   with auto-recovery on bridge wedge, EXIT trap that always powers the
   Pi off. See `docs/netboot-test-cycle.md`.
4. **UART picocom `--noinit` fix** — picocom was leaving the macOS
   USB-UART TTY at whatever stale baud `stty` last set (typically 9600),
   so every netboot capture decoded Pi 115200 output as framing-error
   garbage. `--noinit` removed; `--baud` is now actually honored.

### Captured probe data

From `artifacts/rpi4b-uart/rpi4b-uart-20260428-215524-netboot-uart-fix-115k.log`:

```
probe: pre-jump read#1
probe: no diff (DDR stable)
probe[0x310]=0000000000000000
probe[0x318]=00000000002103a8
probe[0x320]=0000000000210288
probe[0x328]=0047500000000002
... <handoff to kernel> ...
NYOPSTUZbcdeFGHIJKs{000005d8}p{ffffffff}r{ffffffff}q{00000000}VWX
abcdefgB{04fd819dfccb5bcff8c1c7067c971e23947cbdb5cec120fefc8bf2cfb1d6c01d}
T{c0025680}O{c00251c0}h{c00251c0}ij
R{c0025680}klh{c00251e8}ij
R{c00251c0}klh{c0025210}ij
R{c00251e8}klh{c0025290}ij
R{c0025210}klh{c0025320}ij
R{c0025290}klh{c00253b8}ij
R{c0025320}klh{c00253b8}ij    <- iter 6
R{abb988f1}klh{c0025448}ij    <- iter 7: R is corrupt
R{759ecdc4}klh{c6a91328}      <- iter 8: R and h both corrupt
```

### Reading the trace

- `T{c0025680}` — `progs` head pointer (used here as the map's first
  entry pointer for the trace).
- `O{c00251c0}` — first `entry` pointer used by the loop.
- `h{addr}ij` — pre-iteration markers; `addr` is the entry pointer
  the kernel is about to dereference.
- `R{addr}kl` — post-iteration marker; `addr` is the value of
  `entry->next` (i.e. the pointer the next iteration will use).
- `B{hash}` — SHA-256 of the syspage region as observed by the kernel
  immediately after handoff.

### Working hypothesis

Plo's pre-jump probe shows DDR is stable from plo's side — the
syspage bytes plo wrote are still readable to plo right before the
jump. The kernel's `B{...}` hash is computed *after* handoff but
*before* the loop runs, and is currently the only authoritative
"what does the kernel see" snapshot.

The first ~6 iterations walk a clean linked list inside the
`0xc0025xxx` region (kernel virtual addresses, plausible). At
iteration 7 the `entry->next` field reads as `0xabb988f1` — a value
outside any plausible kernel/physical mapping. At iteration 8, both
the live entry pointer (`0xc6a91328`) and the read `entry->next`
(`0x759ecdc4`) are wild.

The clean prefix rules out "list corrupt from the start". Two leading
candidates:

1. **The list is genuinely shorter than 8 entries**, and what we
   call "iter 7's `next`" is a read off the end of the entry that
   used to legitimately be the tail. Plo would have written a NULL
   or terminator there, and we're misreading either because (a) the
   entry struct layout differs between plo and kernel, or (b) the
   relocation arithmetic shifts us off-by-one after some boundary.
2. **The buffer overrun lives between plo and kernel** — plo's
   syspage is larger than `_hal_syspageCopied`'s 4 KiB scratch page
   in `_init.S:788-792`, and entries past the boundary fall into
   uninitialised BSS that happens to start with `0xabb988f1`,
   `0x759ecdc4`, etc. (Hypothesis #1 from the prior step's notes;
   E1 probe confirms it's at least plausible.)

### Proposed next change

A single-shot diagnostic: have plo also emit, just before the jump,
the count of entries it sees on the map's entry list and the size in
bytes of the syspage it copied. Then have the kernel's `B{}` block
also emit (a) the kernel-side count of entries up to and including
the first NULL `next`, and (b) the offset of the first non-zero byte
past the kernel's view of the syspage end. That triangulates whether
the discrepancy is (a) plo wrote a list that genuinely terminates at
N=7 and the kernel walks past it, or (b) plo wrote >7 entries and
the kernel only sees up to the first 7 because of a copy-size
mismatch.

### Exit criteria

- One of the two hypotheses ruled out by the new probe.
- Probe code is small, reverts cleanly, and lives behind the existing
  E1 marker prefix so `summarize-rpi4b-uart-log.py` can pick it up.
- Three consecutive netboot runs produce bit-identical hashed
  syspage and entry-count output (deterministic).

### Rollback

Worktree `dazzling-joliot-cd9889`. Sibling repos:

- `phoenix-rtos-kernel` branch `agent/rpi4-program-reloc`,
  ahead of tag `known-good/2026-04-19-map-relocation-complete`
  by the iter-8 Q-marker and E1 probe commits.
- `plo` E1 probe instrumentation committed (this session).

Coordination-repo baseline manifest:
`manifests/2026-04-23-claude-setup-baseline.md`. A new manifest will
be snapshotted once the next probe extension is captured.

### Notes

- Netboot infrastructure means the build→flash→boot loop is now
  ~30 s per iteration (rebuild-rpi4b-fast → test-cycle-netboot → read
  log) rather than the previous SD-flash cycle. Use it.
- The E1 probe trace was recovered only after fixing the picocom
  `--noinit` bug; before that, every netboot UART capture was
  unreadable. SD-boot logs from prior sessions used `tio` and were
  unaffected — the bug was netboot-cycle-specific.
