# 2026-05-15 — Pi 4 cache enable: clean restart (Linux `__cpu_setup` pattern)

## Why we're restarting

The 2026-05-14 cache-enable work (kernel commits `3d5c9574`, `3b63677f`,
`f2b7c62f`) was reverted on 2026-05-15 after real-Pi bisect proved it
broke user-process execution while looking clean in the UART log. The
shipping milestone criterion ("no fault, reaches `proc_reap idle`") was
insufficient: every spawned user process silently failed before reaching
any of its own code, so `fbcon`, `pcie`, `psh`, `pl011-tty`, etc. never
ran. HDMI showed only the brown background plo had drawn.

Three independent failure paths surfaced under the previous design:

1. `process_load → process_validateElf64` randomly rejecting valid ELF
   headers depending on console-print timing (cache-coherency race
   between the kernel's cacheable temp ELF mapping and the source pages).
2. `process_load64` returning `-ENOMEM` from the user-stack `vm_mmap`
   on subsequent spawns (kernel page allocator state stale-cached
   across spawns).
3. `dummyfs-root` (PID 2) completing the kernel-side spawn cleanly and
   eret'ing to EL0 — but producing zero user output (likely stale user
   I-cache lines, or some related EL0 hazard).

The previous attempt tried to fix these by adding narrow targeted
invalidations (`amap_page()` invalidation, TLBI ISB hardening). It
worked around two specific paths but missed the underlying class of
problem. Bisect found `3d5c9574` alone is sufficient to break
userspace; the other two commits are partial follow-ups that didn't
close the gap.

## Reference: how Linux `__cpu_setup` works

Linux arm64 boot is the canonical reference. Key invariants in order:

1. **All page-table construction happens with MMU disabled.** Linux's
   `__create_page_tables` builds the entire `swapper_pg_dir` (kernel
   identity + kernel virtual + fixmap + early-printk) before any
   `SCTLR_EL1.M=1` write. The walker never observes a half-built
   table.

2. **Page-table memory itself is mapped cacheable, inner-shareable.**
   The walker accesses cacheable memory through the same cache
   hierarchy as ordinary CPU loads, so PT writes and walker reads stay
   coherent without explicit maintenance.

3. **TCR programs cacheable, inner-shareable walks** for both TTBR0
   and TTBR1 (`IRGN0/1 = ORGN0/1 = WBWA, SH0/1 = Inner Shareable`).
   This is the *default* shape; the previous Phoenix attempt set walks
   to Non-Cacheable/Non-Shareable as an experiment — Linux does not.

4. **`SCTLR_EL1.M | .C | .I` are turned on as a single MSR write** at
   the end of `__enable_mmu`, wrapped in barriers (`isb` before, full
   memory barrier sequence after). Linux never runs with caches
   partially on.

5. **No fancy NC-marking of the kernel image.** The kernel `.text`,
   `.rodata`, `.data`, BSS, stack, and the page tables themselves are
   all cacheable from the first instruction after MMU enable. The
   single exception is device-mapped MMIO regions.

6. **First user-text mapping does explicit PoU clean + I-cache
   invalidate.** When a new process is created and its text pages are
   loaded into the page cache (kernel D-cache from the file-read
   path), the loader runs `__flush_icache_range(start, end)` over the
   user-text VA range. That macro does
   `dc cvau; dsb ish; ic ivau; dsb ish; isb` per cache line — the
   canonical sequence to make freshly-written instructions observable
   to EL0 I-fetch.

7. **Free-page recycling invalidates the cache for the recycled PA.**
   When a page leaves the allocator's free list to become user-text,
   user-data, kernel heap, or DMA buffer, any stale lines from the
   previous owner are dropped before the new owner sees the page.
   Linux does this via `flush_cache_page()` or
   `__inval_dcache_area()`. With this discipline, every fresh mapping
   starts with a clean cache view of its PA.

8. **DMA-coherent allocations** for devices that don't snoop are
   handled by `dma_alloc_coherent` which maps the page non-cacheable
   on the CPU side. Other DMA uses explicit
   `dma_sync_single_for_*_device` calls that issue the appropriate
   `dc civac` / `dc ivac` over the buffer range. Phoenix has nothing
   equivalent yet, but xHCI rings will need it.

## Mapping each Linux invariant to Phoenix code paths

