# Round 3 — Phoenix-RTOS Port-Convention Audit (rpi4b deviation map)

**Scope.** Compare every Phoenix-RTOS architecture port (plo + kernel) on the
five plo→kernel handoff dimensions that matter for the BCM2711 cache-enable
problem: caches-on-at-handoff, regions flushed by plo before jump, kernel-side
cache invalidation at start, MMU+caches enable order, and SMP/coherency setup.
Then identify exactly where `aarch64a72-generic-rpi4b` deviates from the
established Phoenix idiom, and propose minimal alignment patches.

All claims are cited as `path:line`. Paths are relative to
`/Users/witoldbolt/phoenix-rpi/sources/`.

---

## 1. Per-architecture summary

### 1.1 ia32 (x86 32-bit) — `phoenix-rtos-project/_projects/ia32-generic-pc/`

- **plo entry.** `plo/hal/ia32/_init.S:1` — pure 32-bit boot, no cache/MMU
  manipulation in plo's _init.S. Cache & paging are driven by the BIOS/UEFI
  contract. plo runs with the platform-default cache state (typically caches on
  for normal RAM). No `mmu_enable()` exists.
- **plo→kernel jump.** `plo/hal/ia32/cpu.c:34-49` — `hal_cpuJump()` is bare-
  metal: `cli; mov 24(syspage),%esp; pushl syspage; jmpl *entry`. **No cache
  flush, no D-cache disable, no I-cache invalidate** before jumping. Fully
  cache-coherent x86 architecture removes the need.
- **Kernel side.** `phoenix-rtos-kernel/hal/ia32/_init.S:35-96` — sets up page
  directory, sets `cr3`, enables PSE then sets `CR0.PG`. Caches stay at their
  inherited state (no SCTLR-equivalent bit flip). Subsequent SMP cores re-
  enable cache via `cr0 &= 0x9FFFFFFF` then enable PG (`_init.S:166-169`).
- **Syspage handoff.** `%esi` carries syspage PA (`_init.S:46-47`).
- **MMU+caches.** Caches are *implicit* and never gated by Phoenix code on x86
  — paging is enabled in the kernel. No staging issue.
- **SMP.** AP cores brought up via INIT/SIPI (`_init.S:138-148`).

### 1.2 armv7a — i.MX 6ULL (Cortex-A7) and Zynq-7000 (Cortex-A9)

- **plo entry.** `plo/hal/armv7a/imx6ull/_init.S:1` and
  `plo/hal/armv7a/zynq7000/_init.S:1`. plo brings up DDR / clocks, does set/way
  cache invalidation, then enables MMU+I+D-cache via `hal_memoryInit()`.
- **plo `hal_memoryInit()` (cache + MMU on).**
  `plo/hal/armv7a/imx6ull/hal.c:55-86` and `plo/hal/armv7a/zynq7000/hal.c:57-93`:
  call `mmu_init()`, populate cached/uncached section maps, then `mmu_enable()`
  (which flips SCTLR.{M,C,I} together for armv7a). plo runs **with caches and
  MMU on** during command processing and image load.
- **plo `hal_cpuJump()` — pre-jump sequence.**
  `plo/hal/armv7a/imx6ull/hal.c:120-146` and
  `plo/hal/armv7a/zynq7000/hal.c:242-272`:
  ```
  hal_interruptsDisableAll();
  hal_dcacheEnable(0);             /* turn D-cache OFF */
  hal_dcacheFlush(OCRAM_LOW..);    /* civac the entire OCRAM_LOW window  */
  hal_dcacheFlush(OCRAM_HIGH..);   /* civac the entire OCRAM_HIGH window */
  hal_dcacheFlush(DDR..);          /* civac the entire DDR window        */
  hal_icacheEnable(0);             /* turn I-cache OFF */
  hal_icacheInval();               /* iciallu */
  mmu_disable();                   /* SCTLR.M = 0 */
  ... blx entry ...                /* jump to kernel with caches+MMU off */
  ```
  This pattern hands the kernel a fully clean DRAM image: every dirty cache
  line plo wrote has been written back (`dc civac` is clean+invalidate), then
  caches are physically off, then MMU is off. The kernel sees raw DDR with
  zero ambiguity about coherency.
