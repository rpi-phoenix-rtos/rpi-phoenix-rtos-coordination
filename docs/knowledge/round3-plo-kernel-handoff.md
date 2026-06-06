# Round 3 — plo→kernel handoff path on Pi 4 (cache enable investigation)

## Scope and failure under investigation

The kernel boots cleanly with caches OFF. Enabling SCTLR_EL1.M+I (MMU + I-cache;
D-cache still off) reaches the X4/X5 markers, the TD-15 mailbox-buffer probe,
and the TD-16 alias cleanup, then takes a synchronous exception while
`syspage_init()` executes line 476 of
`/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-kernel/syspage.c`:

```
syspage_common.syspage->progs = hal_syspageRelocate(syspage_common.syspage->progs);
```

This is the very first read of the `progs` chain off the *copied* syspage. The
goal of this round is to walk the entire plo→kernel handoff path with current
file/line citations and decide whether the user's hypothesis ("plo writes
syspage with D-cache on; jumps without flushing; kernel reads stale DDR")
matches Phoenix's actual code, and to spell out the cache-maintenance contract
that the handoff currently implements vs. what is needed before caches can be
enabled in the kernel.

## 1. What state plo actually runs in (rpi4 generic target)

### 1.1 Entry SCTLR values written before plo enters C

`/Users/witoldbolt/phoenix-rpi/sources/plo/hal/aarch64/generic/_init.S:106-107`
sets `sctlr_el3 = 0x30c50838`. Decoding bit-by-bit: bits 3 (SA), 4 (SA0),
5 (CP15BEN), 11 (RES1), 18, 22, 23, 28, 29 are set; bits 2 (C) and 12 (I)
are zero. The matching `sctlr_el2` write at line 119 is `0x30c00838` — same
shape, also C=0, I=0. The `start_el2` path (line 145) and `start_el1` path
(line 164) write the same values. Plo therefore enters its main code with
**both D-cache and I-cache disabled**, irrespective of the EL it landed at.

### 1.2 Plo never re-enables caches on rpi4 generic

`grep -rn "hal_dcacheEnable\|hal_icacheEnable\|mmu_enable" plo/hal/aarch64`
returns hits only inside `cache.c`/`mmu.c` (the implementations) and inside
the **zynqmp** target (`plo/hal/aarch64/zynqmp/hal.c:94, 257-261`). The
`generic` (rpi4) target does *not* call `mmu_enable()`, `hal_dcacheEnable()`,
or `hal_icacheEnable()`. The matching plo source comment at
`plo/hal/aarch64/generic/_init.S:183-193` (the TD-05 comment) explicitly
states "plo runs cache-off (SCTLR.{M,C,I}=0)". Plo's stores therefore go
directly to DDR with no D-cache between them and DRAM, and plo's loads
likewise come straight from DDR.

This **partially refutes the user's primary hypothesis as stated**. The
hypothesis assumed plo writes syspage with caches enabled and forgets the
final clean. In reality plo writes the syspage with caches *disabled*, so
no dirty cache lines exist on plo's side at the moment of the eret.

The hypothesis is, however, *partially* still in play in a more subtle form
— see §3 below (firmware/VPU-owned dirty lines that survive plo's lifetime).

### 1.3 Pre-handoff cache maintenance plo *does* do

`plo/hal/aarch64/generic/_init.S:194-225` runs a set/way **invalidate-only**
(`dc isw`) walk of every cache level up to LoC, before any plo store. The
TD-05 comment explains why invalidate-only and why set/way: civac would
write firmware's stale dirty lines back to DDR over plo's later uncached
stores; VA-form `dc ivac` is promoted to civac on writable pages by A72.
Set/way invalidate-only at the very start ensures that whatever the Pi 4
firmware (start4.elf, bootcode, armstub running with caches on) left dirty
in L1/L2 cannot speculatively hit DDR after plo writes.

`plo/hal/aarch64/generic/hal.c:386` performs `hal_dcacheFlush(__heap_base,
__heap_limit)` immediately before `hal_exitToEL1()`. `hal_dcacheFlush` is
defined at `plo/hal/aarch64/cache.c:100-113` and issues `dc civac` per line
(clean+invalidate to PoC). With caches off this is a near-no-op for plo's
own writes — they never entered any cache — but it does kill any
speculatively pre-fetched lines the A72 may have populated for the heap PA
range during plo's own execution (per ARM ARM DDI 0487 B2.10, speculative
fetches into shareable Normal regions are permitted even with SCTLR.C=0).

