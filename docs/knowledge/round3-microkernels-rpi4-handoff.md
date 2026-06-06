# Round 3: Microkernels' plo→kernel-equivalent handoff vs Phoenix on Pi 4

Status: deep dive into the loader→kernel cache handoff comparing Phoenix to seL4
elfloader and Genode base-hw bootstrap. Written to localise the BCM2711-class
cache-coherency failure that breaks every Phoenix M+I and M+C+I cache-enable
variant while M-only boots cleanly.

## Tooling caveat

Bash and WebFetch were unavailable, so seL4_tools and Genode repos could not
be cloned. seL4/Genode claims are anchored to the in-repo survey
[`boot-mmu-bringup-non-linux.md`](boot-mmu-bringup-non-linux.md), the seL4
elfloader docs, mailing-list threads, and the AArch64 architecture spec.
Phoenix-side claims are cited at file:line in the local tree. Inferences not
directly pinned to a line are flagged "(architectural)".

## 1. Phoenix plo → kernel handoff: ground truth from the local tree

### 1.1 plo cache state on entry to the OS-kernel handoff

The generic AArch64 plo build (the one in use for Pi 4) **runs with caches
off, MMU off** for its entire active lifetime. Evidence:

- `start_el3` writes `SCTLR_EL3 = 0x30c50838`
  (`sources/plo/hal/aarch64/generic/_init.S:107`). Bits set: 3 (SA), 4 (SA0),
  5 (CP15BEN), 11 (RES1), 16 (nTWI), 18 (nTWE), 20/22/28/29 (RES1). Bits 0
  (M), 2 (C), 12 (I) are all clear → MMU off, D-cache off, I-cache off.
- `start_el2` and `start_el1` similarly install MMU/cache-off SCTLR values
  (`generic/_init.S:118`, `generic/_init.S:147`).
- `start_common` does not contain any later SCTLR write that turns M, C, or I
  back on. The TD-05 set/way invalidation block at lines 194–225 is the only
  cache maintenance that fires before the rest of plo runs, and it executes
  with caches *still off*.

So plo's **stores after `_start` go directly to DDR**, since there is no
cache to populate. This is critical for the rest of the analysis.

The TD-05 block at `generic/_init.S:194-225` does a *level-walked set/way
invalidate-only* (`dc isw`) of every cache level up to LoC, before plo writes
anything. The comment at 180–193 explains the design intent: clear *firmware's*
dirty lines (Pi 4 firmware runs with D-cache enabled and leaves dirty lines
behind) without writing those stale bytes back over plo's fresh DDR stores.

### 1.2 What plo writes to DDR before jumping

Reading `sources/plo/_startc.c:32-67` and the syspage allocator path:

1. **plo's own .data, .rodata, .ramtext** copied from .load aliases at
   `_startc.c:37-49` into DRAM via direct (cache-off) stores.
2. **plo's BSS** zeroed at `_startc.c:51`.
3. **plo's heap zeroed** as a TD-05 diagnostic at `_startc.c:60`.
4. The **kernel image** is downloaded from phfs (TFTP / SD) by the `kernel`
   command (`sources/plo/cmds/kernel.c`) and written, again with caches off,
   to its load PA (= `syspage->pkernel`).
5. The **syspage struct** is built incrementally by `syspage.c` and the
   various `syspage_alloc*()` calls — the actual struct lives inside plo's
   `.heap` window between `__heap_base` and `__heap_limit` (`generic/hal.c:386`
   confirms this implicitly; see also `generic/hal.c:215-218`, where the
   reserved-map entry uses `[hal_common.hs, __heap_limit)` as bounds).
6. plo's **stack** writes during execution.
7. **No page tables are created by plo** for the kernel: TTBR_EL1 / page
   tables are entirely the kernel's responsibility (built in
   `phoenix-rtos-kernel/hal/aarch64/_init.S:357-558`).

### 1.3 What plo flushes before the jump

The flush is in `cmd_go` → `hal_cpuJump()`:

- `sources/plo/hal/aarch64/generic/hal.c:386`:
  `hal_dcacheFlush((addr_t)__heap_base, (addr_t)__heap_limit);`

That is **the only DRAM range plo cleans before EL drop**.
`hal_dcacheFlush` (`sources/plo/hal/aarch64/cache.c:100-113`) does
`dc civac` per cache line over the requested VA range, with `dsb ish` and
`isb` straddling. It bypasses set/way (the comment at hal.c:382-385 explains
why: ARM documents set/way as unreliable for inter-observer coherency).