- **Kernel _init.S.** `phoenix-rtos-kernel/hal/armv7a/zynq7000/_init.S:94-152`
  and `phoenix-rtos-kernel/hal/armv7a/imx6ull/_init.S:355-548`: kernel
  re-disables caches and MMU at entry, runs the canonical Cortex-A set/way
  D-cache invalidation loop (`mcr p15,0,r2,c7,c6,2 (DCISW)`), invalidates
  I-cache (`ICIALLU`) and TLB, **then** enables SMP bit in ACTLR
  (`zynq7000/_init.S:108`), populates TTL1/TTL2, and finally enables
  D-cache + I-cache + branch prediction + MMU together
  (`zynq7000/_init.S:326-345`, `imx6ull/_init.S:551-554` then `:717-722`).
  zynq7000 enables data+instruction caches+MMU in the same window;
  imx6ull defers the cache-enable to a later kernel stage but the structure
  is identical.
- **Syspage handoff.** plo passes syspage PA in `r9`
  (`zynq7000/_init.S:154-186` reads `r9` and copies syspage with caches off).
- **SMP.** `mrc p15,0,r1,c1,c0,1; orr r1,#((1<<6)|(1<<0)); mcr ...` — SMP +
  cache/TLB maintenance broadcast asserted before MMU enable
  (`zynq7000/_init.S:107-109`, `imx6ull/_init.S:460-463`).

### 1.3 aarch64a53 — ZynqMP (Cortex-A53) `aarch64a53-zynqmp-zcu104`

- **plo entry.** `plo/hal/aarch64/zynqmp/_init.S:30-219`: starts in EL3, runs
  set/way invalidate-all (`invalidate_dcache:` at `:189-216` using `dc isw`),
  invalidates I-cache and TLB (`:136-138`), enables PMU and SMP
  (`:152-162`, behind `__TARGET_AARCH64A53`), parks secondary cores and
  jumps to C `_startc`. Same structure as zynq7000 but at EL3.
- **plo `hal_memoryInit()`.** `plo/hal/aarch64/zynqmp/hal.c:64-95`: calls
  `mmu_init()`, maps OCRAM and DDR as `MMU_FLAG_CACHED`, BITSTREAM and the
  uncached DDR window as `MMU_FLAG_UNCACHED`, then `mmu_enable()`. The
  EL3 mmu_enable flips SCTLR_EL3.{M,C,I} together — `plo/hal/aarch64/mmu.c:73-82`:
  ```c
  val = sysreg_read(sctlr_el3);
  val |= ((1 << 12) | (1 << 2) | (1 << 0));   /* I, C, M */
  sysreg_write(sctlr_el3, val);
  ```
- **plo `hal_cpuJump()`.** `plo/hal/aarch64/zynqmp/hal.c:249-268`:
  ```
  hal_interruptsDisableAll();
  hal_dcacheEnable(0);              /* SCTLR.C = 0 */
  hal_dcacheFlush(OCRAM..);         /* civac entire OCRAM */
  hal_dcacheFlush(DDR..);           /* civac entire DDR  */
  hal_icacheEnable(0);              /* SCTLR.I = 0 */
  hal_icacheInval();                /* ic iallu */
  mmu_disable();
  hal_coreJumpFlag = 1;
  hal_exitToEL1();                  /* eret EL3->EL1 */
  ```
  Identical shape to armv7a/zynq7000. Caches+MMU off, every DDR/OCRAM page
  cleaned-and-invalidated, then `eret` into the kernel.
- **Kernel _init.S.** Shared `phoenix-rtos-kernel/hal/aarch64/_init.S` (also
  consumed by aarch64a72-rpi4b — same file). Without rpi4b TD-04 patches,
  the canonical ZynqMP path is:
  - syspage PA arrives in `x9` (per the contract on `_init.S:254`);
  - kernel sets up TTBR0 identity + TTBR1 high-half (`_init.S:347-403`);
  - enables SCTLR_EL1.M only (`_init.S:444-449`);
  - copies syspage into `_hal_syspageCopied` BSS slot
    (`_init.S:738-755`);
  - jumps to virtual entry.