So plo *does* in fact do the seL4-style "flush boot info before jump" step
at `plo/hal/aarch64/generic/hal.c:386`. The range covers `__heap_base` to
`__heap_limit` — the syspage and everything `syspage_alloc()` produced
(`plo/syspage.c:37, 52-68` shows the syspage and all linked structures live
in the heap).

### 1.4 The actual handoff path

After the `hal_dcacheFlush`, control flows
`plo/hal/aarch64/generic/hal.c:395 hal_exitToEL1()` →
`plo/hal/aarch64/generic/_init.S:262-302` `hal_exitToEL1`. The exit pulls
`hal_common.hs` and `hal_common.entry` out of memory, sets up SPSR/ELR for
the chosen EL, and `eret`s. There is **no further cache maintenance**
between line 386 and the `eret` at line 288/296/301. The `dsb sy` at line
265 ensures previous stores (and the civac chain) have completed.

## 2. What the kernel reads, and what kills the boot at line 476

### 2.1 Kernel's first cache-relevant action

`phoenix-rtos-kernel/hal/aarch64/_init.S:707-710` runs
`_clean_inval_dcache_range(x9, x9 + size)` over the *source* syspage in plo's
heap before the kernel copies it. This matches the comment at lines 701-706:
"clean+invalidate plo-owned syspage cache range … so DDR is authoritative".
With caches still off in the kernel at this point, the operation behaves like
plo's own pre-jump flush: it kills any speculative fills that the A72 has
produced for the syspage source PA range.

### 2.2 The copy itself

Lines 738-754 perform an 8-byte-stride copy from x9 (plo PA) to x1
(VADDR_SYSPAGE high-VA alias). The TD-04 fix at lines 537-558 had earlier
re-mapped the single TTL3 page covering `_hal_syspageCopied` as **Normal
Non-Cacheable** (NC_ATTRS = 0x707, AttrIdx=1, MAIR slot 1 = `MAIR_NOR_NC` =
0x44). The kernel's stores into the copy destination therefore bypass the
A72 D-cache entirely and go directly to DDR.

A second `_clean_inval_dcache_range` over the LOW-PA destination range at
lines 770-774 then kills any cacheable speculative lines the A72 may have
populated for the destination via the cacheable LOW-PA identity alias.

### 2.3 What `hal_syspageRelocate` actually does

`phoenix-rtos-kernel/hal/aarch64/hal.c:38-41`:

```
void *hal_syspageRelocate(void *data)
{
    return ((u8 *)data + relOffs);
}
```

Pure pointer-add. The crash at `syspage.c:476` is therefore *not* in the
add itself; it must be in the load that fetches `syspage_common.syspage->progs`
into the argument register, or in the store that writes the result back. Both
are reads/writes through the high-VA alias of `_hal_syspageCopied`. That
single page is mapped Normal Non-Cacheable (TD-04 fix); reads from it never
touch the D-cache at all.

### 2.4 Why line 476 in particular?

Lines 297-468 already walk the entire `maps` chain through identical
`hal_syspageRelocate(...)` operations against the same NC TTL3 page
(`maps`, `map->next`, `map->prev`, `map->name`, `map->entries`, `entry->next`,
`entry->prev` — see `syspage.c:297, 304, 307, 310, 317, 431, 434`). All of
these execute successfully with M+I enabled in the failing config. Line 476
is the *first* read of the `progs` chain. If the bug were "stale D-cache
lines on syspage data" the maps chain would fault first.