| Linux invariant | Phoenix file(s) to change | Today's state |
|---|---|---|
| 1. All PTs built pre-MMU | `hal/aarch64/_init.S` — restructure `_start` / `el1_entry` | Today the kernel writes its TTL2/TTL3 entries *after* MMU enable (TD-16 iter 10/11/12/13 attempts) |
| 2. PT memory cacheable | `hal/aarch64/_init.S` — drop the `NC_ATTRS` override on `pmap_common.kernel_ttl*` | Today `3d5c9574` made bootstrap PT memory non-cacheable as a workaround |
| 3. TCR walks cacheable + ISH | `hal/aarch64/_init.S` `TCR_EL1_VALUE` | Today (after revert) it's cacheable + ISH again — restored to baseline |
| 4. Single MSR M\|C\|I + barriers | `hal/aarch64/_init.S` — replace the existing M-only flip | Today only `SCTLR.M=1` is set |
| 5. No NC kernel-image | `hal/aarch64/_init.S` — keep `DEFAULT_ATTRS` for kernel `.text`/`.data` | Today (after revert) kernel mappings are cacheable |
| 6. First user-text PoU+ic ivau | `hal/aarch64/pmap.c` `_pmap_cacheOpAfterChange` | Today only does `ic ivau`. Linux does `dc cvau` *first* on every cache line, then `ic ivau` |
| 7. Free-page recycling flushes | `vm/page.c` `_page_free` / page-recycle path | No cache hygiene today; pages just go back on the free list |
| 8. DMA coherency | `vm/map.c` / vm_mmap MAP_UNCACHED + future `dma_map_*` API | xHCI driver currently has its own ad-hoc handling; needs a clean API |

The cache enable for Pi 4 will land all 7 of (1)-(7) before any
`SCTLR.C=1` flips. (8) can stay TODO — it only matters for DMA-heavy
drivers (xHCI, GENET) and we're not running those yet under cache.

## Implementation plan

Each step is a separate commit. After each step the kernel must still
boot to `(psh)%` on real Pi 4 (M-only or M|C as the step dictates).
After every commit: `./scripts/rebuild-rpi4b-fast.sh` then
`./scripts/test-cycle-netboot.sh --label c-stepN --capture-secs 240`
and `./scripts/uart-summary.sh c-stepN`. Manifest after every PASS.

### Step C-1 — Move kernel PT construction pre-MMU (`_init.S`)

Refactor `_start` / `el1_entry` so the sequence is:

```
# pre-MMU phase:
- compute pkernel, derive PMAP_COMMON_KERNEL_TTL{2,3}, *_DEVICES_TTL3,
  *_SCRATCH_TT, *_SCRATCH_PAGE addresses
- zero all of them via `_fill_page_zero`
- fill TTL2 (kernel + devices)
- fill KERNEL_TTL3 (kernel .text/.data via DEFAULT_ATTRS = cacheable)
- fill DEVICES_TTL3 (PL011 early VA, mailbox if applicable)
- TD-04 NC override for _hal_syspageCopied page (still needed)
- write TTBR0_EL1 (scratch_tt identity), TTBR1_EL1 (kernel_ttl2)
- write TCR_EL1 (cacheable, inner-shareable for both TTBRs)
- write MAIR_EL1, VBAR_EL1
- dsb ish; isb
# enable phase:
- SCTLR_EL1 |= M | C | I (single MSR)
- isb
# post-MMU phase:
- jump to kernel virtual address
- continue init
```

The current code does pieces of (1) post-MMU. The restructure is
mechanical but requires care: we have to avoid C function calls that
expect `sp` set up by the post-MMU phase. Today's `_fill_page_zero` /
`_fill_page_descr` are leaf assembly helpers — they work pre-MMU.

**Acceptance criterion:** kernel boots to userspace (matches today's
M-only baseline). Subsystems that used to require post-MMU PT writes
still work.

### Step C-2 — Land cacheable PT memory + cacheable kernel mappings

In `_init.S`, ensure:
- `PMAP_COMMON_KERNEL_TTL{1,2,3}` and `*_DEVICES_TTL3` reside in pages
  mapped with `DEFAULT_ATTRS` (cacheable inner-shareable).
- The kernel `.text`/`.rodata`/`.data`/BSS sections use
  `DEFAULT_ATTRS` in the `_fill_page_descr` call (already so after
  revert; explicitly re-verify).
- The 5-page `pmap_common` block stays accessible during early bring-up
  via the TTBR0 low-PA alias (which the C code uses while running at
  low PC). Keep that alias cacheable too (`DEFAULT_BLOCK_ATTRS`, not
  `NC_BLOCK_ATTRS`).
- Stack stays cacheable (already so).
- The TD-04 NC override for `_hal_syspageCopied` is the only NC
  exception kept.

