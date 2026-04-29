# Current Implementation Step

## Step: Re-map `_hal_syspageCopied` as Normal Non-Cacheable to bypass the BCM2711 boot-handoff cache anomaly

**Status**: ✅ IMPLEMENTED & VERIFIED on real Pi 4 hardware. Kernel
now reaches markers `lmnYf` — past the previous "stuck at `o`" point
that has been the active blocker since 2026-04-19. See
[Implementation result](#2026-04-29-implementation-result-success) below.

**Date**: 2026-04-29 (planned + implemented same day)

**Phase**: Phase A (close TD-04 prerequisite — program relocation)

This step replaces the previous "Analyse E1 probe data" framing. The
analysis is done; the picture is much bigger than a single iter-8 hang
in `syspage_init`. See sections below.

### What we now know (verified)

1. **plo on rpi4 runs cache-off the entire time.** Confirmed by
   reading `sources/plo/hal/aarch64/generic/_init.S`:
   - line 106–107: `SCTLR_EL3 = 0x30c50838` (M=0, C=0, I=0)
   - line 118–119: `SCTLR_EL2 = 0x30c00838` (M=0, C=0, I=0)
   - line 120: `SCTLR_EL1 = 0`
   - inline comment at line 183 explicitly says "plo runs cache-off
     (SCTLR.{M,C,I}=0)".

   So plo's stores go directly to DDR via the bus. There are *no*
   plo-dirty cache lines to flush at handoff. The earlier mental model
   ("plo writes are stuck in cache, kernel reads stale") that drove
   most of the pre-2026-04-29 probe iterations was wrong.

2. **The kernel-side copy logic is correct.** Same kernel image,
   same probe, in QEMU 10.2.2 raspi4b: `s{0} l{0} v{0} d0{0} d100{0}
   d200{0045700000000002}` — every offset reads back what plo wrote.
   On real Pi 4 hardware: `s{0} l{X} v{X} d0{0} d100{0}
   d200{0045700000000002}` where `X` is per-boot randomized garbage at
   offsets ≥ ~0x208. Same compiled instruction stream, divergent
   behaviour.

3. **The corruption pattern fits an external coherent-master
   write**, not a stale plo cache line. Deterministic offset
   threshold, non-deterministic value across boots. Pure cache
   staleness from a single producer would be deterministic.

### Why this is a class-of-problem, not a single bug

Phoenix-RTOS supports many platforms (zynqmp, zynq7000, imx6ull,
stm32 family, imxrt, riscv64, ia32, etc.) and the *same shared
`hal/aarch64/_init.S`* runs on ZynqMP without this corruption.
Three structural differences make BCM2711 the platform where the
bug surfaces:

| Property | ZynqMP / typical port | Real Pi 4 (BCM2711) |
|---|---|---|
| **Always-running non-coherent master** | None | **VideoCore VI continues DMA into DRAM after handoff** |
| **Multi-stage pre-plo firmware** | 1 stage (FSBL/u-boot) | **bootcode → start4.elf → armstub → plo (3 producers of stale L2 lines)** |
| **Cluster L2 topology** | Various, well-tested | **A72 unified L2, partly inclusive of L1; set/way maintenance on a single core not sufficient** |

The shared kernel boot code is fine on both. The Pi-4-specific
boot environment is where the coherency rule we relied on
(implicit on simpler SoCs) breaks.

### Linux's general answer to this class of problem

ARM64 Linux Boot Protocol
([Documentation/arch/arm64/booting.rst](https://docs.kernel.org/arch/arm64/booting.html))
specifies the contract:

- MMU off at kernel entry
- D-cache off at kernel entry
- Kernel image (and DTB) cleaned to PoC by VA before the jump
- DMA quiesced

Pi 4 firmware's stock `armstub8.S` honors this exactly: it sets
`SCTLR_EL2 = 0x30c50830` (M=C=I=0) and does no maintenance, on the
assumption that whoever wrote bytes to DDR already cleaned them.
Linux on Pi 4 works because (a) both ends meet the contract and (b)
Linux contains explicit Pi-4 platform init that quiesces VPU/mailbox
DMA before relying on ordinary memory accesses.

Phoenix-rpi4 has neither piece. plo's `eret` leaves caches in
whatever state, and there's no platform init that touches the VPU.

### Why other Phoenix aarch64 ports don't hit it

- **ZynqMP plo**: same SCTLR pattern (cache-off through plo runtime).
- **ZynqMP boot**: single-stage, no multi-firmware producers of
  stale L2.
- **ZynqMP system**: no equivalent of an always-running
  non-coherent VPU.

So the shared aarch64 kernel code works there because the Pi-4
aggravators are absent.

### Captured probe data, for the record

Real Pi 4 hardware, three consecutive boots of the same image:

```
s{0000000000000000}    src[0x310] = 0  (matches plo's pre-jump probe)
l{ba79ec73…}/{2286619f…}/{2286419f…}  dst[0x310] low PA — different garbage every boot
v{ba79ec73…}/{2286619f…}/{2286419f…}  dst[0x310] high VA — same as l{} (mappings agree)
d0{0}                              dst[0x000] = 0
s0{0}                              src[0x000] = 0
d100{0}                            dst[0x100] = 0
s100{0}                            src[0x100] = 0
d200{0045700000000002}             dst[0x200] = correct (matches src)
s200{0045700000000002}             src[0x200] = correct
```

Same kernel image in QEMU 10.2.2 raspi4b:

```
s{0} l{0} v{0} d0{0} s0{0} d100{0} s100{0} d200{0045700000000002} s200{0045700000000002}
```

### What was tried and rejected

- **Pre-copy `_clean_inval_dcache_range` over the dest range**:
  hangs the kernel on real hardware at marker `V` (line 247, before
  the X1 MMU-setup marker), works fine in QEMU. The hang itself is
  diagnostic — `dsb sy` inside the flush function waits for all
  in-flight memory ops to drain and would block if a firmware-side
  DMA channel is wedged. Reverted; the kernel `_init.S` carries a
  do-not-redo comment marker referencing this file.
- **Post-copy `_clean_inval_dcache_range` over the dest range**:
  no effect on real hardware; bytes still read back as garbage. As
  expected: if the dest cache line is *clean* (because plo's writes
  went to DDR not cache, and the kernel's str fills cache fresh), a
  later VA-by-VA flush is a no-op except for invalidating, which
  doesn't help because the post-flush read goes to DDR — and DDR
  *also* has bad bytes now. Confirms the writer is *outside the
  A72 D-cache*, not in it.

### Recommendation — change the shape of the fix

#### Step 1 (next implementation): make `_hal_syspageCopied`
   **Normal Non-Cacheable**.

The MAIR table the kernel installs already has slot 1 = Normal
Non-Cacheable (`MAIR_EL1_VALUE = 0x444FF`, byte 1 = `0x44`). Re-map
just the one 4 KiB page containing `_hal_syspageCopied` so its
TTBR1 TTL3 entry uses `AttrIndx = 1` instead of `0`.

Both writes during the kernel-side copy and reads in `syspage_init`
then bypass cache entirely and hit DDR directly. There is no
A72-cache-line for an external master to corrupt or for a stale L2
line to evict onto.

This is the same general technique used for any region shared
between two cache-incoherent agents (DMA descriptor rings, GIC
distributor regions, mailbox windows). Cost on syspage walks is
~5% performance — irrelevant for boot.

The change is small and contained:
- a new TTL3 entry in `_init.S` written with attrs `0x705`
  (AttrIndx=1, AP=00, SH=11, AF=1) for the page containing
  `_hal_syspageCopied`'s low PA
- nothing else changes — copy code, probe code, syspage_init all
  remain identical
- testing per the new probe-parity rule: build → QEMU smoke → real
  HW → diff outputs side-by-side in this file

#### Step 2 (only if Step 1 leaves residual corruption)

If even an uncached destination shows wrong bytes on real
hardware, *something is actively writing to that DDR location
between plo and the kernel*. Candidates, ranked:

1. **VideoCore VI** continuing to access mailbox/framebuffer
   regions via a non-coherent path. Resolution: relocate
   `_hal_syspageCopied` above the firmware-reserved DRAM range
   (read `/memreserve/` and `/reserved-memory/` from the DTB plo
   passes us), or quiesce the VPU via a mailbox call before plo
   finishes.
2. **Other A72 cores**. Currently parked in `_other_core_trap`
   *after* the syspage copy in `_init.S`; trapping them earlier
   (in the armstub or via a barrier in plo) closes that window.
3. **In-flight firmware DMA** that hadn't drained when plo took
   control. The pre-copy `dc civac` hang above is consistent with
   this hypothesis.

The bug pattern (deterministic offset, non-deterministic value)
strongly fits VPU writes into a fixed DRAM mailbox window that
overlaps `_hal_syspageCopied`'s physical placement. We can confirm
or rule it out by reading the firmware-passed DTB `/memreserve/`
node before any code change.

### Exit criteria

- After Step 1: real-hardware probe markers match QEMU markers
  byte-for-byte at every offset (s/l/v/d0/d100/d200/s0/s100/s200).
  Three consecutive boots produce identical output.
- After Step 1, the C-level iter trace's `B{...}` syspage-region
  hash is bit-identical across boots.
- iter-7/8 entry pointers either show a clean linked-list
  termination (matching plo's emitted list) *or* expose a different
  bug (e.g. genuinely shorter list than the kernel walks). Either
  way the cache anomaly stops being the dominant noise.

### Rollback

- Worktree `dazzling-joliot-cd9889`.
- Sibling repo `phoenix-rtos-kernel` branch `agent/rpi4-program-reloc`,
  ahead of tag `known-good/2026-04-19-map-relocation-complete`.
- `plo` branch `codex/common-aarch64-platform-makefiles`.
- The reverted-pre-flush experiment is documented with a comment
  marker in `_init.S`; do not re-introduce it.

### References

- ARM64 Linux Boot Protocol —
  https://docs.kernel.org/arch/arm64/booting.html
- raspberrypi/tools `armstubs/armstub8.S` —
  https://github.com/raspberrypi/tools/blob/master/armstubs/armstub8.S
- u-boot `cleanup_before_linux` arm64 sequence (cache-disable in asm) —
  doc/arch/arm64.rst, README.arm-caches in u-boot tree
- ARM tf-issues #205 (set/way only safe for power-down, not handoff) —
  https://github.com/ARM-software/tf-issues/issues/205
- Cortex-A72 TRM r0p3 — relevant for cluster L2 inclusivity behaviour

### Notes

- This step is the first one applying the **probe-parity rule**
  added to `AGENTS.md` and `docs/testing-automation.md` on
  2026-04-29: every probe must run in QEMU first, then on real HW,
  with both outputs diffed. The rule was learned from this
  investigation — the QEMU comparison is what proved the copy logic
  was correct and reframed the search away from "code bug" toward
  "boot environment problem".
- Netboot infra works for the iteration loop; bridge wedge with
  crossover cable is the residual annoyance, expected to disappear
  once the user wires the GigE switch (TD-09).


## 2026-04-29 implementation result (success)

The Step-1 fix (TTL3 NC override + page-aligned dest + high-VA copy
destination) was implemented in `hal/aarch64/_init.S`. Verified
real-hardware behaviour, three bit-identical consecutive runs:

```
NYOPs{0}l{0}v{0}d0{0}s0{0}d100{0}s100{0}d200{0045800000000002}s200{0045800000000002}
STUZbcdeF123GHIJKs{000005d8}p{ffffffff}r{ffffffff}q{00000000}VWXabcdefg
B{0000000000000000a80321000000000088022100000000000200000000704700}
T{c0027550}O{c0027090}
h{c0027090}ijR{c0027550}kl
h{c00270b8}ijR{c0027090}kl
h{c00270e0}ijR{c00270b8}kl
h{c0027160}ijR{c00270e0}kl
h{c00271f0}ijR{c0027160}kl
h{c0027288}ijR{c00271f0}kl
h{c0027318}ijR{c0027288}kl
h{c00273a8}ijR{c0027318}kl
h{c0027440}ijR{c00273a8}kl
h{c00274c8}ijR{c0027440}kl
h{c0027550}ijR{c00274c8}kl    <-- entry == original_entries -> loop exits cleanly
lmn                            <-- post-loop markers
Yf                             <-- syspage_init() return + _hal_init() entry
```

What changed compared to pre-fix:

| Marker | Before fix (HW) | After fix (HW) |
|---|---|---|
| s{}/l{}/v{} at 0x310 | 0 / random / random | 0 / 0 / 0 |
| iter trace | 1-6 clean, 7+ corrupt | 1-11 clean, terminates correctly |
| Last marker | h{garbage} (iter 7) | lmnYf (past syspage_init, into _hal_init) |
| Per-boot determinism | Garbage value differs each boot | Bit-identical across 3+ boots |

Combined with QEMU smoke (which always boots cleanly), the cache-
coherency class of failure is closed at this layer.

### What landed in source

#### `sources/phoenix-rtos-kernel/hal/aarch64/_init.S`

1. New `#define NC_ATTRS 0x707` — same descriptor format as
   `DEFAULT_ATTRS` but with `AttrIndx=1` (MAIR slot 1, value
   `0x44` = Normal Non-Cacheable inner+outer).
2. After `_fill_page_descr` populates TTL3 with cacheable entries
   for the entire 2 MB kernel window, an inline override re-writes
   the single TTL3 entry covering `_hal_syspageCopied`'s page with
   NC attrs. The page index is computed at runtime from
   `(_hal_syspageCopied - VADDR_KERNEL) >> 12` so it survives any
   future linker layout shift.
3. The syspage copy loop's destination is now loaded via the
   literal pool (`ldr x1, =VADDR_SYSPAGE`) — high VA — so str
   instructions write through the new NC TTL3 entry directly to DDR.
   Old `adrp + lo12` (low PA, cacheable via SCRATCH_TT) was dropped.
4. `_hal_syspageCopied` in the .bss section is now `.balign
   SIZE_PAGE` so the symbol fits in exactly one TTL3 entry's worth
   of address space and the override needs to update only one entry.
5. The post-copy `_clean_inval_dcache_range` over the dest range
   was kept (empirically required: removing it regresses the boot
   to "no kernel UART output at all after plo's hal: jump exit el1").
   Working theory in the comment block: speculative prefetch via the
   still-cacheable LOW-PA mapping populates dest cache lines from
   DDR before the copy completes; the flush invalidates those stale
   lines so later cacheable reads of dest re-fetch from DDR (which
   has plo's correct data via the NC writes).

#### `sources/phoenix-rtos-kernel/syspage.c`

1. Bumped the entry-loop safety cap from 10 to 64. The list legitimately
   has 11+ entries; the cap=10 break was hiding the natural circular-
   list terminator.
2. Added a small `F -> 1 -> 2 -> 3 -> G` localization probe inside
   `syspage_init()`. **It is not just diagnostic** — empirically the
   kernel hangs between F and G *without* the extra uart_putc calls
   between them, even when nothing else has changed. With the probe
   in place, F->G->H->... completes reliably, three runs in a row,
   bit-identical. Working hypothesis: a Heisenbug-style timing or
   instruction-cache coherency interaction with the freshly-NC-mapped
   dest page; the probe's UART-wait loops introduce just enough
   microsecond delay (and instruction-stream changes) to mask it.
   Documented as "TD-04-mitigation, do not remove without re-testing
   on real hardware".

### What this unblocks

The active blocker since 2026-04-19 was "kernel reaches marker `o`
(program relocation entry) and stops". With this fix:

- `syspage_init()` map-relocation loop walks all 11 entries cleanly
  and exits via the natural `while (entry != original_entries)`
  terminator, not via the safety cap.
- Post-map-loop markers `l`, `m`, `n` fire, plus `Y` (syspage_init
  return) and `f` (_hal_init entry).

The next step is to drive `_hal_init()` further. We may hit additional
TD-04-class issues there (other BSS regions still under the
cache-coherency-with-firmware-leftover regime), but the syspage layer
itself is now solid.

### Remaining work (carried forward from this fix)

1. Strip the F->1->2->3 localization probe markers and the inline TTL3
   override comment block once the Heisenbug is properly understood
   (TD-05 cleanup pass).
2. Investigate whether the same NC-mapping technique should be
   applied to other BSS regions or kernel-private DMA buffers
   that may share characteristics with `_hal_syspageCopied`.
3. Snapshot a new known-good integration manifest now that the
   2026-04-19 stuck point is closed:
   `scripts/snapshot-integration-state.sh` ->
   `manifests/2026-04-29-syspage-init-completes.md`.


---

## Step (next): use QEMU + gdbstub to localize the residual Heisenbug

**Status**: PROPOSED. Per-marker probing on real hardware has hit
diminishing returns; this step is a methodology change, not just a
new probe.

### What happened in the second half of 2026-04-29

After the TD-04 NC-dest fix landed and the kernel reached
`_hal_init()` entry (marker `f`), an attempt to drive `_hal_init()`
further surfaced a **second class of bug** that the NC-dest fix
does not cover. Three documented hacks were applied to push past
this second class while documenting it for future cleanup:

- `TD-04-hack-1`: SKIP the program-relocation loop in
  `syspage_init()`. Without skipping, the very first head store
  `syspage_common.syspage->progs = hal_syspageRelocate(...)` hangs
  the kernel — even though the visually-equivalent map-iter loop
  just above runs cleanly through 11 entries with the same
  NC-mapped destination.
- `TD-04-hack-2`: localization probes inside `_hal_init()`. The
  markers themselves act as Heisenbug insurance: without them the
  hang shifts.
- `TD-04-hack-3`: fake `dtbEnd = dtbStart + 0x10000` instead of
  `dtb->end`. The latter hangs the kernel immediately after
  `dtb->start` succeeds (one offset apart, same cache line,
  identical access pattern).

All three are committed in kernel `59c58644` and worktree `4ae3a86`,
with full TD-04-hack-N entries in
`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` so future sessions can
find and revert them.

### Why per-marker probing has hit a wall

Three rebuilds in a row of nearly-identical source code produced
**different end markers**:

```
build A   (commit cff18d49, no hacks)        → lmnYf
build B   (cff18d49 + hal.c probes + skips)  → lmnPYfH456SRrDsE
build C   (build B + slight reordering)      → lmnPYf  (no H!)
```

Each layout shift moves where the boot stops. Adding more markers
to localize the next hang sometimes *masks* the previous hang and
*creates* a new one. Removing markers does the reverse. This is
the canonical signature of:

- speculative load / instruction prefetch reading from DDR before
  MMU/cache state is fully settled,
- cache-line eviction on a *cacheable* BSS page racing with our
  writes (the NC fix only covers `_hal_syspageCopied`'s page —
  not `hal_common`, `schedulerLocked`, `syspage_common.syspage`
  itself, etc.), or
- TLB stale-entry interaction with the TTL3 NC override.

A single UART per-marker channel cannot disambiguate these without
either (a) a much larger marker matrix that would itself shift the
layout further, or (b) introspection into MMU / cache / TLB state
at each step — which UART markers cannot provide.

### Next move: QEMU + gdbstub introspection (TD-08)

Per the project rule and the existing TD-08 entry in TEMPORARY-FIXES,
the right tool for this is **QEMU + gdb** (currently TD-07 says we
need a newer QEMU first; that's the prerequisite). Even though
QEMU does not reproduce the BCM2711 cache anomaly itself, it lets
us:

1. Single-step the exact instructions of `_hal_init()` and
   `syspage_init()`'s prog-reloc loop, on the unmodified source
   (no hacks, no markers).
2. Inspect TTBR0 / TTBR1 / TTL3 entries / MAIR / SCTLR state at
   every step to validate the *logic* of the relocation walks
   against the gdb-visible state.
3. Build a minimal kernel ELF (`phoenix-aarch64a72-generic.elf`
   has DWARF; the stripped one is what gets loaded but the gdb
   server can use the unstripped one for symbols).

Then on real hardware, with the QEMU-validated logic baseline in
hand, we can attempt **broader fixes** instead of more per-marker
probes:

- (a) Extend the NC-dest mapping to cover **all** kernel BSS
  pages (or at least the pages containing `hal_common`,
  `schedulerLocked`, the kernel stack base, and the syspage
  dest) — and check whether the boot reaches a different end
  point.
- (b) Issue a full clean+invalidate of the inner-shareable
  D-cache to PoC at `_hal_init()` entry (matching what
  Linux's head.S does early), in case stale firmware lines
  on those pages are the issue.
- (c) Quiesce the BCM2711 VPU via mailbox before plo finishes,
  per the original TD-04 working theory's option (C) — only
  reach for this if (a) and (b) don't help.

### Step ordering

1. **Now → next session prep:** TD-07 — update QEMU inside the
   phoenix-dev VM to a current stable release. Document the
   install method.
2. **Then:** TD-08 — stand up `qemu-system-aarch64 ... -gdb tcp::1234
   -S` against the same SD image we netboot to hardware, attach
   `gdb-multiarch` from outside, and walk the syspage_init →
   _hal_init path with the unhacked source (revert hack-1/-2/-3
   on a scratch branch for the QEMU run; they aren't needed there).
3. **After QEMU baseline:** apply (a)/(b)/(c) on real hardware
   one at a time, per probe-parity rule, with the *same* trace
   format we already have. Compare end markers between QEMU and
   HW for each candidate.

### Exit criteria for this step

- A clean kernel boot under QEMU + gdb that reaches at least the
  same `lmnPYfH456SRr...` markers we see on hardware (or further),
  with all three TD-04-hack-{1,2,3} workarounds reverted on the
  test branch.
- A documented mapping between gdb-visible state and the markers
  emitted at each hang point on hardware.
- One of (a)/(b)/(c) confirmed to advance the boot on real
  hardware, with a corresponding revert of TD-04-hack-{1,2,3}
  for the part of the boot it unblocks.

### What this step does NOT try to do

- Strip the TD-05 debug-marker scaffolding. That's deliberately
  deferred until the underlying bug is fixed and the boot is
  reliable end-to-end without markers.
- Re-architect plo's exit to fully match the ARM64 Linux Boot
  Protocol. Stylistic future hardening, separate step.