This rules out the user's hypothesis as the cause of *this specific*
failure: the syspage destination is mapped non-cacheable, the D-cache cannot
hold anything for it, and dozens of nearby reads through the same TTL3 page
work fine. Whatever fails at `progs` first read is something else.

### 2.5 Plausible alternative causes for the line-476 fault

1. **Stale TLB entry for the `progs` field.** With M+I enabled but D-cache
   off, the TLB is populated; any prior `tlbi` did or did not cover this VA.
   `phoenix-rtos-kernel/hal/aarch64/_init.S:586-588` issues `tlbi vmalle1is`
   before the high-VA branch. If the I-cache enable is added *after* this
   TLBI, no further TLB invalidation occurs, but the page-table edits
   *before* the TLBI already cover the syspage NC mapping — TLB should be
   clean. Unlikely but worth a `tlbi vae1` over the destination page when
   adding I-cache enable.

2. **I-cache stale instruction fetch.** If the kernel image bytes for
   `syspage_init`'s `progs`-handling code were ever fetched while caches
   were *off* and then SCTLR_EL1.I is flipped to 1 without an
   `ic ialluis` + `dsb ish` + `isb`, the I-cache will fill from DDR on the
   next fetch. That fetch goes through the HIGH-VA TTL3 (cacheable
   DEFAULT_ATTRS, set at lines 530-535). The fault could be a level-3
   translation fault on the I-side if the high-VA TTL3 hadn't been written
   yet *for that PC slice*. The kernel image is mapped 2 MB cacheable; the
   first ~2 MB starting at `pkernel` is covered (line 530-535 calls
   `_fill_page_descr` over PMAP_COMMON_KERNEL_TTL3). Any kernel code beyond
   the first 2 MB would not be mapped, and the I-cache would speculatively
   prefetch and fault. Worth confirming the kernel image fits inside 2 MB
   on the failing build (`syspage.c:476` is past 30 KiB into syspage.o, but
   total kernel `.text` size is the relevant figure).

3. **Speculative I-cache fill before MMU+I enable.** ARM ARM DDI 0487 B2.5
   permits A72 to speculatively fetch instructions from any cacheable
   region the predictor reaches *even with SCTLR_EL1.I=0* (the I-cache
   bit only governs hits/misses; it doesn't stop the line-fill engine
   from populating a holding buffer). The bytes plo wrote into the kernel
   image while caches were off may not yet be observable to a speculative
   I-side load until an `ic ialluis` runs. The kernel does not invalidate
   the I-cache before flipping SCTLR.I.

## 3. Where the user's hypothesis still has bite

Even though plo runs cache-off, the *firmware* that ran before plo did not.
BCM2711's start4.elf and the ARM-side armstub
(`phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/phoenix-armstub8-rpi4.S`)
both run with caches enabled. The armstub at line 202-203 writes
`sctlr_el2 = 0x30c50830` — same shape as plo's value, C=0, I=0. So the
armstub itself is cache-off. But the GPU firmware (start4.elf, bootcode.bin)
runs on the VideoCore and, while not cache-coherent with the ARM in the
classical sense, can still leave dirty lines in the ARM L2 (which is
unified) for any DRAM region the firmware-resident ARM stub touched
(stack, mailbox buffers, framebuffer backing store, …). Plo's TD-05
set/way invalidate-only at `plo/hal/aarch64/generic/_init.S:194-225`
addresses exactly that case.