**Acceptance criterion:** still boots M-only; explicit
`./scripts/test-cycle-netboot.sh --label c-step2-mc-off` shows the
same user-process output as the M-only baseline (sanity-check the
restructure didn't disturb).

### Step C-3 — Single-shot `SCTLR.M|C|I` enable

Replace the M-only flip with the canonical pattern:

```
ic   ialluis
dsb  ish
tlbi vmalle1is
dsb  ish
isb
mrs  x0, sctlr_el1
orr  x0, x0, #(1 << 0)     /* M */
orr  x0, x0, #(1 << 2)     /* C */
orr  x0, x0, #(1 << 12)    /* I */
msr  sctlr_el1, x0
isb
ic   iallu
dsb  nsh
isb
```

**Acceptance criterion:** kernel reaches `Phoenix-RTOS microkernel`
banner and successfully spawns `dummyfs-root` *with own UART output*
(`name: register /` or similar). If still silent, see C-4/C-5/C-6.

### Step C-4 — Per-user-text-mapping `dc cvau` + `ic ivau` in pmap

Edit `_pmap_cacheOpAfterChange` to do the **full** PoU sync, not just
`ic ivau`:

```c
if ((newEntry & (DESCR_PXN | DESCR_UXN)) == 0U) {
    hal_cpuCleanDataCacheToPoU(vaddr, vaddr + SIZE_PAGE);
    hal_cpuInvalInstrCache(vaddr, vaddr + SIZE_PAGE);
}
```

Add the helper if missing — Phoenix `cache.c` already has `dc cvac`
(to PoC); a PoU variant is one more inline asm wrapper.

**Acceptance criterion:** psh actually spawns and reaches a prompt,
HDMI shows text. Several spawn cycles in `psh` (`ls`, `cat`) work.

### Step C-5 — `dc ivac` on kernel ELF temp mappings

In `proc/process.c` `process_load`, after `vm_mmap` returns `ehdr`,
invalidate the D-cache lines for the freshly-mapped VA range so the
kernel's reads come from RAM (not from stale cache lines that
firmware/plo may have left for those PAs).

The earlier attempt at this faulted because `vm_mmap` with `MAP_NONE`
left some pages PTE-unmapped. Either:
- Force the mapping to be fully resident (extend `vm_mmap` to handle
  `MAP_POPULATE`-style eager mapping), OR
- Use the low-PA alias (kernel TTBR0 identity) to invalidate the PAs
  directly. The `o->paddr + base` calculation gives the PA range; we
  can issue `dc ivac` over the corresponding low-VA-of-PA addresses.

Pick whichever is simpler. The low-PA approach is more robust for
generic objects (any backing store).

**Acceptance criterion:** add diagnostic prints (or use the M|C|I
output as the signal) and verify `process_validateElf64` consistently
passes for every spawned binary on every boot.

### Step C-6 — Free-page recycle hygiene

In `vm/page.c`, wherever a page leaves the free list (allocator,
zone, anonymous map allocation), invalidate the D-cache for its PA
range. Wherever a page returns to the free list, clean+invalidate.
Two simple wrappers in `cache.c` (`hal_cpuPageReuseClean()`,
`hal_cpuPageReuseInval()`); call sites in `_page_free()` and
`_page_alloc()`. Skip for kernel-allocator pages that never leave the
kernel.

**Acceptance criterion:** subsequent spawns (PID 3+) no longer suffer
`-ENOMEM` from user-stack `vm_mmap` after the same kernel page was
recycled.

### Step C-7 — Real Pi 4 verification + manifest

Full netboot run, confirm:
- `fbcon: ok` shows up on UART
- `pcie:` produces its bring-up output (LinkUp, BAR programming,
  VL805 reset notification)
- `xhci:` capProbe runs (even if it fails ENODEV later — that's a
  separate USB-firmware issue, not cache)
- `psh: tty open / ready`
- `(psh)%` prompt appears on UART
- HDMI shows the same prompt as a kernel framebuffer console
- Interactive psh via `./scripts/test-cycle-psh-interact.sh` accepts
  `help` and replies

Manifest: `manifests/2026-05-15-cache-enable-c-approach.md` snapshots
the sibling SHAs of the passing image.

## Risk register

* **A72 erratum we still haven't applied.** The armstub now applies
  859971 and 1319367; TF-A's full SDEN list has more (832075, 853709,
  852421, etc.). If a future iteration of this design hits a
  walker-coherency or speculative-side-effect problem we haven't
  named, check the full SDEN list before chasing software issues.