- **MMU+caches.** ZynqMP currently boots with **MMU on, caches off** in the
  kernel. The `_init.S:430-458` comment explicitly says this is the
  known-good baseline; cache enable is parked under `TODO(TD-16-cache-enable)`.
  This is itself a recent rpi4b-driven regression that has spread to the
  shared file — see §3.
- **SMP.** plo parks secondary cores (`zynqmp/_init.S:167-179`), then
  `hal_coreJumpFlag` releases them post-MMU-init in the kernel.

### 1.4 aarch64a72 — generic / rpi4b (`aarch64a72-generic-rpi4b`)

- **plo entry.** `plo/hal/aarch64/generic/_init.S:42-251`: handles entry at
  any of EL3/EL2/EL1, programs SCTLR_ELx to a value with M=C=I=0 (e.g.
  `_init.S:106-107` — `sctlr_el3 = 0x30c50838`), runs a TD-05
  invalidate-only set/way loop (`_init.S:194-225` — `dc isw`, *not* `dc cisw`)
  to drop firmware's dirty lines, then jumps to C `_startc`.
- **plo `hal_init()` — NO `hal_memoryInit()`.**
  `plo/hal/aarch64/generic/hal.c:86-96` does **not** call `mmu_init()` or
  `mmu_enable()`. Compare with `plo/hal/aarch64/zynqmp/hal.c:98-109` which
  calls `hal_memoryInit()` ⇒ `mmu_init() + mmu_enable()`. **The rpi4b plo
  runs MMU-off and cache-off for its entire lifetime.** Confirmed by the
  comment block at `plo/hal/aarch64/generic/hal.c:267-272`:
  > "with plo running cache-off (SCTLR.C=0)…"
  and `:340-342`:
  > "Plo runs cache-off so a direct store hits DDR; we still issue a dsb sy
  > + isb to drain."
- **plo `hal_cpuJump()`.** `plo/hal/aarch64/generic/hal.c:364-399`:
  ```
  hal_interruptsDisableAll();
  hal_coreJumpFlag = 1;
  hal_dcacheFlush(__heap_base..__heap_limit);   /* civac heap only      */
  hal_probeSyspage();                            /* diagnostic only      */
  hal_td15ProbeWrite();                          /* diagnostic only      */
  hal_exitToEL1();
  ```
  No `hal_dcacheEnable(0)`/`hal_icacheEnable(0)`/`mmu_disable()` calls (they
  would be no-ops because plo never turned MMU+caches on, but the *symbolic
  contract* of the call is missing). The only DDR maintenance is over plo's
  heap range — the kernel image, the syspage destination buffer, and the
  rest of plo's `.data`/`.bss` are *not* explicitly civac'd.
- **Kernel _init.S.** Same shared
  `phoenix-rtos-kernel/hal/aarch64/_init.S` as ZynqMP, but heavily augmented
  with rpi4b-specific TD-04/TD-15/TD-16 work-arounds: NC TTL3 entry over
  `_hal_syspageCopied` (`:530-559`), NC override on early kernel stack
  (`:562-574`), `_clean_inval_dcache_range` over the source syspage range
  before the copy (`:706-710`), and over the LOW-PA dest range after the
  copy (`:770-774`).
- **MMU+caches.** Kernel enables MMU only (`_init.S:444-449`). Caches stay
  off — this is the active TD-16 problem.
- **SMP/coherency.** Pi 4 SMPEN (`CPUECTLR_EL1[6]`) and erratum 859971 work-
  around (`CPUACTLR_EL1[32]`) are applied **at EL3** in the armstub
  (`phoenix-armstub8-rpi4.S:181-198`) — the kernel cannot apply them itself
  because the impl-def CP15 traps from EL1 on A72 r0p3.

### 1.5 riscv64 — `riscv64-generic-qemu`, `riscv64-gr765-vcu118`

- **plo entry.** `plo/hal/riscv64/_init.S:24-47` — minimal: zero `sie`, save
  hart id and DTB pointer, set `gp` and `sp`, jump to `_startc`. No cache or
  MMU manipulation. plo runs S-mode without paging (Sv translation OFF).