A second class of post-plo writers remains live during the handoff window:
the VPU itself, GPIO/USB DMA, and any peripheral that performs DMA into
ARM-usable DRAM. The TD-15 phase-1 probe at
`plo/hal/aarch64/generic/hal.c:344-355` writes a known pattern at
`PLO_RPI_MAILBOX_BUFFER_ADDRESS` and the kernel reads it back through the
NC alias mapped at `phoenix-rtos-kernel/hal/aarch64/_init.S:517-528` —
this is exactly the test for whether external writers are corrupting DDR
during the handoff. It does not cover the syspage range itself, but the
TD-04 NC-destination fix already walks-around any such corruption for
syspage bytes.

## 4. armstub state at eret

`phoenix-armstub8-rpi4.S:171-210` writes `sctlr_el2 = 0x30c50830` (caches
off), sets SCR_EL3 to NS|HCE|RW|RES1, and does an `eret` to `in_el2` at
line 211. Plo enters at EL2 with caches off — confirmed at
`plo/hal/aarch64/generic/_init.S:140-161`. The armstub does not perform
its own cache maintenance over the kernel image / DTB / mailbox-buffer
range; it only hands off PC at `br x4` (line 261) after a `dsb sy; isb`.

## 5. seL4 elfloader comparison

seL4's elfloader does, immediately before `eret` to the kernel:

- `flush_dcache_range` over the entire kernel ELF region in DDR
- `flush_dcache_range` over the `bootinfo` blob
- `invalidate_icache` (architecturally `ic ialluis`)
- `dsb ish; isb`

Phoenix's plo-side equivalents:

- `hal_dcacheFlush(__heap_base, __heap_limit)` at
  `plo/hal/aarch64/generic/hal.c:386` covers the syspage and everything
  allocated through it. **Equivalent of bootinfo flush — present.**
- The kernel image is loaded from phfs into DDR earlier in plo. There is
  **no explicit civac over `[ADDR_KERNEL, ADDR_KERNEL + kernelSize)`**
  before the eret. Because plo runs cache-off this is "fine" today (no
  dirty lines), but speculative fills can still leave stale lines that
  shadow DDR after the kernel enables I-cache. **This is the gap most
  likely to break I-cache enable.**
- Plo does **not** issue `ic ialluis` before the eret. The kernel does
  not issue it before flipping SCTLR_EL1.I either. **Both gaps relevant
  to the user-reported fault at line 476.**

## 6. Concrete fix proposals

In priority order, smallest first:

### Fix 1 (kernel side, immediately before SCTLR_EL1.I flip)

Add at `phoenix-rtos-kernel/hal/aarch64/_init.S:444-450` (the X3
`uart_tag2 88, 51` block where M-only is currently turned on):

1. Compute the kernel image VA range (or low-PA range — both still mapped).
   Use the linker symbols `_start..__etext` or `_start.._end`.
2. Walk it with `dc cvau` (clean to PoU) + `ic ivau` (invalidate I-cache to
   PoU) per line; or simpler, do `ic ialluis; dsb ish; isb` over the whole
   I-cache.
3. Then `orr x0, x0, #(1 << 12)` (SCTLR_EL1.I) and `msr sctlr_el1, x0; isb`.

The simpler global `ic ialluis` form is what ATF uses post-image-load; the
A72 has the I-cache invalidated at reset anyway, so the only thing this
guards against is post-reset speculative fills.

### Fix 2 (plo side, mirror seL4)

Add to `plo/hal/aarch64/generic/hal.c` just before `hal_exitToEL1()` at
line 395:

```
hal_dcacheFlush((addr_t)kernel_load_pa, (addr_t)kernel_load_pa + kernel_size);
hal_icacheInval();          /* exists at plo/hal/aarch64/cache.c:50-57 */
```

`kernel_load_pa` and size are already known to plo (the phfs loader knows
where it placed the kernel; see `plo/hal/aarch64/generic/hal.c:131-141
hal_kernelGetAddress`). The flush range need only cover the kernel `.text`
+ `.rodata` + `.data` payload as actually written.

### Fix 3 (kernel side, defensive TLBI)