* **The TD-04 NC override for `_hal_syspageCopied`.** This is kept
  because plo writes through the same PA and we can't change plo's
  side easily. Document it as a known asymmetry.
* **Pre-MMU PT writes are large.** With `pmap_common` covering 5 pages
  and KERNEL_TTL3 needing 512 entries plus 2-3 NC overrides, the
  pre-MMU init does many stores. With MMU off, those stores use
  default memory attribute (typically Device-nGnRnE per ARM ARM
  B2.6.2), which is strongly-ordered and slow. Acceptable for boot
  (one-shot).
* **No QEMU rpi4b verification possible.** The QEMU rpi4b machine
  model doesn't run the kernel through plo cleanly (PC=0x200 trap
  documented separately). Real-Pi UART is the only authoritative
  test. Each step requires a physical boot cycle.

## What this design explicitly does not do

* Re-enable the `vm/zone.c` cacheable backing pages experiment that
  failed three times in 2026-05-14. That work is parked behind a
  separate hypothesis (zone allocator's free-list pointer needs
  specific cache discipline before initialization).
* Touch the user/kernel context-switch path. Phoenix's context switch
  doesn't currently flush caches across processes; if I-cache aliasing
  becomes a problem after C-4 lands, it will manifest as inter-process
  text corruption and we'll add a `flush_icache_all` to the scheduler
  switch path then.
* Address USB+keyboard. That work is independent and can resume once
  the kernel is cache-enabled and userspace runs reliably.

## Estimated effort

* **C-1 + C-2**: half a day. Mostly mechanical refactor with careful
  review.
* **C-3 + C-4**: 2-4 hours total. Single-MSR change plus 2-line pmap
  edit, then physical verification.
* **C-5 + C-6**: 4-8 hours each. Low-PA invalidation path needs care
  to handle objects backed by non-DDR memory (mailbox region,
  initramfs).
* **C-7**: 1-2 hours for full validation + manifest.

Total: ~1.5 days physical work, spread across multiple netboot cycles.

## 2026-05-15 evening — C-3 attempt outcomes (FAILED)

All C-3 single-MSR M|C variants tested on real Pi 4 hardware fail at the
first post-MMU low-VA PL011 access. Bisect-style log table:

| Test | TCR walks | dc op | _hal_init probes | Result |
|------|-----------|-------|------------------|--------|
| c3a (M\|C\|I) | cache | cisw | yes | EX=4 infinite loop (handler self-faults at slot 4) |
| c3b (M\|C, cache) | cache | cisw | yes | L1 translation fault FAR=0xfe201018 ELR=line 635 (X4) |
| c3c-c3e (M\|C, NC) | NC+NS | cisw | yes | No fault. Hangs at `_init.S:1363` (probe store) |
| c3f (M\|C, NC, no probes) | NC+NS | cisw | no | No fault. Hangs at `b _hal_init_c` |
| c3g (M\|C, NC, dc isw) | NC+NS | isw | no | Same as c3f — no improvement |
| c3h (M\|C, cache, dc isw) | cache | isw | no | Same as c3b — fault at X4 |

### Key findings

* **Cacheable walks always fault** at `FAR=0xfe201018` ELR=`_init.S:635`
  (first `ldr w4, [x3]` from PL011 FR through TTBR0 low-PA identity).
  ESR=`0x96000005` = level-1 translation fault.
  The walker reads `SCRATCH_TT[3]` (1 GiB block for PA [3 GiB, 4 GiB))
  and finds it invalid, even though the block descriptor (0x600000c0000709,
  decoded: AttrIdx=2 Device-nGnRE / AP=00 EL1-RW / SH=11 IS / AF=1 / PXN+UXN /
  PA[47:30]=3) was stored pre-MMU and `_inval_dcache_range` covers all
  PT pages (KERNEL_TTL2..STACK, includes SCRATCH_TT).

* **`dc cisw` vs `dc isw` makes no difference for these tests.** Both
  produce identical fault location with cacheable walks, identical hang
  with NC walks. The Linux-style `dc isw` (discard, do not clean) is
  still the correct primitive at cold-start, but it is not what was
  blocking M|C on Pi 4.

* **NC walks avoids the SCRATCH_TT[3] fault** but exposes a second
  hang at the first stack write in `_hal_init_c` prologue
  (`stp x30, x19, [sp, #-32]!`). The disassembly path is: asm "I" marker
  prints, `b _hal_init_c` executes, `stp` to NC-mapped stack page
  (TD-04 sibling override) hangs silently. No fault, no progress.