- **plo→kernel jump.** Hart id in `a0`, syspage PA in `a1`, DTB in `a2`.
  Per the contract documented in `phoenix-rtos-kernel/hal/riscv64/_init.S:26-31`.
- **Kernel _init.S.**
  `phoenix-rtos-kernel/hal/riscv64/_init.S:33-100` — saves hart-data, copies
  syspage into `_hal_syspageCopied` (kernel-side BSS), then enables Sv
  translation. RISC-V cache coherency is part of the architecture (no
  inter-observer flushing needed at handoff).
- **MMU+caches.** Sv enabled in kernel; cache state is implicit / hardware-
  managed.
- **SMP.** Multi-hart supported via per-hart spin in `_init_core` (riscv64
  `_init.S:74` onwards).

### 1.6 sparcv8leon — `sparcv8leon-gr712rc-board`, `sparcv8leon-gr740-mini`

- **plo entry.** No `_init.S` in `plo/hal/sparcv8leon/` (relies on `cpu.c`
  init only); `phoenix-rtos-kernel/hal/sparcv8leon/` ships both an MMU
  variant and an SRMMU/no-MMU variant (`pmap.c` vs `pmap-nommu.c`).
- **plo→kernel.** Trap-window setup in `_traps.S`; no cache flush since
  Leon's L1 cache is write-through and is auto-invalidated on context flush.
- **MMU+caches.** SRMMU (Sun reference MMU) enabled in kernel pmap.c after
  syspage handoff. Cache control via Leon ASR registers, separate from MMU.

### 1.7 armv7r5f / armv8r — Cortex-R lockstep variants

- Cortex-R has no MMU (PMSA — protected memory). plo/armv7r/_cache.S and
  plo/armv8r/_cache.S provide range-based clean+invalidate. Per-region
  flushing in the platform `hal.c` (`armv8r/mps3an536/hal.c:181-185`) before
  jump is the standard pattern. Less directly comparable to A-class.

### 1.8 armv7m / armv8m — Cortex-M

- No MMU; MPU optional. plo runs caches enabled where present (M7) and
  flushes via `SCB_CleanInvalidateDCache_by_Addr` equivalents. Out of scope
  for BCM2711 cache-coherency analysis.

---

## 2. Comparison table

Columns: ARCH | plo-caches/MMU at handoff | regions plo civac's | kernel-side
cache inval at start | MMU+I+D enable order | SMPEN setup site

| arch                       | plo at handoff           | plo civac coverage                              | kernel inval at start            | MMU+I+D order                  | SMPEN site                          |
|----------------------------|---------------------------|--------------------------------------------------|-----------------------------------|--------------------------------|--------------------------------------|
| ia32 generic               | implicit (HW coherent)    | n/a                                              | n/a                               | PG only; caches inherited      | n/a                                  |
| armv7a imx6ull             | OFF (M=C=I=0)             | OCRAM_LOW + OCRAM_HIGH + DDR (full)              | DCISW set/way + ICIALLU + TLB    | M+C+I together (post-PT init)  | kernel ACTLR.SMP pre-MMU             |
| armv7a zynq7000            | OFF (M=C=I=0)             | OCRAM_LOW + OCRAM_HIGH + DDR (full)              | DCISW set/way + ICIALLU + TLB    | M+C+I together                 | kernel ACTLR.SMP+CTLB pre-MMU        |
| aarch64a53 zynqmp          | OFF (SCTLR_EL3.{M,C,I}=0) | OCRAM + DDR (full)                               | dc isw all-levels + ic iallu     | M+C+I together (canonical)     | plo `_init.S` `__TARGET_AARCH64A53`  |
| **aarch64a72 rpi4b**       | **OFF — but never on**    | **plo heap only** (`__heap_base..__heap_limit`)   | dc isw all-levels (TD-05) + ic iallu in plo entry; kernel side relies on per-VA `_clean_inval_dcache_range` over syspage src/dest only | **M only** (TD-16: caches parked) | armstub EL3 (kernel cannot)         |
| riscv64 generic            | implicit (Sv off, fence)  | none (architecture coherent)                     | none required                     | satp.MODE flip, single step    | per-hart in kernel                   |
| sparcv8leon gr712rc        | implicit (no L2)          | none (write-through L1)                          | tlb_flushAll, srmmu_init          | SRMMU enable; cache via ASR    | per-CPU                              |