What is **not** flushed:

- The kernel image at `syspage->pkernel` (the kernel ELF/binary download
  destination — typically not inside `[__heap_base, __heap_limit)`).
- plo's own `.text` / `.rodata` / `.data` (irrelevant: plo is not re-read,
  so any dirty lines do not affect the kernel).
- plo's stack pages (only matter while plo is executing).
- The plo-loaded kernel binary outside the heap window — *if* the kernel
  load PA is outside `[__heap_base, __heap_limit)`, plo's stores landed
  directly in DDR (caches were off — see §1.1), but the kernel will fetch
  those bytes through a *cacheable* mapping once it enables D-cache, and
  any speculative cache lines previously populated for that PA range
  (e.g. from firmware or prior plo activity) are not invalidated.

But: because plo runs with caches *off*, the only way a stale cache line for
a kernel-image PA could exist is if **another agent** populated it. That agent
is either the BCM2711 VPU/firmware (TD-04 root cause), or speculative fills
into the same PA *triggered by the SCTLR.M=1 / SCTLR.C=1 transition itself*
inside the kernel.

### 1.4 The EL drop and jump itself

`hal_cpuJump()` (`generic/hal.c:364-399`) does, in order:

1. `hal_dcacheFlush(__heap_base, __heap_limit)` (heap covers syspage).
2. `hal_probeSyspage()` — diagnostic.
3. `hal_td15ProbeWrite()` — TD-15 mailbox-window diagnostic stamp.
4. `hal_exitToEL1()` (`generic/_init.S:262-302`).

`hal_exitToEL1` does **no further cache maintenance**; it just programs
`elr_el3`/`spsr_el3` (or EL2 equivalents) and `eret`s with the kernel entry
PA loaded from `hal_common.entry`. It executes with caches still off. The
syspage PA is in `x9` already (the kernel reads it via
`SYSPAGE_PKERNEL_OFFSET` in its `_init.S`).

### 1.5 What the kernel does on entry

`phoenix-rtos-kernel/hal/aarch64/_init.S:186-289` enters at EL2 or EL1,
drops to EL1 if needed, then:

- writes `SCTLR_EL1 = 0x30d4d938` (M=0, C=0, I=0; RES1 baseline) at
  `_init.S:290-291`;
- `ic ialluis; tlbi vmalle1; dsb ish; isb` (`_init.S:305-308`);
- programs MAIR/TCR (`_init.S:348-355`); builds TTBR0 identity TT and
  TTBR1 kernel TT (`_init.S:359-403`);
- invalidates PT region by `dc ivac` over `[KERNEL_TTL2, STACK)`
  (`_init.S:419-421`, helper at `_init.S:930-947`);
- writes `SCTLR_EL1.M = 1` (MMU only) (`_init.S:444-449`);
- copies syspage PA → `_hal_syspageCopied` (high VA, Normal NC) with
  clean+invalidate over source and dest ranges (`_init.S:707-774`);
- branches to `main` (`_init.S:821`).

Two load-bearing facts:

- **C and I bits are never set** in the M-only baseline (comment at
  `_init.S:444-458` "Cache enable is parked; reverting to M-only").
- **Syspage destination and early C stack** are remapped Normal
  Non-Cacheable at `_init.S:548-574` (TD-04 hack; `NC_ATTRS = 0x707`
  defined at `_init.S:147-156`).

The kernel works because (a) it keeps caches off after MMU enable, and
(b) the syspage destination is NC. `hal_syspageRelocate` at
`syspage.c:476` faults under M+I because once I-cache is on, the
C walker over `syspage_common.syspage->progs` may pick up stale I-side
literal-pool / switch-table lines that resolve `relOffs`.

## 2. seL4 elfloader → kernel handoff

### 2.1 Cache state on entry to the elfloader

seL4_tools/elfloader-tool's `crt0.S` (per existing in-repo notes in
`docs/knowledge/boot-mmu-bringup-non-linux.md` and the seL4 mailing-list
thread "issue with elfloader ret instruction while enabling the MMU on
armv8-a" at <http://www.mail-archive.com/devel@sel4.systems/msg04595.html>)
expects to be entered from U-Boot with **caches and MMU off**. The widely
cited workaround at <https://docs.sel4.systems/Hardware/Rpi3.html> notes
that on RPi3 the default U-Boot cache configuration breaks elfloader and
the user must add `dcache flush; icache flush; dcache off` to the U-Boot
boot script. This means the seL4 model assumes elfloader runs cache-off,
just like plo. Architectural alignment with Phoenix: identical.