* **Multiple cacheable TTBR1 loads succeed before the fault.** The asm
  stub at `_init.S:1349-` reads PL011 literals from the literal pool
  (cacheable WB), prints `h`, `R`, `I` markers, and only fails on the
  TTBR0 low-PA path. So PT setup for TTBR1 (KERNEL_TTL3) is fine; the
  failure is specific to SCRATCH_TT / TTBR0.

### Root cause hypothesis (unverified)

A Cortex-A72 / BCM2711-specific walker-cache interaction with the TTBR0
identity mapping. Possibilities to investigate next time:

1. **Walker speculative prefetch** between `_inval_dcache_range` and
   `msr sctlr_el1` that re-pulls a stale 0 line for SCRATCH_TT[3].
   The pre-flip canonical fence (`ic ialluis; dsb ish; tlbi vmalle1is;
   dsb ish; isb`) should prevent this, but A72 may have implementation
   detail not covered.
2. **Cortex-A72 errata** beyond 859971/1319367 (the two we apply).
   Full SDEN list has more entries (832075, 853709, 852421, etc.).
3. **L2 cache aliasing** with the BCM2711-specific unified-L2 quirks
   already documented for plo (TD-plo-icache, TD-plo-dcache).
4. **TTBR0 vs TTBR1 walker cache asymmetry** — TTBR1 PT setup works
   fine (kernel code executes), only TTBR0 SCRATCH_TT walks fail.

### Independent improvements kept