The relevant comparison is the three A-class rows: imx6ull, zynq7000, zynqmp,
all share the **same plo→kernel cache-handoff shape**:

> plo turns I+D-cache OFF, civac's its three known DRAM windows, turns MMU
> OFF, then jumps to the kernel. Kernel re-asserts disabled state, runs an
> all-levels set/way invalidate-all, programs page tables, then enables
> M+I+D in one SCTLR write.

rpi4b is the **only** A-class port that:

1. never turns plo's MMU+caches on in the first place (no `hal_memoryInit()`),
2. only civac's the heap on the way out (not the kernel image, not the
   syspage destination),
3. has `mmu_enable()` flip M+C+I together in plo (`mmu.c:73-82`) but doesn't
   call it,
4. has the kernel attempt M-then-C-then-I as separate steps (TD-16) on a
   non-coherent post-handoff DDR.

---

## 3. rpi4b deviations from the established Phoenix idiom

The following are concrete divergences with file:line citations.

### 3.1 plo never enables MMU + caches

`plo/hal/aarch64/zynqmp/hal.c:98-109` (canonical):

```c
void hal_init(void)
{
    _zynqmp_init();
    hal_memoryInit();          /* <- mmu_init + mmu_mapAddr + mmu_enable */
    interrupts_init();
    ...
}
```

`plo/hal/aarch64/generic/hal.c:86-96` (rpi4b):

```c
void hal_init(void)
{
    interrupts_init();
    timer_init();
    console_init();
    video_init();
    hal_printCurrentEl();
    video_markHalReady();
    hal_common.entry = (addr_t)-1;
}
```

No `hal_memoryInit()`. The contract that imx6ull/zynq7000/zynqmp rely on —
"plo built the kernel image into DDR with caches on, then cleaned them at
hal_cpuJump" — is broken on rpi4b. plo writes go directly to DDR
uncached, but the firmware (start4.elf, bootcode.bin, armstub) ran with
caches on and left dirty lines whose owner of the PA is still cache-resident
when the kernel later turns on its D-cache.

### 3.2 plo civac coverage is incomplete

`plo/hal/aarch64/zynqmp/hal.c:257-262`:

```c
hal_dcacheEnable(0);
hal_dcacheFlush((addr_t)ADDR_OCRAM, (addr_t)ADDR_OCRAM + SIZE_OCRAM);
hal_dcacheFlush((addr_t)ADDR_DDR,   (addr_t)ADDR_DDR + SIZE_DDR);
hal_icacheEnable(0);
hal_icacheInval();
```

`plo/hal/aarch64/generic/hal.c:373-388`:

```c
hal_interruptsDisableAll();
hal_consolePrint("hal: jump irq off\n");
hal_coreJumpFlag = 1;
hal_consolePrint("hal: jump exit el1\n");
hal_dcacheFlush((addr_t)__heap_base, (addr_t)__heap_limit);
hal_probeSyspage();
hal_td15ProbeWrite();
hal_exitToEL1();
```

Missing relative to the canonical pattern:

- `hal_dcacheEnable(0)` and `hal_icacheEnable(0)` — symbolically the disable.
  On rpi4b plo these would be no-ops (cache is already off) but the
  *contract* of explicitly disabling before flushing matches the canonical
  shape.
- `hal_dcacheFlush(KERNEL_IMAGE_BASE..KERNEL_IMAGE_END)` — the kernel image
  was just loaded by plo and *firmware* may still own dirty lines for those
  PAs. zynqmp fixes this by civac'ing the entire DDR window.
- `hal_dcacheFlush(SYSPAGE_DEST_BASE..SYSPAGE_DEST_END)` — the syspage
  destination is read by the kernel before any kernel-side cache flush. On
  rpi4b this is partially compensated by the TD-04 NC TTL3 entry in
  `phoenix-rtos-kernel/hal/aarch64/_init.S:537-559`, but that's a workaround
  for plo not having flushed it.
- `hal_icacheInval()` — plo never instruction-cache-invalidates the kernel
  image PAs. rpi4b kernel relies on the armstub having done it before
  `eret` to EL2.

