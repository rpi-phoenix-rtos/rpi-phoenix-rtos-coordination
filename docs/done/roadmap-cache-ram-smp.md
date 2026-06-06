# Roadmap: caches + 4 GiB DRAM + SMP + HDMI/USB console

**Status:** ACTIVE plan as of 2026-05-04, with Stage 1 cache-enable
parked 2026-05-05 after 4 architectural-refactor cycles all hit silent
hangs on real Pi 4 (resolution requires JTAG-grade debugging access).
Stages 2 (4 GiB) and 4 (HDMI/USB) proceed without Stage 1 caches at
reduced boot speed; Stage 3 (SMP) remains blocked on Stage 1 because
LDXR/STXR cross-core semantics require cacheable Inner-Shareable
memory. Supersedes ad-hoc step-by-step sequencing for these four
interconnected goals.

This document is the single source of truth for the *strategic* trajectory
toward a fully working Phoenix-RTOS on Raspberry Pi 4. Tactical per-step
execution lives in `tracking/current-step.md`; per-shortcut history lives
in `docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`. This file ties them
together.

## User-stated goals (2026-05-04)

The user named four crucial milestones, in no particular order:

1. **Caches fully enabled** (currently OFF in kernel; Pi runs ~62× slower
   than physics).
2. **4 GiB DRAM unlocked** with correct GPU/VC4 memory partitioning
   (currently clamped to ~948 MiB; firmware reserves 76 MiB for VC4).
3. **SMP** with all four Cortex-A72 cores online (currently single-core,
   with TD-13 atomic-fallback shortcut).
4. **HDMI0 text console + USB keyboard input** (the original Gate 2 + Gate 3
   in `tracking/current-step.md`). The end-user-visible payoff.

## Dependency analysis

| Goal | Hard prerequisite | Why |
|---|---|---|
| 4 GiB RAM + GPU memory | None (mostly independent) | DTB-driven memory layout; doesn't need caches or SMP |
| Caches enabled | Boot restructure to Linux `__enable_mmu` shape | All 5 cache-enable attempts on real Pi 4 faulted at translation walk because PT writes happen *after* `SCTLR.M` flip |
| SMP (cores 1-3) | Caches enabled + IS-shareable PT attrs | LDXR/STXR exclusive monitor's cross-core semantics are defined only over Inner-Shareable Normal Cacheable memory |
| HDMI + USB-keyboard console | All three above | Performance + DMA correctness + interrupt steering across cores |

**Caches are the architectural bottleneck.** SMP cannot work correctly
without them (the exclusive monitor's cross-core semantics require
IS-shareable Normal Cacheable memory). And debugging anything else at
1/62 speed wastes human and CI time on every cycle.

The 4 GiB unlock is independent of the cache work. We could do it first,
but each Pi cycle would run at ~7 minutes to `(psh)%` instead of seconds,
which slows iteration on TD-15 phases 2-6 dramatically. The gain from
fixing caches first amortizes across every later iteration.

## Why the cache attempts have failed (5 iterations, 2026-05-04)

The `tracking/current-step.md` 2026-05-04 update logs five distinct
cache-enable attempts. Every attempt produced a walk-time translation
fault on real Pi 4, even though the page tables had been written
correctly with caches off. Decoded faults:

| # | Approach | Result |
|---|---|---|
| 1 | Set/way invalidation + M\|C\|I together | EC=0 unknown (suspected A72 #851672) |
| 2 | VA-range pre-MMU | Silent hang Pi between X3/X4 |
| 3 | VA-range post-MMU, kernel image only | ESR=0x96000003 level-3 fault, FAR=0xffffffffc0001890 |
| 4 | + post-enable `tlbi vmalle1` | ESR=0x96000001 level-1 fault, FAR=0xfe201018 |
| 5 | + PT region invalidation | Same fault as #4 |

The pattern is consistent: adding more invalidation does not move the
needle. The most plausible remaining hypothesis is that
**Cortex-A72 speculatively populates D-cache lines for the kernel-image
PA range while D-cache is "off"**, and `dc civac` does not fully evict
those speculatively-filled lines. When the MMU then turns on and the
table walker reads PT entries through the freshly-coherent path, it
either misses or reads a stale walk because the speculation polluted
the wrong line.

**Linux arm64 avoids this entire problem** by structuring boot so that
*all* page-table memory is populated, cleaned to PoC, and made coherent
**before** the single atomic flip that enables M, C, and I together.
After the flip, no PT memory is mutated until normal kernel runtime.
Phoenix currently does the opposite — flips M first, then mutates
syspage and TTL3, then would flip C|I — and this is the architectural
shape we need to fix.

## Stage plan

### Stage 1 — Cache root cause: Linux `__enable_mmu` shape (PARKED 2026-05-05)

**Outcome:** 4 architectural-refactor cycles on real Pi 4. Every cycle
produced a silent hang on real silicon while QEMU passed cleanly. The
hang point is consistent across all 4 cycles: between the SCTLR write
that affects cache state and the first post-flip instruction. No
exception fires; the no-call exception dump produces no output.

This pattern strongly suggests a BCM2711-specific hardware coherency
interaction (SCU, L2 cache state, or firmware-shared coherency domain)
that QEMU's cache model doesn't reproduce. Resolution requires
lab-grade debugging access (JTAG + real-time trace, ARM RealView, or
equivalent) to observe what the core does in those few instruction
cycles. **Beyond the scope of UART-only iteration; tracked as a
future investment session.**

`tracking/current-step.md` 2026-05-05 has the full iteration table
with image SHAs and per-cycle variants. `_init.S` is back at kernel
HEAD `49ca0c66` (the cache-disabled validated baseline).

The Stage 1 prereqs that DID land are kept in tree as building
blocks for any future attempt:
- VA-range cache helpers (`_clean_inval_dcache_range`,
  `_inval_dcache_range`, `_inval_icache_range`).
- No-call early exception dump (kernel `2a5b6a05`).
- TTBR0 alias safety + early TTBR0 drop + pre-MMU PT invalidation
  (kernels `7f7684c4`, `d52f6c3a`, `5e727dcc`).

### Stage 1 — original plan (preserved for the future investment session)

The plan below is the architectural target. It's correct but
empirically the M+C+I or M-after-(C+I) transitions hang on real Pi 4.
Future work needs JTAG-level diagnosis before retrying.

**Goal:** Eliminate every page-table or syspage write that currently
happens *after* `SCTLR_EL1.M` is set. Move all such writes into the
caches-off pre-MMU window. After the flip, no PT/syspage mutation; only
`isb` + `br x0` to high VA.

**Concrete refactor in `sources/phoenix-rtos-kernel/hal/aarch64/_init.S`:**

1. **Inventory** every store between `SCTLR.M=1` and `b main`:
   - TD-04 NC mapping for `_hal_syspageCopied`
   - Syspage copy itself (currently in `_core_0_virtual:` after MMU on,
     per kernel commit `d52f6c3a`)
   - Any TTL3 fixup, vector-table install, stack-pointer fixup
   - The post-copy `_clean_inval_dcache_range` (this becomes redundant
     after the restructure)
2. **Move every such store** to *before* the SCTLR.M write. The
   bootstrap mapping must already cover the regions those stores
   touch. If the current TTBR0 identity map doesn't cover them, extend
   it or add a temporary high-VA mapping that points at the right PA
   *with the MMU still off*.
3. **Replace `bl hal_cpuInvalDataCacheAll`** (set/way; unreliable on
   A72 per #851672) with one VA-range pass over the union of:
   - kernel image `[__init_start, __init_end]` ∪ `[_kernel_image_start, _kernel_image_end]`
   - PT region `[PMAP_COMMON_KERNEL_TTL2 .. PMAP_COMMON_STACK]`
   - Syspage destination region
   Using `dc civac` per cache line (line size from `CTR_EL0` IminLine,
   already implemented as `_clean_inval_dcache_range` and
   `_inval_icache_range` helpers in kernel `49ca0c66`).
4. **Single atomic SCTLR write** that sets M | C | I (and clears nTLSMD
   etc. as needed) in one MSR.
5. **Post-flip path is now minimal**: `isb` → `tlbi vmalle1is; dsb ish;
   isb` → `br x0` to `_core_0_virtual` → set up SP_EL1 in registers
   only → `b main`. **No memory writes between the flip and `b main`.**

**Validation:**
- QEMU Pi 4 + generic AArch64 smoke: both reach `(psh)% help`.
- Real Pi 4 netboot: reaches `(psh)%` at native speed. Target:
  TD-16 nop-loop probe `dt ≈ 0x4000` (was `0x872d51` cache-off, expected
  ~144,000 ticks = `0x23280` at 1.5 GHz with caches on).
- The no-call exception dump (kernel `2a5b6a05`) catches any new fault
  in the post-flip window without recursive faulting.

**Exit criterion:** Pi 4 reaches `(psh)%` in **under 30 s** capture
window (vs ~420 s today), with no exception markers in the log.

**Rollback baseline:** kernel `49ca0c66` (current head with VA-range
helpers + iteration log). Manifest:
`manifests/2026-05-04-td16-va-range-investigation.md`.

**Risk:** if the refactor proves intractable in 1-2 sessions, escalate
the question rather than churning more iterations:
- Is there a hardware errata workaround we're missing?
- Should we revisit Circle's exact cache-enable sequence on Pi 4?
- Should we accept I-cache only (with documented D-cache workaround)
  as a temporary stage exit, and return to D-cache after Stage 2 lands?

### Stage 2 — 4 GiB DRAM unlock + GPU memory partitioning (2-3 sessions)

This is **TD-15 phases 2-6** unchanged in content; the only difference
is we run it with caches enabled (per Stage 1), so each iteration is
fast and DMA-cacheability decisions can be tested with the real cache
behavior.

**Phase 2 — Move VC4 mailbox buffer out of ARM-usable RAM.**
- File: `sources/plo/hal/aarch64/generic/video.c`
  (`PLO_RPI_MAILBOX_BUFFER_ADDRESS = 0x02000000`).
- Target: relocate above 3 GiB or into the firmware-reserved top region
  (`0x3b400000 + offset`).
- Update plo's mailbox helpers and re-validate that mailbox traffic
  still works.

**Phase 3 — VC4 quiesce sequence before plo→kernel handoff.**
- After plo's last user-visible mailbox call, send a final sequence that
  sets every VC4 clock except HDMI scanout to off (Linux uses
  `mbox_set_clock_state`).
- Follow with `dsb sy ; isb` before the `eret` to EL1.
- If TD-04-class corruption disappears, that's causal evidence VC4 was
  the writer.

**Phase 4 — DTB-driven memory layout.**
- File: `sources/phoenix-rtos-kernel/hal/aarch64/dtb.c`.
- Implement `/reserved-memory` parsing: every `reg` range becomes
  off-limits to the kernel allocator.
- Implement `/soc/dma-ranges` parsing: expose
  `arm_to_bus_addr(arm_pa) → bus_addr` helper for DMA descriptor
  builders.
- Have plo (or kernel) build syspage memory entries from DTB instead
  of from the hardcoded `0x00400000…0x3b400000` range in
  `sources/plo/ld/aarch64a72-generic.ldt`.
- Drop `SIZE_DDR` from `aarch64a72-generic.ldt` once plo can derive it.

**Phase 5 — Unlock 4 GiB.**
- File:
  `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt`.
- Set `total_mem=4096` and a small fixed `gpu_mem` (e.g. 64 MiB).
- Verify firmware reports `MEM ARM: ~3968 MiB` after reboot.
- Validate boot end-to-end at full DRAM. Watch for TTBR1 map sizing
  regressions — the kernel's TTBR1 may need extending to cover the
  larger physmap.

**Phase 6 — DMA correctness audit.**
- Files: `sources/phoenix-rtos-devices/pcie/server/pcie.c`,
  `sources/phoenix-rtos-devices/usb/xhci/xhci.c`, plus any follow-on
  drivers.
- Replace any implicit identity mapping with the ARM↔BUS helper from
  Phase 4.
- Decide cacheability per region: Normal Inner-Shareable Cacheable for
  most allocations with explicit `dma_sync` points; NC only where the
  hardware requires it (legacy framebuffer regions, mailbox buffer).

**Validation per phase:** rebuild + QEMU smoke + one real-Pi netboot.
Manifest after each phase.

**Exit criterion:** real Pi 4 reports ~3.9 GiB usable RAM; xHCI / PCIe
DMA work; mailbox/HDMI still functional; no regression in `(psh)%`
boot speed.

**Risk:**
- Phase 3 (quiesce) could disable HDMI if too aggressive — keep HDMI
  scanout clock alive.
- Phase 5 (4 GiB unlock) likely surfaces TTBR1 sizing gaps and may
  re-expose TD-04-class corruption in regions newly added to the
  kernel map. Stage cautiously.

### Stage 3 — SMP bring-up (cores 1-3) (2-3 sessions)

**Prerequisites:** Stage 1 (caches enabled, IS-shareable attributes)
and Stage 2 (per-core stack regions in 4 GiB physmap).

**Sub-steps:**

1. **Replace TD-13 single-core atomic fallback** in
   `sources/phoenix-rtos-kernel/lib/lib.h` and `hal/aarch64/_armv8.S`
   with the `__atomic_*` builtins (LDXR/STXR-based). Verify the
   exclusive monitor works across cores. (TD-11 also needs revisit.)

2. **Secondary-core release.** BCM2711 firmware exposes PSCI
   `CPU_ON`; use that. Spin-table fallback exists for boards without
   PSCI but is not needed here.

3. **Per-core state.** Stack, MPIDR_EL1-keyed lookup, separate IDLE
   thread. Phoenix kernel already has multi-core scheduler hooks; wire
   them up.

4. **GIC-400 per-core.** Each core's redistributor (CPU interface)
   must be initialized. SGI delivery (software-generated interrupt)
   is needed for IPI and TLB shootdown.

5. **TLB broadcast audit.** Sweep all kernel mappings to ensure
   they use the Inner-Shareable attribute, so `tlbi vmalle1is` reaches
   all cores. (Likely small — most attrs are already IS once Stage 1
   restructures the bootstrap maps.)

6. **Cache maintenance audit.** Replace any remaining set/way ops
   with PoU/PoC VA-range variants (set/way is unreliable on A72 per
   #851672 and meaningless under SMP).

7. **Smoke test.** `ps` shows threads on multiple cores; mutex stress
   test passes; `pl011_thr` and other server threads observably move
   between cores.

**Exit criterion:** 4 cores running, scheduler distributes threads,
mutex stress test runs for >5 minutes without deadlock.

**Resolves:** TD-01 (SMP enable disabled on A72), TD-11 (single-core
spinlock path), TD-13-residual (atomic fallback).

### Stage 4 — HDMI0 text console + USB keyboard (1-2 sessions)

**Prerequisites:** Stage 1 (full speed — fbcon poll runs at native
rate), Stage 2 (DMA-correct VC4 framebuffer mapping), Stage 3 (xHCI
interrupts steered to a non-boot core).

The pieces are already wired end-to-end (see Gate 2 and Gate 3 in
`tracking/current-step.md` history). Suspected current failure is that
plo never populates `syspage->hs.graphmode`, or kernel reads it
corrupted.

**Sub-steps:**

1. **fbcon banner.** Add a one-line UART probe after
   `pl011_fbcon_init()` to confirm whether it returned `-ENOSYS`
   (graphmode not populated) or actually ran. Path:
   `sources/phoenix-rtos-devices/tty/pl011-tty/pl011-tty.c`.

2. **Fix graphmode handoff** in plo
   (`sources/plo/hal/aarch64/generic/video.c`) or kernel
   (`sources/phoenix-rtos-kernel/hal/aarch64/generic/generic.c`).

3. **xHCI bring-up.** `sources/phoenix-rtos-devices/usb/xhci/xhci.c`
   currently spawns but produces no `main: spawned` follow-up.
   Add init markers, debug to first IPC.

4. **HID keyboard.** `sources/phoenix-rtos-usb/libusb/hid_client.c`
   should publish `/dev/kbd0`; `pl011-tty kbdthr` reads it and feeds
   keys.

5. **Visual smoke.** Type on USB keyboard, see characters echo on
   HDMI display. End-user-observable working system.

**Exit criterion:** boot with no UART connected (only HDMI + USB
keyboard) reaches an interactive `(psh)%` prompt visible on HDMI,
accepts keystrokes from the USB keyboard, executes `help` /
`ls /dev` / `ps` end-to-end.

## Stage-tagged TD ledger

For cross-reference; canonical entries in
`docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`.

| TD | Stage | Notes |
|---|---|---|
| TD-01 SMP disabled | Stage 3 | Resolved by SMP bring-up |
| TD-02 pre-MMU dcache inval disabled | Stage 1 | Subsumed by `__enable_mmu` restructure |
| TD-03 syspage/BSS mapping shortcut | Stage 1 | Cleaned up as part of moving syspage writes pre-MMU |
| TD-11 single-core spinlock path | Stage 3 | Replaced by real LDXR/STXR atomics |
| TD-12 plo memory clamp ~948 MiB | Stage 2 phase 4-5 | Replaced by DTB-driven layout + 4 GiB |
| TD-15 VC6 hygiene + 4 GiB | Stage 2 | Phases 2-6 are the entire stage |
| TD-15-mboxprobe | Stage 2 phase 1 | Already LANDED with `td15:OK` evidence |
| TD-16 timer slowdown | Stage 1 | Resolved by enabling caches |
| TD-16-1 timer probe | Stage 1 | Already LANDED; provides validation criterion |
| TD-16-cache-enable | Stage 1 | Active investigation; restructure pending |

## Cross-cutting concerns

- **Code published publicly** (per CLAUDE.md). Stage 1's restructure is
  the highest-leverage upstreamable change in this roadmap; keep diff
  minimal, comments precise, and avoid reformatting the surrounding
  Phoenix code.
- **Manifests after every validated step.** Use
  `scripts/snapshot-integration-state.sh` and
  `scripts/restore-integration-state.sh` for deterministic rollback.
  Each Stage 1-2 sub-step gets a manifest; Stage 3-4 sub-steps may
  cluster.
- **Diagnostic infrastructure stays in tree across all stages:**
  - No-call early exception dump (kernel `2a5b6a05`) — survives
    recursive faults via direct PL011 MMIO writes.
  - VA-range cache helpers `_clean_inval_dcache_range`,
    `_inval_dcache_range`, `_inval_icache_range` (kernel `49ca0c66`).
  - TD-15 phase 1 mailbox-buffer drift probe and TD-16-1 timer probe.
- **Validate timing claims.** Every stage that promises a speed gain
  must produce a TD-16-1-style measurement before/after.
- **Pi 4 cycle budget.** Real-Pi netboot today costs ~7 minutes per
  cycle; with caches on it should cost <30 s. The early Stage 1
  iterations are the most expensive — budget carefully.

## Estimated total scope

~6-10 focused sessions, gated by real-Pi validation at each stage exit.

| Stage | Sessions (est.) | Risk |
|---|---|---|
| Stage 1 | 1-2 | High (architectural refactor) |
| Stage 2 | 2-3 | Medium (multi-phase, but each phase isolated) |
| Stage 3 | 2-3 | Medium (new bring-up territory for Phoenix) |
| Stage 4 | 1-2 | Low (pieces wired, just needs debug + light fixes) |

If Stage 1 doesn't land within 2 sessions, pause and re-evaluate
rather than continuing to iterate. The 5 cache attempts already
captured in the iteration log are sufficient evidence that "more
invalidation tactics" is the wrong axis to vary; only the structural
fix remains.

## Re-verify

- Linux arm64 `__enable_mmu` shape:
  `https://raw.githubusercontent.com/torvalds/linux/master/arch/arm64/kernel/head.S`
  and `arch/arm64/mm/proc.S`.
- FreeBSD arm64 `start_mmu`:
  `https://cgit.freebsd.org/src/tree/sys/arm64/arm64/locore.S`.
- Cortex-A72 errata list (especially #851672 set/way reliability):
  re-check ARM developer site for current revision.
- BCM2711 PSCI version + supported functions: re-check
  `https://github.com/raspberrypi/firmware` for the active armstub.
- Pi 4 firmware `total_mem` / `gpu_mem` semantics: re-check
  `https://www.raspberrypi.com/documentation/computers/config_txt.html`.