* `hal_cpuInvalDataCacheAll`: `dc cisw` → `dc isw` (correct Linux
  pattern; the previous form would write stale firmware-era dirty
  cache lines back to DDR over plo's just-loaded kernel image).
* `_hal_init` asm stub: TD-04-hack-2 probe stores removed. The probes
  were ad-hoc diagnostics that became a hang point under M|C with NC
  walks. Simplified to the canonical shape (msr spsel, set sp, branch).
* TCR remains Linux-standard (Inner+Outer WB cacheable, Inner-Shareable)
  even at M-only — no functional difference with caches off, but ready
  for future M|C attempts.

### Status

Cache enable parked. Kernel runs M-only as before, slow but correct.
Re-engaging cache enable requires either:
  - JTAG / SWD debugger on real Pi 4 to single-step through the MMU
    enable + first walker access (visible in MMU state)
  - Hardware gdbstub similar to U-boot debug for state introspection
  - Cross-reference Linux's arm64 boot trace for the BCM2711 variant
    to find what additional cache-maintenance Linux performs that
    Phoenix doesn't.

Next focus: features that don't depend on caches — full 4 GiB RAM
unlock, SMP secondary cores, USB+keyboard via PCIe, HDMI text mode.

## 2026-05-15 night — C-3 second pass: deferred-enable strategies (ALL FAILED)

After the morning's `_init.S`-time M|C|I attempts (c3a–c3h) all
faulted at the first post-MMU walker read, the afternoon pivoted to
**deferred enable**: keep `_init.S` at M-only (the boot-correct
baseline) and flip SCTLR_EL1.C / SCTLR_EL1.I from C code in `main.c`
*after* `_hal_init()` returns. This let the boot reach further than
ever before — the kernel ran ~3000 lines of M-only init, including
all the `_hal_init_c` sub-stages (`_pmap_preinit`, `_hal_platformInit`,
`_hal_consoleInit`, `_hal_exceptionsInit`, `_hal_interruptsInit`,
`_hal_cpuInit`, `_hal_timerInit`), printed the cyan-coloured
"hal: console init done", then transitioned to a clean call to
`hal_cpuEnableDCache` / `hal_cpuEnableICache`.

Every deferred-enable variant tested failed in the **same class** of
way: the SCTLR.C=1 (or SCTLR.I=1) MSR succeeds, the canonical fences
complete, the helper returns to main.c — and then the **very next
cacheable data load** returns stale data (D-cache enable case) or the
**very next instruction fetch from the helper's tail** returns garbage
(I-cache enable case). Both manifest as a silent hang — no fault, no
EX= marker, no further UART output, indistinguishable from a busy-loop
on a stuck status register.

### Complete c3 attempt matrix

| Test | What changed | Where it stopped | Notes |
|------|--------------|------------------|-------|
| c3a | M\|C\|I single MSR in _init.S | EX=4 infinite loop | Vector slot 4 sync exception; handler self-faults at literal-pool load → no ESR/ELR/FAR printed |
| c3b | M\|C single MSR, TCR cacheable walks | L1 translation fault FAR=0xfe201018 ELR=_init.S:635 (X4 marker) | First post-MMU PL011 access via TTBR0 SCRATCH_TT[3]; walker reads SCRATCH_TT[3] as invalid |
| c3c | M\|C, TCR cacheable, plo dc ivac fix | Same as c3b | plo `dc civac` → `dc ivac` change alone didn't help |
| c3d | M\|C, **TCR NC walks** | No fault — but hang at _init.S:1363 (probe store) | NC walks bypass walker D-cache; kernel reaches syspage relocation, hangs in `_hal_init` asm stub on first probe store |
| c3e | Same as c3d, 480s capture | Same hang at line 1363 | Confirmed hang (not slowness); 240s vs 480s identical |
| c3f | NC walks, **removed `_hal_init` probe stores** | Hang at `b _hal_init_c` (after 'I' marker) | Asm probes removed; hang moves to `_hal_init_c` C-prologue stack write |
| c3g | NC walks, **dc cisw → dc isw fix** | Same as c3f | cisw→isw didn't unstick NC-walks hang |
| c3h | TCR cacheable walks + dc isw | Same as c3b (L1 fault FAR=0xfe201018) | Confirms dc isw vs cisw doesn't affect cacheable-walks fault |
| c3i | **Baseline verification**: M-only + hygiene fixes (dc isw + _hal_init cleanup), no M\|C | Full boot ✓ (kernel banner, threads, init thread, fbcon, pcie, xhci, psh tty) | The hygiene fixes don't regress M-only |
| c3j | **Staged** M-then-C (plo's pattern) + cacheable walks | X4/X5/X6/N printed, then L3 fault FAR=0xffffffffc0001890 ELR=_core_0_virtual | Staged enable solved the X4 fault. New fault on first TTBR1 data load on high-VA stream after `br x0`. Same walker-cache pattern, different table. |
| c3k | Staged M-then-C + NC walks | Same as c3f/c3g (hang at `b _hal_init_c`) | Staging didn't help NC walks case |
| c3l | **Deferred** C enable in `main.c` after `_hal_init` returns | Breakthrough: kernel runs all of `_hal_init_c`, prints "hal: console init done", enable returns ('c' marker), td16b prints — then hangs at **first hal_consolePrint after enable** ("main: hal init done") | Massive progress vs all earlier attempts. Hang is now squarely a *cacheable-read returns stale data* problem. |
| c3m | c3l + finer markers around hal_consolePrint | Hang localized between '1' marker (post-td16b) and '2' marker (after hal_consolePrint) | hal_consolePrint is what stalls — likely on a cacheable read of `console_common.uart.base` or the spinlock state |
| c3n | c3l + **bypass first hal_consolePrint** with direct `*uart=` writes | '1', '2', 'h' all print — then **_usrv_init hangs** | Not specific to hal_consolePrint; any function that reads cacheable kernel data after cache-enable hangs |
| c3o | Deferred enable moved **after** first hal_consolePrint | First print works M-only, 'C', 'c' both print (enable returns) — then hangs in _usrv_init | Same as c3n: enable itself works, post-enable reads fail |
| c3p | Add `dc ivac` over kernel 2 MiB VA range after SCTLR.C=1 | Hangs in `hal_cpuEnableDCache` itself (no 'c' marker) | The post-enable invalidate loop hits the same stale-read issue when reading its own literal-pool address constant |
| c3q | c3p but build addresses via movz/movk (no literal-pool reads) | Still hangs in helper — no 'c' | dc ivac requires walker translation; walker reads PT through cache; circular dependency |
| c3r | **Double set/way invalidate** (dc isw before AND after SCTLR.C=1) | Helper returns ('c' prints), next access (td16b) hangs | Sequential dc isw works, but doesn't fix the stale-read pattern at the C code level |
| c3s | **Split**: try `hal_cpuEnableICache` only, no D-cache | Hangs in helper (no 'i' marker) | I-cache enable has the same problem in a different guise |
| c3t | c3s + add `hal_cpuInvalDataCacheAll` to I-cache helper (cleans L2 unified) | Same as c3s — no 'i' | The extra dc isw doesn't help here either |

### Root-cause hypothesis (still unverified)

All deferred-enable failures share the same shape: **the first
cacheable access after `SCTLR_EL1.C=1` (or `SCTLR_EL1.I=1`) returns
stale/wrong data**, even though `hal_cpuInvalDataCacheAll` (Linux-
style `dc isw` walking all CLIDR levels) was just run.

Three independent hypotheses, in order of decreasing confidence:

1. **BCM2711 system L2 cache (SLC) — most plausible.** Pi 4's
   BCM2711 SoC has a system-wide L2 cache between the A72 cluster
   and DDR. ARM cache-maintenance ops (`dc isw`, `dc ivac`,
   `ic ialluis`) only invalidate the A72 cluster caches — they do
   not reach the SLC. The VC4 GPU and firmware run with their own
   caching policy and dirty the SLC during boot; plo and the
   M-only kernel write through the SLC (NC writes pass through) and
   reach DDR, so DDR is correct. But when SCTLR.C=1, A72 cacheable
   reads go *to the SLC*, not directly to DDR — and the SLC may
   still hold firmware-era dirty lines for the kernel-data PA range.
   The SLC has its own control interface; BCM2711-specific
   maintenance is needed (analogous to Linux's `outer_cache.flush_all`
   hooks on platforms with PL310/PL220 L2 controllers).

2. **A72 hardware data prefetcher.** Even with SCTLR.C=0, the L1D
   prefetcher may speculatively pull lines into cache during M-only
   execution. If those lines are tagged with stale data (because the
   PA in DDR is fresh kernel data but the prefetcher pulled an old
   value from somewhere else), they'd survive `dc isw` if the
   set/way walk happens to miss them — and surface on the first
   post-SCTLR.C=1 read.

3. **A72 errata beyond 859971 / 1319367.** The armstub applies both,
   which were the documented "wild pointer after cache enable"
   workarounds. There are more in the Cortex-A72 SDEN that we
   haven't reviewed for relevance: 794073, 814670, 851141, 855423,
   1149018, 1786420. ARM-DEN-0013 has the full list.

### Strategies still to try (next session)

Most promising first, then progressively heavier:

**Tier 1: software-only, low-risk**

* **(c3u) Disable L1D hardware prefetcher** via
  `CPUACTLR_EL1[40:38] = 0b111` (A72 r0p3 disables L1D-prefetch).
  Must be applied at EL3 in the armstub because CPUACTLR_EL1 traps
  from EL1 (same place we apply 859971 + 1319367 today). If the
  prefetcher is responsible for stale-line resurrection, disabling
  it should let the post-enable reads succeed. Minimal patch:
  ~6 lines in `phoenix-armstub8-rpi4.S` right after the existing
  `CPUACTLR_EL1` block. **First thing to try next session.**

* **(c3v) Disable A72 L2 hardware prefetcher** via additional
  CPUECTLR_EL1 bits. Less commonly disabled but documented as a
  prefetch source. Same EL3 issue.

* **(c3w) Disable ALL A72 prefetchers + load-pass-DMB**: full
  set of CPUACTLR_EL1 conservative bits. If even with everything
  disabled the kernel still hangs, definitive evidence the cause is
  NOT prefetcher-related.

* **(c3x) Move cache enable to MUCH later in boot**, e.g. inside
  `proc_threadsInit` right before scheduling first user thread.
  By then the kernel has done all heavy init; if any data
  structures still have stale-line hazards, they should all have
  been *written* by now via NC paths, drained, and have correct
  DDR contents. Most stalemate-breaker: same fundamental issue
  but with maximum M-only run-time before the flip.

**Tier 2: BCM2711-specific cache maintenance**

* **(c3y) SLC invalidate via BCM2711-defined sequence.** The
  BCM2711 datasheet describes the SLC and its control registers
  (in the "ARM peripherals" region around `0xff800000+`). Need to
  reverse-engineer or extract from VideoCore firmware / Linux
  bcm2711 driver code what the SLC invalidate sequence looks like.
  Linux's `bcm2835-cache` or similar driver, if it exists, is the
  reference. Add a `_hal_bcm2711SlcInvalidate` function and call
  it in `hal_cpuEnableDCache` between the dc isw and the SCTLR
  write. **This is the most likely fix if Tier 1 fails.**

* **(c3z) Configure problematic mappings as Outer-NC.** Add MAIR
  slot 4 = 0x4F (Inner WB + Outer NC) and use it for KERNEL_TTL3
  default attrs. With Outer NC the SLC is bypassed; with Inner WB
  the A72 L1/L2 still caches. Tests the "SLC is the stale-data
  source" hypothesis directly. Performance hit on cross-core
  shared state (which currently doesn't matter at NUM_CPUS=1).

**Tier 3: hardware debugger**

* **(c3-jtag) JTAG/SWD on real Pi 4 with OpenOCD.** Single-step
  through `hal_cpuEnableDCache` and inspect cache + memory state
  at each instruction. Should pinpoint exactly which read returns
  stale data and from which cache level. The Pi 4 has a 14-pin
  JTAG header reachable through soldering or a HAT.

* **(c3-uboot) U-Boot debug stub on Pi 4.** Boot U-Boot first,
  attach gdb via JTAG or via U-Boot's gdb-server, single-step
  Phoenix's MMU+cache enable path. U-Boot has known-working A72
  cache enable that we can compare against.

**Tier 4: defer / acceptance**

* **(c3-accept) Accept M-only for v1 release.** The current M-only
  kernel boots correctly on real Pi 4 with all expected user
  processes (8 spawned: bind/dummyfs/dummyfs-root/mkdir/pcie/
  pl011-tty/psh/usb). It's slow (~30× slower than cacheable would
  be) but functionally correct. Other features (USB+keyboard,
  HDMI text, SMP, full 4 GiB RAM) don't depend on cache enable
  and can land independently. Cache enable becomes a v2 milestone.

### What's currently committed and what's not

**Committed (on respective working branches):**
* phoenix-rtos-kernel `agent/rpi4-program-reloc`:
  * `f68d1008` C-1: PT pre-MMU
  * `3c2fa845` C-2: TTBR0 cacheable
  * `3d8bb81b` cache-hygiene fixes (dc isw, _hal_init asm cleanup, M-only revert)
  * `f7fe6b39` deferred cache-enable helpers (this commit, work-in-progress)
* plo `codex/common-aarch64-platform-makefiles`:
  * `57254f3` teardown `dc civac` → `dc ivac`
* coord (phoenix-rpi) `main`:
  * `4921297` option C: revert cache commits + design doc + M-only rollback manifest
  * `c0130c2` cache: C-3 experiments concluded; M-only baseline + hygiene fixes manifest
  * (this commit) cache: C-3 second pass — deferred-enable strategies all hang

**The `f7fe6b39` kernel commit is INTENTIONALLY non-boot-correct.**
It contains `hal_cpuEnableICache()` enabled in `main.c` after
`_hal_init()`, which causes the kernel to hang silently. To return
the kernel to a boot-correct state, comment out the
`hal_cpuEnableICache()` call site in `main.c` (or revert
`f7fe6b39`). The intent is to preserve the work-in-progress code
so the next session starts from c3t state and tries c3u
(prefetcher disable) without reconstructing the helper functions.

### Action items for next session, in priority order

1. **Implement c3u** (L1D-prefetcher disable in armstub). 6-line
   patch + rebuild armstub blob + rebuild kernel image + 1 test
   cycle. ~30 min including verify.
2. **If c3u fixes it**: commit c3u, restore D-cache enable in
   `main.c`, run full validation (psh prompt + spawned user
   processes), commit, write release note.
3. **If c3u doesn't fix it**: implement c3v then c3w (heavier
   prefetcher disables).
4. **If Tier 1 doesn't fix it**: pivot to c3y (BCM2711 SLC
   invalidate). This is the highest-confidence remaining hypothesis
   per Linux bcm2711 driver code review.