### 3.3 Kernel _init.S does not run all-levels set/way invalidate-all on entry

`phoenix-rtos-kernel/hal/armv7a/zynq7000/_init.S:117-148`:

```
/* Disable L1 caches */
... bic SCTLR.{C,I} ...
/* Invalidate L1 ICache */
mcr p15, 0, r1, c7, c5, 0    /* ICIALLU */
/* Invalidate L1 DCache (set/way loop) */
... mcr p15,0,r2,c7,c6,2 (DCISW) ...
/* Invalidate TLB */
mcr p15, 0, r1, c8, c7, 0
```

`phoenix-rtos-kernel/hal/aarch64/_init.S:304-308` (rpi4b/zynqmp shared):

```
ic ialluis            /* I-cache invalidate inner shareable */
tlbi vmalle1
dsb ish
isb
```

Missing the all-levels D-cache set/way invalidate-all (the canonical
`dc isw` walking CLIDR_EL1 levels). Plo's TD-05 block at
`plo/hal/aarch64/generic/_init.S:194-225` runs an `isw` (invalidate-only)
loop in plo entry — but **this happens before plo writes any data**, not
before kernel entry. Once plo runs (with caches off) and fills DDR, then
hands off to the kernel, the kernel never re-invalidates. A canonical
Phoenix port repeats the invalidate-all on **kernel entry** as well.

The rpi4b kernel does have a `hal_cpuInvalDataCacheAll`
(`phoenix-rtos-kernel/hal/aarch64/_init.S:1188-1242`) but it is **not called
from `_start`**. It exists only as a building block for the parked TD-16
cache-enable.

### 3.4 MMU and caches enabled in different windows

`phoenix-rtos-kernel/hal/armv7a/zynq7000/_init.S:326-345` (canonical
M+C+I together):

```
/* Enable L1 Caches */
... orr r1, r1, #(0x1 << 2)   /* C */
... orr r1, r1, #(0x1 << 12)  /* I */
... orr r1, r1, #(0x1 << 11)  /* Z (branch prediction) */
mcr p15,0,r1,c1,c0,0
... Enable SCU ...
/* Enable MMU */
mrc p15,0,r1,c1,c0,0
orr r1, r1, #1                /* M */
mcr p15,0,r1,c1,c0,0
dsb
isb
```

(Note: zynq7000 enables caches *first*, then MMU — but they're written into
SCTLR via two adjacent RMWs separated by no other state changes.)

`phoenix-rtos-kernel/hal/aarch64/_init.S:444-458` (rpi4b/zynqmp shared
current state):

```
mrs x0, sctlr_el1
orr x0, x0, #(1 << 0)   /* M only */
msr sctlr_el1, x0
isb
... TODO(TD-16-cache-enable) ...
```

The `M+C+I together` recipe matches what `plo/hal/aarch64/mmu.c:73-82` does
in plo (`val |= ((1<<12) | (1<<2) | (1<<0));`). The rpi4b kernel attempted
this in TD-16 Phase B and hit immediate post-flip stalls, but the
*structural* convention across imx6ull, zynq7000, zynqmp, and plo's own
`mmu_enable` is **flip all three together in one SCTLR write**, after
having flushed every dirty line that could ambush the new cacheable view.

### 3.5 Syspage destination not deterministically clean

The canonical ports civac the entire DDR window in plo
(`zynqmp/hal.c:259`). The kernel then copies the syspage into its
`_hal_syspageCopied` BSS slot via plain `ldr`/`str` without further
maintenance — this works because *every* DDR PA was just clean+invalidated.

rpi4b skips that bulk civac; the kernel `_init.S:537-559` instead overrides
a single TTL3 entry to NC over the syspage destination (TD-04). This is a
**point fix** for symptoms, not a port-aligned solution. The right fix —
matching every other A-class Phoenix port — is to civac the kernel image's
target DDR window in plo before `eret`.

---

## 4. Canonical Phoenix-RTOS plo→kernel handoff pattern

Distilled from imx6ull, zynq7000, zynqmp:

```
                  ┌──────────────────────────────┐
                  │  plo runs MMU+caches ON      │
                  │  (built via hal_memoryInit:  │
                  │   mmu_init + mmu_mapAddr     │
                  │   for each region + mmu_     │
                  │   enable that flips M+C+I)   │
                  └──────────────┬───────────────┘
                                 │ load kernel image,
                                 │ build syspage, run cmds
                                 ▼
                  ┌──────────────────────────────┐
                  │  hal_cpuJump():              │
                  │  1. hal_interruptsDisableAll │
                  │  2. hal_dcacheEnable(0)      │
                  │  3. hal_dcacheFlush(OCRAM)   │
                  │  4. hal_dcacheFlush(DDR)     │
                  │  5. hal_icacheEnable(0)      │
                  │  6. hal_icacheInval()        │
                  │  7. mmu_disable()            │
                  │  8. eret/blx to kernel entry │
                  └──────────────┬───────────────┘
                                 │ DDR is bit-clean
                                 │ caches off, MMU off
                                 ▼
                  ┌──────────────────────────────┐
                  │  kernel _start:              │
                  │  - mask interrupts           │
                  │  - SCTLR baseline (M=C=I=0)  │
                  │  - SMPEN (per-arch site)     │
                  │  - dc isw all levels         │
                  │  - ic iallu(is)              │
                  │  - tlbi all                  │
                  │  - copy syspage              │
                  │  - build page tables         │
                  │  - dsb ish; isb              │
                  │  - flip SCTLR.{M,C,I}        │
                  │    in one write              │
                  │  - br high-VA stub → main    │
                  └──────────────────────────────┘
```

Key invariants:

1. **plo does the bulk DDR civac**, not the kernel. Kernel set/way is a
   safety belt over local L1, not the primary mechanism.
2. **MMU and caches are flipped together** in the kernel (`SCTLR.{M,C,I}`
   single write).
3. **Per-VA range maintenance is reserved for runtime drivers** (DMA
   buffers in `sdcard.c`, framebuffer in `video.c`). It is never the
   handoff mechanism.

---

## 5. Specific fix recommendations

### 5.1 plo `aarch64a72-generic-rpi4b` — align with canonical idiom

Two patches, ordered by risk:

**(A) Add full DDR civac before eret.** Edit
`plo/hal/aarch64/generic/hal.c:364-399` `hal_cpuJump()`:

```c
int hal_cpuJump(void)
{
    if (hal_common.entry == (addr_t)-1) {
        return -1;
    }

    hal_interruptsDisableAll();
    hal_coreJumpFlag = 1;

    /* Canonical Phoenix handoff: civac every plo-touched DDR window so the
     * kernel sees a deterministic image. Mirrors zynqmp/hal.c:257-262. */
    hal_dcacheFlush((addr_t)ADDR_DDR, (addr_t)ADDR_DDR + SIZE_DDR);
    hal_icacheInval();

    hal_exitToEL1();
    return 0;
}
```

(`ADDR_DDR`/`SIZE_DDR` already exist in `plo/hal/aarch64/generic/config.h`.)

This is a no-op when plo's caches are off — `dc civac` on a non-cached
mapping is permitted by the architecture and acts as a barrier. But it
encodes the contract symbol-for-symbol with the other A-class ports and
prepares the path for (B).

**(B) Bring up plo's own MMU+caches.** Add `hal_memoryInit()` and call it
from `hal_init()`, matching `plo/hal/aarch64/zynqmp/hal.c:64-95`:

```c
static void hal_memoryInit(void)
{
    size_t sz;
    addr_t addr;

    mmu_init();

    for (sz = 0; sz < SIZE_DDR; sz += SIZE_MMU_SECTION_REGION) {
        addr = ADDR_DDR + sz;
        mmu_mapAddr(addr, addr, MMU_FLAG_CACHED);
    }
    /* Map BCM2711 peripheral window uncached */
    for (sz = 0; sz < SIZE_PERIPH; sz += SIZE_MMU_SECTION_REGION) {
        addr = ADDR_PERIPH + sz;
        mmu_mapAddr(addr, addr, MMU_FLAG_UNCACHED | MMU_FLAG_XN);
    }

    mmu_enable();
}
```