After the SCTLR_EL1.I flip in Fix 1, add `tlbi vmalle1is; dsb ish; isb`.
This is belt-and-braces — the page tables haven't changed, so it should be
a no-op, but it guarantees the post-cache-enable instruction stream walks
fresh table entries.

### Fix 4 (rule out the kernel-image-too-big case)

Confirm `__etext - _start <= 2 MB`. If not, either grow PMAP_COMMON_KERNEL_TTL3
to cover N pages × N entries, or reduce the kernel image. Easy check via
`size phoenix-rpi-kernel.elf`.

## 7. Recommended order of attack

1. **Fix 4 first** (free, 30 s): confirm kernel image size. If kernel is >2 MB
   the I-cache enable will speculatively fault on any high-VA fetch beyond
   the first PMAP_COMMON_KERNEL_TTL3 page coverage.
2. **Fix 1** (kernel-side `ic ialluis` before SCTLR_EL1.I flip) is the
   minimal change that addresses the most likely cause of the line-476
   fault. Implement and re-run with M+I enabled.
3. If Fix 1 still faults at 476, **Fix 2** (plo-side civac of the kernel
   image range) closes the remaining handoff gap.
4. Only after both pass should D-cache enable be attempted; that path needs
   its own analysis (the TD-04 NC mapping for the syspage destination is
   only safe with D-cache off because the LOW-PA alias is cacheable; with
   D-cache on, the LOW-PA cacheable mapping must either be torn down before
   the source read, or the source read must use the same NC alias plo built
   it through — which Phoenix does not currently expose).

## 8. Caveats

The above assumes the failing config is exactly "SCTLR_EL1.M=1, .I=1, .C=0"
with the rest of the present-day TD-04/TD-15/TD-16 work in place. The
syspage destination NC mapping at `phoenix-rtos-kernel/hal/aarch64/_init.S:537-558`
makes the user's "stale D-cache over syspage" hypothesis structurally
impossible for the *destination* page; it does not, however, protect the
kernel `.text` against I-cache speculative fills, which is why the same
class of bug (cache-vs-DDR mismatch) can still bite at the I-side. The
fix proposals above target the I-side gap explicitly. The eventual D-cache
enable will need a parallel analysis for the kernel `.data` and `.bss`
images and for the syspage *source* PA in plo's heap.

## 9. Summary

- Plo on Pi 4 generic runs entirely cache-off (`SCTLR_{EL3,EL2,EL1}.{M,C,I}=0`,
  `plo/hal/aarch64/generic/_init.S:106-119`); no D-cache writes happen
  during syspage construction.
- Plo *does* civac its heap (syspage + allocator) before eret
  (`plo/hal/aarch64/generic/hal.c:386`), matching seL4's bootinfo flush.
- The kernel re-civac's the syspage source range, copies it through a
  freshly-installed Normal Non-Cacheable TTL3 entry, and re-civac's the
  destination LOW-PA alias
  (`phoenix-rtos-kernel/hal/aarch64/_init.S:537-558, 707-710, 738-754,
  770-774`).
- Therefore the line-476 fault under M+I enable is **not** explained by
  stale D-cache over the syspage. The remaining gaps are:
  1. No `ic ialluis` (or per-line `ic ivau`) covering the kernel image
     bytes plo wrote, on either plo's side at eret or the kernel's side
     before SCTLR_EL1.I goes 1.
  2. No civac of the kernel image in plo before eret (kernel `.text` etc.
     can hold speculatively-prefetched stale lines from before plo wrote).
  3. (Lower probability but worth checking) the kernel `.text` may exceed
     the single 2 MB region currently mapped via PMAP_COMMON_KERNEL_TTL3.
- Recommended next step: add `ic ialluis; dsb ish; isb` in
  `phoenix-rtos-kernel/hal/aarch64/_init.S` immediately before the
  SCTLR_EL1.I bit is set, and verify with M+I enable. If the line-476
  fault persists, add a plo-side civac over the kernel image PA range
  before `hal_exitToEL1()`.