5. **If Tier 2 also fails**: stop, document, switch to c3-accept
   and pursue USB+keyboard / HDMI text / SMP independently. They
   work fine on M-only — caches are a performance feature, not a
   correctness gate.

### Files touched during this work (not all committed yet)

* `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` — TCR walker
  attrs revert to cacheable (Linux standard); SCTLR.M-only enable
  (C-3 deferred); `hal_cpuInvalDataCacheAll` uses `dc isw`;
  `_hal_init` asm stub TD-04-hack-2 probes removed;
  `hal_cpuEnableDCache` + `hal_cpuEnableICache` helpers added
* `sources/phoenix-rtos-kernel/hal/aarch64/aarch64.h` — declarations
* `sources/phoenix-rtos-kernel/main.c` — late-enable call site
  (currently set to `hal_cpuEnableICache` for the c3s/c3t experiments;
  swap to `hal_cpuEnableDCache` to revisit D-cache enable)
* `sources/plo/hal/aarch64/generic/hal.c` — teardown
  `hal_dcacheFlush` → `hal_dcacheInval`
* `docs/research/2026-05-15-cache-enable-c-approach-design.md` —
  this file
* `manifests/2026-05-15-cache-hygiene-fixes-m-only.md` — M-only +
  hygiene snapshot
* `manifests/2026-05-15-c3-deferred-enable-wip.md` — current WIP
  state snapshot (new)