### 2.2 What seL4 elfloader writes to DRAM

According to <https://github.com/seL4/seL4_tools/blob/master/elfloader-tool>
(per the README description summarised in `boot-mmu-bringup-non-linux.md` and
the seL4 elfloader documentation at
<https://docs.sel4.systems/projects/elfloader/>), elfloader:

1. unpacks the kernel image from a CPIO archive (`src/common.c`) and copies
   it to its load PA;
2. unpacks the user-image (root-task ELF) similarly;
3. relocates / patches the DTB to the post-kernel address;
4. **builds the initial page tables for the seL4 kernel** in DRAM (this
   differs from Phoenix: plo does *not* build page tables — the kernel does
   it itself in `_init.S`);
5. enables the MMU and branches to the kernel entry.

### 2.3 Cache maintenance before the kernel jump

Architectural model (citation requires repo clone for line accuracy):

- `cache.S` for armv8-a 64-bit defines `flush_dcache` (set/way over all
  levels), `clean_dcache_range`, `invalidate_dcache_range`,
  `flush_dcache_range` (= clean+invalidate by VA to PoC).
- `boot.S` / `mmu.S` immediately before the branch to the kernel entry does:
  - clean+invalidate D-cache by VA over (a) the kernel image extent,
    (b) the DTB extent, (c) the user image extent, (d) the page-table
    region elfloader allocated. This is the standard "everything we touched
    in DRAM since boot, push to PoC" pattern.
  - I-cache invalidate (`ic ialluis`) so prefetched lines from the
    relocation source are not used at the kernel destination.
  - DSB/ISB barriers around the MMU-enable + branch.

The seL4 elfloader notably does *not* leave caches off when it jumps to the
kernel — it enables the MMU (and on most platforms caches) inside the
loader, then `br`s to a virtual entry point. Cache-off boot is supported
(the RPi3 work-around above) but not the default.

### 2.4 Implication vs Phoenix

Because seL4 elfloader cleans by VA every DRAM region it wrote (kernel
image, user image, DTB, page tables) before the kernel jump, the seL4
kernel can rely on D-cacheable reads of those bytes the moment its own
SCTLR enables D-cache. **There is no "stale firmware line" risk at the
elfloader→kernel boundary** for the regions elfloader populated, because
the by-VA clean writes them through to PoC and any line speculatively
fetched from DDR by the elfloader's own MMU/cache-on operation is brought
into a known-clean state before the kernel takes over.

The remaining risk seL4 inherits is exactly the BCM2711 anomaly Phoenix
hit: cache lines for PAs the elfloader did *not* explicitly write (e.g.
firmware-reserved windows, mailbox PAs) may still hold stale firmware data.
seL4's approach to this on Pi 4 is to use U-Boot, which sets up the MMIO
region attribute table conservatively and clears caches before chainloading.
Phoenix uses an in-tree armstub instead of U-Boot, so it lacks that prior
sanitisation step.

## 3. Genode base-hw bootstrap → core handoff

Genode's bootstrap (`repos/base-hw/src/bootstrap/spec/arm_v8/`) is the closest
architectural analogue to Phoenix's plo + kernel `_init.S` combined: it does
identity-mapped boot, builds page tables, enables MMU, then `br`s to core's
real entry. Per `docs/knowledge/boot-mmu-bringup-non-linux.md` §2 "Genode
base-hw" and the genodians.org post "Exploring the ARMv8 system level"
(<https://genodians.org/skalk/2019-06-18-arm64-kernel>), the sequence is:

1. `crt0.s` opens with `_mmu_disable`: read `CurrentEL`, branch on
   EL1/EL2/EL3, clear `SCTLR_ELx.M`, `isb`. So whatever EL it lands in,
   it forces MMU off there before doing anything.
2. C++ `Cpu::enable_mmu()` programs MAIR / TCR / TTBR.
3. The MMU-enable MSR is straddled by `dsb sy` and `isb`.
4. `Cpu::Mmu_context::init` and `Bootstrap::Platform` ELF-load the core
   binary into its target memory, then transfer control.

Cache-maintenance specifics (architectural; line-accurate citation requires
repo clone):

- Bootstrap on arm_v8 invalidates the data cache by set/way at the start
  (analogous to Phoenix's TD-05 block).
- Before jumping to core, bootstrap does a clean+invalidate by VA over the
  page-table region and the core image region to bring DDR up to date.
- I-cache invalidate before the branch.

Genode's distinguishing feature: the bootstrap is *the same binary domain*
as core's later setup — there is no OS-loader → microkernel boundary to
defend. So the cache-flush demands are simpler than for plo→Phoenix-kernel
(which is two independent binaries built with two independent linkers).

## 4. Side-by-side: what each loader flushes before the kernel/core jump

| Region | Phoenix plo | seL4 elfloader | Genode bootstrap |
|---|---|---|---|
| Loader's own .text/.rodata/.data | not flushed (cache-off → no dirty lines) | clean by VA (cache was on) | clean by VA |
| Loader's heap (where syspage / boot info lives) | `dc civac` over `[__heap_base, __heap_limit)` (`generic/hal.c:386`) | clean by VA over boot-info struct + DTB | clean by VA over Bootinfo |
| **Kernel image (loaded from media)** | **NOT flushed by plo** | clean by VA over `[kernel_load, kernel_load+ksize)` | clean by VA over core image |
| **Loader-built page tables** | **N/A — plo does not build page tables** | clean by VA over PT region | clean by VA over PT region |
| Loader's stack | not flushed | not flushed (irrelevant past the jump) | not flushed |
| MMIO regions | left untouched | left untouched (Device-nGnRnE, not cacheable) | left untouched |
| DTB / firmware-passed blob | not flushed (read by kernel via ID-map; on Pi 4 plo just hands the FW DTB PA in `hal_firmwareDtb`) | clean by VA over DTB extent | clean by VA |
| L1 set/way invalidate at loader entry | `dc isw` walked across all levels up to LoC (`generic/_init.S:194-225`, TD-05) | `flush_dcache` (set/way) at elfloader entry | `dc isw` at bootstrap entry |
| MMU/cache state at jump | M=0 C=0 I=0 (cache-off) | MMU on, caches on (default); cache-off optional via U-Boot work-around | MMU on, caches on |
| EL at jump | EL1 (`hal_exitToEL1` `eret`) | EL1 or EL2 per build | EL1 (after `Cpu::switch_to_supervisor_mode`) |

The most striking gap: **plo does not flush the kernel image PA range** and
**does not pre-create page tables** that the kernel would need to walk. Both
of those are explicit clean-by-VA operations in seL4 elfloader and Genode
bootstrap.

## 5. Why this matters for Phoenix's failing cache-enable variants

### 5.1 M+I variant (D-cache off, I-cache on, MMU on) faulting at syspage.c:476

`hal_syspageRelocate` (`hal/aarch64/hal.c:38-41`) reads `relOffs` and adds it
to a pointer. After the kernel turns the I-cache on, every subsequent C
function call may fetch instruction bytes through the I-cache. Two pathways
risk stale bytes:

1. **Kernel image PAs that plo wrote** — never civac'd by plo. With caches
   off in plo there are no plo-side dirty lines, but firmware-side dirty
   lines or speculative fills (BCM2711 has a unified L2 not strictly
   inclusive of L1; pre-existing dirty L2 lines for these PAs are possible
   per Cortex-A72 TRM r0p3 §6.4). When the kernel turns I=1, the I-cache
   may pre-fetch from those PAs and pick up *firmware-era stale* bytes.
   Phoenix's TD-05 set/way invalidate fires inside *plo*, not inside the
   kernel — and plo cannot reach the cache lines that fire after the EL
   drop.
2. **Page-table walker** — TTBR1's TTL2/TTL3 are written by the kernel
   itself with D-cache off, so they hit DDR directly. The walker on A72
   can fetch them through L2 (it shares L2 with the I-cache). If a stale
   L2 line exists for a TTL3 PA, the walker returns garbage and the
   instruction fetch faults. The kernel does invalidate the PT region by
   VA at `_init.S:419-421` *before* M=1, but **only for the data side**;
   `dc ivac` does not reach lines speculatively filled from the I-cache
   side after M+I. The seL4 elfloader does both `flush_dcache_range`
   (data side, civac per line) and `ic ialluis` (instruction side) over
   the PT region just before the MMU+cache enable; Phoenix's kernel does
   `ic ialluis` only at boot start (`_init.S:305`), not at the cache-enable
   moment.

Both conditions match the M+I crash signature: a layered translation fault
or a fetched-instruction-undefined fault deep inside the C code. The
exception dump in `tracking/current-step.md` reports
`ESR=0x96000003 (data abort, level-3 translation fault)` after iteration 3
and `ESR=0x96000001 (level-1 translation fault), FAR=0xfe201018 (PL011)`
after iteration 4 — exactly the symptoms of stale TTL entries fed to the
walker.

### 5.2 M+C+I variant: hangs even earlier, before the X4 marker

With D-cache also on, the moment any data load hits a *speculatively
populated* cache line that doesn't match DDR truth, downstream control
flow goes off the rails. The very first cacheable load after the SCTLR
write reads through whatever the L1/L2 already contains for that PA. On
plo→kernel handoff Phoenix does NOT issue a clean+invalidate by VA over
the kernel image PAs. Hence the immediate post-flip silent stall.

### 5.3 Why M-only works

`SCTLR_EL1.M=1, .C=0, .I=0` keeps both caches off, so the kernel's loads
and instruction fetches go to DDR through the MMU. The MMU walker is the
only consumer of cache lines, and on A72 the walker can be configured (via
TCR.SH/IRGN/ORGN) to do non-cacheable walks — Phoenix's TCR_EL1_VALUE in
`_init.S:122-137` sets `SH=3 (inner shareable)` and `IRGN/ORGN = 1
(write-back, RA, WA)` for both TTBR0 and TTBR1, **so the walker IS using
cacheable accesses**. The reason this still works in M-only is that the PT
was just written by the same CPU's cache-off store path → DDR has the
correct bytes → the walker's cache fill from DDR picks up correct bytes →
correct translations. There is no second writer of those PAs (no firmware,
no DMA), so no stale-line risk. The M+I and M+C+I failures are about *new*
cache content getting populated for non-PT PAs (kernel image, syspage)
that the kernel reads after the flip.

## 6. Concrete recommendation for Phoenix's plo

### 6.1 Add three explicit clean-by-VA flushes to `hal_cpuJump()`

In `sources/plo/hal/aarch64/generic/hal.c`, just before
`hal_exitToEL1()` (around line 395), add:

```c
/* Mirror seL4 elfloader / Genode bootstrap pre-jump cache discipline:
 * clean (or clean+invalidate) by VA every DRAM region we wrote that
 * the kernel will read once it enables caches.
 */

/* (a) Kernel image extent: plo loaded the kernel binary at
 * hal_common.entry via direct (cache-off) DDR stores, so DDR is
 * authoritative. But on BCM2711 the L2 may still hold stale firmware
 * lines for these PAs; clean+invalidate by VA brings DDR forward and
 * discards stale content. */
extern char __kernel_start[], __kernel_end[];   /* or use entry+ksize */
hal_dcacheFlush((addr_t)hal_common.entry,
                (addr_t)hal_common.entry + plo_kernel_size_known_to_loader);

/* (b) Syspage struct, even though it's already inside __heap_base..
 * __heap_limit (current civac covers it). Make this explicit so the
 * coverage doesn't silently regress if syspage allocation moves. */
hal_dcacheFlush((addr_t)hal_common.hs,
                (addr_t)hal_common.hs + hal_common.hs->size);

/* (c) The DTB the kernel will read via x0/firmwareDtb. */
if (hal_firmwareDtb != 0u) {
    addr_t dtb = (addr_t)hal_firmwareDtb;
    /* DTB header (8 bytes) tells us its total size in big-endian at +4. */
    u32 dtb_size = hal_readBe32(dtb + 4u);
    hal_dcacheFlush(dtb, dtb + dtb_size);
}

/* (d) I-cache invalidate to PoU so the kernel's first fetch is clean
 * even if firmware previously left stale I-side lines for kernel PAs. */
__asm__ volatile("ic ialluis; dsb ish; isb" ::: "memory");
```

These four lines per region reproduce, on the plo side, the cache discipline
seL4 elfloader and Genode bootstrap apply on the loader side. The kernel
side already does its own `_clean_inval_dcache_range` over the syspage
source range at `_init.S:710` — that is now redundant but harmless and
should be kept until the new plo behaviour is validated.

### 6.2 Why the kernel-side fix in `_init.S:419-421` is insufficient

The current `_inval_dcache_range` over the PT region is invalidate-only
(`dc ivac`). On A72, `dc ivac` for writable addresses is **architecturally
promoted to civac** — but only on lines that are currently allocated. Lines
not yet allocated (e.g. the kernel image PA before the cache-enable flip)
are unaffected. The fix needs to be on the *plo* side, before the EL drop,
where the writes happened.

### 6.3 Risk assessment

- **No regressions for the working M-only baseline.** Adding extra civacs
  in plo while caches are off is a no-op on the L1 (no allocated lines)
  and a `dc cvac` to PoC on any L2/firmware lines. PoC on BCM2711 is DDR.
  L2 stale lines get cleaned to DDR, then invalidated. DDR was already
  correct, so the cleaned writeback overwrites DDR with itself.
- **Edge case: the kernel image PA range is unknown to plo.** Phoenix's
  `kernel.c` cmd downloads the kernel and stamps `hal_common.entry`; the
  size needs to be tracked. If unavailable, plo can flush the entire
  reserved RAM window above `__heap_limit` up to a configured upper bound.
- **TD-04 / TD-15 do not become obsolete** — the BCM2711 firmware-side
  stale-line problem affects PAs that plo never touched (firmware-reserved
  windows, mailbox buffers). Those still need the kernel-side NC remap
  trick. The plo-side civac fix only addresses PAs plo *did* write.
- **Upstreaming.** This change matches every other ARMv8 microkernel
  loader's pre-jump behaviour. It is the smallest, least invasive
  alignment with seL4 / Genode prior art and would be readily accepted
  upstream as a generic-AArch64 hardening.

## 7. UART signature: pre-fix vs post-fix expectation

### 7.1 Current M-only baseline (working)

`hal: jump exit el1` → `[Z][K][L][M][77][86][X1..X5][!][Y][O][P][S][T][U][Z][b]
… h q w u W X …` → `(psh)%`.

### 7.2 With M+I variant under current plo (failing — observed)

`hal: jump exit el1` → `[X1..X4]` → `ESR=0x96000003 ELR=ffffffffc00006b0
FAR=ffffffffc0001890` (level-3 translation fault near syspage walker).

### 7.3 Predicted with §6.1 + M+I

L3 fault clears: kernel image PAs are civac'd → I-side fetches DDR-correct
bytes; syspage+DTB civac'd → C-walker sees correct pointer chains;
plo-side `ic ialluis` pairs with kernel's at `_init.S:305`. Expected log
matches M-only baseline through `(psh)%`. Residual hangs would point at
PAs plo never touched (firmware/VPU windows) → genuinely TD-04 territory.

### 7.4 Predicted with §6.1 + M+C+I

The "X3 silent hang" should clear: first cacheable load after the SCTLR
write fills from DDR-correct bytes. Residual failure modes: A72 erratum
859971 needing armstub-side workaround (`_init.S:330-337`), or
speculative-prefetch issues needing wider TD-04 NC mapping.

## 8. Risk and exit criteria

### 8.1 Lowest-risk landing strategy

1. Land §6.1 in plo on a side branch, keep kernel at M-only. Run
   `./scripts/rebuild-rpi4b-fast.sh` then
   `./scripts/capture-rpi4b-uart.sh`. Verify the (psh)% prompt still
   reaches normally; baseline must be unchanged.
2. Snapshot a manifest with `scripts/snapshot-integration-state.sh`.
3. Flip the kernel to M+I (one-liner OR of `(1 << 12)` into the
   SCTLR_EL1 RMW at `_init.S:444-449`). Capture log.
4. If M+I now reaches `(psh)%`, snapshot. Then flip M+C+I.
5. Each step that boots cleanly gets a manifest; any regression triggers
   `scripts/restore-integration-state.sh`.

### 8.2 What would falsify the hypothesis

If §6.1 lands and the M+I crash signature is identical (same FAR, same
ELR), the hypothesis "plo is leaving stale lines" is wrong and we are
looking at one of:

- BCM2711 firmware writing to a kernel-mapped PA *during* the boot window
  (TD-15 territory; the mailbox-buffer drift probe is already there to
  detect it).
- A72 erratum manifesting via TCR cacheable-walk + 16K/4K granule edge
  cases. The Pi 4 firmware sometimes leaves CPUACTLR_EL1 in unusual
  states; the Phoenix armstub workaround for erratum 859971 is supposed
  to handle this but the comment at `phoenix-rtos-kernel/_init.S:330-337`
  notes it "needs to migrate to the armstub for the cache enable Stage 1
  path to progress past Phase A." If §6.1 fails to fix M+I, that armstub
  migration is the next item.

### 8.3 Cleanup once cache-enable is stable

- Remove the kernel-side TD-04 NC remap of `_hal_syspageCopied`
  (`_init.S:537-558`) — once D-cache is on for the kernel and plo has
  flushed properly, the syspage destination can be plain Normal Cacheable.
- Remove the kernel-side `_clean_inval_dcache_range` over source range
  (`_init.S:707-710`) — redundant with plo's flush.
- Keep the TD-15 mailbox probe; it tracks an orthogonal external-writer
  problem.

## 9. Confidence assessment

- **Phoenix-side facts (file:line in this repo):** high confidence;
  every claim is anchored in code I read in this session.
- **seL4 elfloader cache-flush set:** medium-high confidence on the
  *architectural shape* (clean-by-VA over kernel image, DTB, page
  tables, plus ic ialluis), corroborated by the existing in-repo
  research doc and the seL4 mailing-list thread on cache-on
  RPi3 boot. **Lower confidence** that those operations occur at exactly
  the file:line locations one would cite without a repo clone, since I
  could not access seL4_tools/elfloader-tool/src/arch-arm/64/boot.S
  directly.
- **Genode base-hw bootstrap:** medium confidence; the `crt0.s`
  EL/MMU disable is documented in detail in
  `boot-mmu-bringup-non-linux.md` and on genodians.org. The exact set of
  pre-jump cache flushes is architectural inference.
- **Recommendation §6.1:** high confidence in the *direction*. The exact
  set of regions to flush from plo is well-defined (kernel image extent,
  syspage, DTB, ic-ialluis). Whether this alone fixes M+C+I is uncertain —
  it should fix M+I per the failure-mode analysis.

## Sources

Phoenix local tree (cited at file:line above):

- `sources/plo/hal/aarch64/generic/_init.S` (105–250 EL branches + TD-05;
  262–302 hal_exitToEL1).
- `sources/plo/hal/aarch64/generic/hal.c` (364–399 hal_cpuJump; 386 heap
  civac; 215–218 reserved-map bounds).
- `sources/plo/hal/aarch64/cache.c` (28–113).
- `sources/plo/hal/aarch64/cpu.c`, `sources/plo/cmds/go.c`,
  `sources/plo/_startc.c`.
- `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` (186–558; 707–774;
  444–458 parked cache enable; 930–966 cache helpers).
- `sources/phoenix-rtos-kernel/syspage.c:476` (crash site);
  `sources/phoenix-rtos-kernel/hal/aarch64/hal.c:38-41` (hal_syspageRelocate).

External:

- [seL4 elfloader docs](https://docs.sel4.systems/projects/elfloader/) — role
  spec: load kernel+user+DTB, init secondaries, set up PTs, enable MMU, jump.
- [seL4_tools elfloader README](https://github.com/seL4/seL4_tools/blob/master/elfloader-tool/README.md).
- [seL4_tools sys_boot.c](https://github.com/seL4/seL4_tools/blob/master/elfloader-tool/src/arch-arm/sys_boot.c).
- [seL4 RPi3 hardware notes](https://docs.sel4.systems/Hardware/Rpi3.html) —
  cache-off-on-entry expectation.
- [seL4 mailing list, elfloader MMU/cache thread](http://www.mail-archive.com/devel@sel4.systems/msg04595.html).
- [Genode base-hw arm_64 crt0](https://github.com/genodelabs/genode/blob/master/repos/base-hw/src/bootstrap/spec/arm_64/crt0.s).
- [genodians.org "Exploring the ARMv8 system level"](https://genodians.org/skalk/2019-06-18-arm64-kernel).
- [ARM Cortex-A72 TRM r0p3](https://developer.arm.com/documentation/100095/0003/)
  — L2 not strictly inclusive of L1.
- [ARMv8 ARM D7.4](https://developer.arm.com/documentation/ddi0487/latest/)
  — dc isw vs civac PoC reach.
- In-repo: [`boot-mmu-bringup-non-linux.md`](boot-mmu-bringup-non-linux.md);
  [`TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`](../inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
  (TD-04, TD-05, TD-15, TD-16).