With (B) in place, plo runs MMU+caches on (matching firmware's pre-handoff
state), then the canonical `hal_dcacheEnable(0)/flush(DDR)/icacheInval/
mmu_disable` sequence in `hal_cpuJump()` becomes load-bearing — and the
kernel boots with truly clean DDR.

### 5.2 Kernel `phoenix-rtos-kernel/hal/aarch64/_init.S` — call invalidate-all

Insert a call to `hal_cpuInvalDataCacheAll` (already defined at `:1188-1242`)
into `_start` after the existing `ic ialluis; tlbi vmalle1` block at
`_init.S:304-308`:

```
ic ialluis
tlbi vmalle1
dsb ish
isb

/* Phoenix idiom: drop every D-cache line up to LoC before touching any
 * memory the kernel will later read with caches enabled. Matches the set/way
 * loop used by armv7a/zynq7000/_init.S:132-148 and armv7a/imx6ull/
 * _init.S:529-545. */
bl hal_cpuInvalDataCacheAll
```

This restores the symmetry with the armv7a kernels: every A-class Phoenix
kernel does the all-levels D-cache invalidate-all on entry. The current
shared aarch64 `_init.S` does only the I-cache invalidate, which is the
asymmetry that lets BCM2711 firmware-owned dirty lines linger.

Once 5.1(B) is in place and plo civac's all of DDR, this kernel-side
invalidate is redundant but harmless and matches the canonical safety belt.

### 5.3 (Optional) Move the cache-enable to the canonical site

After 5.1+5.2, retry the `M+C+I together` SCTLR write at
`phoenix-rtos-kernel/hal/aarch64/_init.S:444-449` — i.e. replace the M-only
flip with the full one:

```
mrs x0, sctlr_el1
orr x0, x0, #((1 << 12) | (1 << 2) | (1 << 0))   /* I, C, M */
msr sctlr_el1, x0
isb
```

Mirroring `plo/hal/aarch64/mmu.c:78-79`. The TD-16 attempts that previously
failed all happened in a regime where (a) plo didn't civac the kernel image,
(b) the kernel didn't `dc isw` all-levels at entry, (c) BCM2711 firmware-
owned dirty lines could shadow page-table walks. Once those are addressed
the joint flip is the architecturally normal site.

---

## 6. Phoenix documentation pointers

The canonical pattern is described informally in
`phoenix-rtos-doc/loader/architecture.md:70-74`:

> "Platform initialization is one of the most important part of the loader.
> [...] It prepares the most low-level components of the processor like
> cache invalidation, [...] disabling MMU and FPU."

— i.e. the loader is responsible for cache+MMU teardown before kernel
entry. The rpi4b plo never *did* the setup that makes the teardown
meaningful, which is precisely the gap §3.1/§3.2 captures.

`phoenix-rtos-doc/loader/architecture.md:136-140` describes syspage as the
single point of interface between loader and kernel; the assumption that
syspage bytes are bit-clean across the handoff is implicit, and the
canonical ports enforce it via the civac-DDR step.

No top-level doc in `phoenix-rtos-doc/` enumerates the SCTLR flip or
set/way maintenance contract — the "spec" lives in the per-port `_init.S`
files themselves. That makes it easy for a new port (rpi4b) to deviate
silently. A future cleanup item is to lift §4's diagram into
`phoenix-rtos-doc/loader/architecture.md` so the contract is explicit.

---

## 7. Bottom line

The rpi4b port is the only A-class Phoenix port that:

- never enables plo's MMU+caches,
- only civac's plo's heap (not the kernel image, not the syspage dest, not
  the broader DDR window) before `eret`,
- omits the all-levels set/way D-cache invalidate-all in kernel `_start`,
- attempts to enable kernel caches in a separate window from MMU enable.

Each one of these is a fixable deviation — and they compound. §5.1(A) is
the smallest patch that restores the canonical handoff symmetry; §5.1(B) +
§5.2 + §5.3 take the rpi4b port back onto the same convention as imx6ull,
zynq7000, and zynqmp. After that the BCM2711 cache-coherency anomaly
should look much more like a tractable per-line issue (TD-04) than the
current cross-cutting "the entire handoff is non-canonical" failure mode.
