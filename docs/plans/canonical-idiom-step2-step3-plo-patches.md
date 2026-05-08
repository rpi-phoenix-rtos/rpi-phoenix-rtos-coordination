# Canonical-idiom alignment — Step 2 + Step 3 plo patches

This document holds two ready-to-apply unified diffs against the
`plo/hal/aarch64/generic/` tree, implementing Steps 2 and 3 of the
canonical-idiom alignment plan in
[`docs/research/round3-cache-enable-synthesis.md`](../research/round3-cache-enable-synthesis.md)
§5.

The goal of both steps is to make rpi4b's plo→kernel handoff match
the idiom that already works on `imx6ull`, `zynq7000`, and `zynqmp`.
The reference for "what canonical Phoenix plo looks like on a 64-bit
A-class core" is `plo/hal/aarch64/zynqmp/hal.c`. Where rpi4b deviates,
we cite the corresponding zynqmp lines.

Both patches are deliberately surgical: they touch the smallest
amount of code needed to achieve the canonical-idiom property, and
they preserve every existing rpi4b-specific diagnostic (TD-15
mailbox-buffer probe, syspage probe, marker UART output) so the
existing trace flow stays intact.

## Background — what each step achieves

**Step 2** addresses canonical-idiom deviation #2 from
`round3-phoenix-port-conventions-audit.md` (also summarised in the
synthesis doc §3.1, table row 2): rpi4b's `hal_cpuJump`
(`plo/hal/aarch64/generic/hal.c:386`) currently flushes only the
heap range `[__heap_base, __heap_limit)`. Canonical Phoenix idiom
flushes the entire DDR plus OCRAM by VA-to-PoC
(`plo/hal/aarch64/zynqmp/hal.c:258-259`). This matters once Step 3
turns plo's caches on — every cache line that may hold any plo
write (linker-mapped data, BSS, stack-allocated transients,
syspage padding, kernel image bytes copied via
`hal_kernelEntryPoint`) must reach DDR before the kernel takes
over with caches OFF and starts reading those PAs uncached.

In the current caches-off plo, this expansion is functionally a
no-op: there are no cached lines to flush. That is what makes Step 2
safe to land first. It is the structural precondition for Step 3
without changing any observable boot signature.

**Step 3** addresses canonical-idiom deviation #1 (synthesis doc §3.1
table row 1): rpi4b's `hal_init`
(`plo/hal/aarch64/generic/hal.c:86-96`) does not call
`hal_memoryInit()`. Canonical zynqmp `hal_init`
(`plo/hal/aarch64/zynqmp/hal.c:98-109`) calls `hal_memoryInit`,
which programs the MMU and enables D-cache + I-cache for the entire
plo-resident execution. Step 3 ports that pattern to rpi4b.

After Step 3, plo executes its main body (kernel image load, syspage
construction, video init) with MMU+caches on, mirroring zynqmp.
Stores into DDR now go through the D-cache, so the Step-2 full-DDR
`dc civac` in `hal_cpuJump` becomes load-bearing — it is now what
makes those writes visible to the cache-off kernel that takes over
at `hal_exitToEL1`.

Step 3 also adds the matching tear-down ritual to `hal_cpuJump`
(disable I/D caches, then `mmu_disable`), exactly as zynqmp does at
`plo/hal/aarch64/zynqmp/hal.c:255-266`.

## Step 2 — Extend `hal_cpuJump` flush range to full DDR

### What this changes

In `plo/hal/aarch64/generic/hal.c`, the existing `hal_dcacheFlush`
call on line 386 — which currently flushes only the heap range
`[(addr_t)__heap_base, (addr_t)__heap_limit)` — is replaced by a
full DDR flush from `ADDR_DDR` (= 0x0 on rpi4b per
`plo/ld/aarch64a72-generic.ldt:26`) to `ADDR_DDR + SIZE_DDR`
(= 0x3b400000, the BCM2711 1GB ARM-usable bank per
`plo/ld/aarch64a72-generic.ldt:27`). The heap range is a strict
subset of DDR, so coverage is preserved and broadened.

Pi 4 has no separate on-chip RAM bank in the address layout used by
plo (cf. zynqmp which has both `ADDR_OCRAM` and `ADDR_DDR`), so the
extra `dcacheFlush(ADDR_OCRAM, ADDR_OCRAM+SIZE_OCRAM)` line from
zynqmp is intentionally not ported.

### Why this matches canonical Phoenix idiom

Reference: `plo/hal/aarch64/zynqmp/hal.c:258-259`. The zynqmp
canonical handoff issues a full-DDR civac before `mmu_disable`, on
the principle that any line plo touched anywhere in DDR must reach
PoC before the kernel takes over. The heap-only civac on rpi4b is a
historical artefact from when plo was assumed to write only into the
heap-allocated syspage; once Step 3 turns plo's caches on, every
linker section sitting in DDR (text, rodata, data, bss, stack — all
mapped into `m_ram` via `plo/ld/aarch64a72-generic.ldt:36`) can
hold dirty lines, and only a full-DDR civac reliably clears them.

### Diff

```diff
--- a/hal/aarch64/generic/hal.c
+++ b/hal/aarch64/generic/hal.c
@@ -375,15 +375,29 @@ int hal_cpuJump(void)
 	hal_coreJumpFlag = 1;
 	hal_consolePrint("hal: jump exit el1\n");
 
-	/* Clean+invalidate plo's heap (which holds the syspage and everything
-	 * allocated via syspage_alloc) by VA to PoC so every dirty line reaches
-	 * DDR before the kernel takes over. Without this, Cortex-A72 kernel
-	 * reads of plo-written PAs show bit-level nondeterminism — mostly
-	 * deterministic but with some cache lines stranded above DDR. Set/way
-	 * (dc cisw) is documented by ARM as unreliable for inter-observer
-	 * coherency — it only covers L1 and doesn't hit PoC reliably — so we
-	 * must use civac by VA over the heap range. */
-	hal_dcacheFlush((addr_t)__heap_base, (addr_t)__heap_limit);
+	/* Clean+invalidate the entire ARM-usable DDR bank by VA to PoC so
+	 * every dirty line plo may have produced reaches DDR before the
+	 * kernel takes over with caches OFF.
+	 *
+	 * Canonical Phoenix idiom: zynqmp's hal_cpuJump flushes the full
+	 * DDR + OCRAM range here, not just the heap. See
+	 *   plo/hal/aarch64/zynqmp/hal.c:258-259
+	 * for the reference pattern.
+	 *
+	 * Once plo runs with MMU+caches on (Step 3 of the canonical-idiom
+	 * alignment plan in docs/research/round3-cache-enable-synthesis.md
+	 * §5), any DDR address plo touched can hold a dirty line: not just
+	 * the heap-resident syspage, but also .data, .bss, the kernel ELF
+	 * bytes copied during load, and any stack-derived transients. The
+	 * earlier heap-only civac is a strict subset and would leave those
+	 * other lines stranded.
+	 *
+	 * Set/way (dc cisw) is documented by ARM as unreliable for
+	 * inter-observer coherency (covers L1 only, doesn't hit PoC), so
+	 * we keep using civac by VA across the full bank. ADDR_DDR and
+	 * SIZE_DDR are defined in plo/ld/aarch64a72-generic.ldt:26-27.
+	 */
+	hal_dcacheFlush((addr_t)ADDR_DDR, (addr_t)ADDR_DDR + (addr_t)SIZE_DDR);
 
 	hal_probeSyspage();
```

`ADDR_DDR` and `SIZE_DDR` are exposed to C via the linker template
include chain `config.h` → `ld/aarch64a72-generic.ldt`
(`plo/hal/aarch64/generic/config.h:82-86`), so no extra header is
required. `hal_dcacheFlush` is already declared in
`plo/hal/aarch64/cache.h:32` and pulled in via the existing
`#include "../cache.h"` at `plo/hal/aarch64/generic/hal.c:18`.

### Compile cleanliness

- Two `addr_t` casts match the existing call's casting style.
- The line iterator in `hal_dcacheFlush`
  (`plo/hal/aarch64/cache.c:100-113`) walks by cache-line-size
  increments, so a 944 MiB range is fine algorithmically — it is
  what zynqmp already does over its DDR.
- The `__heap_base` / `__heap_limit` linker symbols stay extern'd
  at the top of the file (still used by `hal_memoryGetNextEntry`
  in `plo/hal/aarch64/generic/hal.c:203,216`) — no symbol removal
  needed.

### Observable boot signature

Identical to today: plo runs caches off, no dirty lines exist, the
loop runs but every `dc civac` is a no-op. UART trace, video frame
output, and kernel boot progression are unchanged. The patch's job
is to make Step 3 sufficient, not to change current behaviour.

---

## Step 3 — Run plo with MMU + caches enabled

### What this changes

Three coordinated edits in `plo/hal/aarch64/generic/hal.c`:

1. Add a new `hal_memoryInit()` static helper modelled on
   `plo/hal/aarch64/zynqmp/hal.c:64-95`. It calls `mmu_init()`,
   maps the ARM-usable DDR bank as cached Normal memory in
   2 MiB sections, and calls `mmu_enable()`.
2. Call `hal_memoryInit()` from `hal_init()` after
   `interrupts_init()` and before `console_init()` /
   `video_init()`, mirroring the zynqmp ordering at
   `plo/hal/aarch64/zynqmp/hal.c:98-109`.
3. In `hal_cpuJump`, immediately before the existing full-DDR
   civac added in Step 2, disable D-cache + I-cache and after the
   civac call `mmu_disable`, mirroring
   `plo/hal/aarch64/zynqmp/hal.c:255-266`.

### Why this matches canonical Phoenix idiom

Reference: `plo/hal/aarch64/zynqmp/hal.c:64-95` (memoryInit) and
`plo/hal/aarch64/zynqmp/hal.c:98-109` (hal_init body). The
canonical pattern is: plo runs its full execution with MMU + I/D
caches on, so syspage construction, kernel ELF copy, and any
linker-mapped bookkeeping use cacheable Normal memory. The
loader's video init, console writes, and big DDR copies all
benefit from cacheability. Then at the very end of `hal_cpuJump`,
the loader cleans the whole DDR back to PoC (Step 2), drops the
caches, drops the MMU, and `eret`s into the kernel — which then
takes over with M=C=I=0 and rebuilds its own translation regime.

This is the exact lifecycle Phoenix's other A-class ports rely on,
and it is what the rpi4b kernel-side cache-coherency assumptions
were written against (cf. the round-3 audit
`docs/research/round3-phoenix-port-conventions-audit.md`).

### Constraints carried by `mmu.c`

The existing `plo/hal/aarch64/mmu.c` programs `ttbr0_el3`, `tcr_el3`,
`mair_el3`, `sctlr_el3`, and uses `tlbi alle3` / `tlbi vae3`
(`mmu.c:55,65-67,77-79,89-91,124-126`). It is therefore valid only
when plo is executing at EL3.

Pi 4 firmware + the Phoenix armstub currently leave the cores at
EL3 for plo (this is consistent with `plo/hal/aarch64/cache.c:120,125`
also writing `sctlr_el3` and the rpi4b boot succeeding with the
existing cache toggles when used). The Step 3 patch therefore
relies on the same EL3 contract that the rest of plo's
caches-on machinery already assumes. If the EL contract ever
changes (e.g. if armstub starts ERET'ing into plo at EL2), `mmu.c`
will need a small generalisation — but that is out of scope for
Step 3 and is independently flagged in
[`docs/code-quality-and-upstreaming.md`](../code-quality-and-upstreaming.md)
as part of the upstreaming cleanup.

### Diff

```diff
--- a/hal/aarch64/generic/hal.c
+++ b/hal/aarch64/generic/hal.c
@@ -15,6 +15,7 @@
 #include <hal/hal.h>
 
 #include "../cpu.h"
+#include "../mmu.h"
 #include "../cache.h"
 
 
@@ -83,8 +84,38 @@ static u32 hal_readBe32(addr_t addr)
 }
 
 
+/* Map the ARM-usable DDR bank (ADDR_DDR..ADDR_DDR+SIZE_DDR) as cached
+ * Normal memory and enable the MMU + D/I caches, matching the canonical
+ * Phoenix A-class loader idiom.
+ *
+ * Reference: plo/hal/aarch64/zynqmp/hal.c:64-95 — same pattern, with an
+ * additional OCRAM and bitstream mapping that rpi4b's address layout
+ * does not have. The Pi 4 BCM2711 has no equivalent OCRAM region at
+ * this stage of bring-up; the only memory plo touches is the 1 GiB
+ * ARM bank declared in plo/ld/aarch64a72-generic.ldt:26-27 (ADDR_DDR =
+ * 0x0, SIZE_DDR = 0x3b400000), which subsumes m_ram (0x00200000,
+ * 0x00200000) where plo itself is linked.
+ *
+ * MMIO regions (PL011 UART, GIC, mailbox, framebuffer) inherit the
+ * default 32-bit-range Device mapping installed by mmu_init() — see
+ * plo/hal/aarch64/mmu.c:140-147 — so console_init / video_init that
+ * follow continue to work unchanged.
+ */
+static void hal_memoryInit(void)
+{
+	size_t sz;
+	addr_t addr;
+
+	mmu_init();
+
+	for (sz = 0; sz < (size_t)SIZE_DDR; sz += SIZE_MMU_SECTION_REGION) {
+		addr = (addr_t)ADDR_DDR + sz;
+		mmu_mapAddr(addr, addr, MMU_FLAG_CACHED);
+	}
+
+	mmu_enable();
+}
+
+
 void hal_init(void)
 {
 	interrupts_init();
+	hal_memoryInit();
 	timer_init();
 	console_init();
 	video_init();
@@ -375,6 +406,15 @@ int hal_cpuJump(void)
 	hal_coreJumpFlag = 1;
 	hal_consolePrint("hal: jump exit el1\n");
 
+	/* Tear down the cacheable execution environment we set up in
+	 * hal_memoryInit() so the kernel takes over with M=C=I=0, exactly
+	 * mirroring the canonical zynqmp pattern at
+	 * plo/hal/aarch64/zynqmp/hal.c:255-266.
+	 *
+	 * Order matters: drop D-cache enable first, then issue the
+	 * full-DDR civac (so no further fills can race the flush), then
+	 * drop I-cache, invalidate I-cache, and only after that
+	 * mmu_disable. */
+	hal_dcacheEnable(0);
+
 	/* Clean+invalidate the entire ARM-usable DDR bank by VA to PoC so
 	 * every dirty line plo may have produced reaches DDR before the
 	 * kernel takes over with caches OFF.
@@ -395,6 +435,11 @@ int hal_cpuJump(void)
 	 */
 	hal_dcacheFlush((addr_t)ADDR_DDR, (addr_t)ADDR_DDR + (addr_t)SIZE_DDR);
 
+	hal_icacheEnable(0);
+	hal_icacheInval();
+
+	mmu_disable();
+
 	hal_probeSyspage();
 
 	/* TD-15 phase 1: stamp mailbox buffer with known pattern; kernel
```

(The Step 3 diff is shown layered on top of Step 2, since both
target the same `hal_cpuJump`. The final `mmu_disable` lands
before the existing `hal_probeSyspage` / `hal_td15ProbeWrite`
calls, which run cache-off as before — no semantic change to those
diagnostics.)

### Compile cleanliness

- The new `#include "../mmu.h"` pulls in `mmu_init` /
  `mmu_enable` / `mmu_disable` / `mmu_mapAddr` /
  `MMU_FLAG_CACHED` / `SIZE_MMU_SECTION_REGION` (declared in
  `plo/hal/aarch64/mmu.h:21-39`).
- `hal_dcacheEnable`, `hal_icacheEnable`, `hal_icacheInval` are
  declared in `plo/hal/aarch64/cache.h:26,35,38` and already
  reachable through the existing `#include "../cache.h"`.
- `ADDR_DDR` and `SIZE_DDR` resolve through
  `plo/hal/aarch64/generic/config.h:82-86`.
- `size_t` and `addr_t` follow the same conventions used in the
  zynqmp reference.
- The placement of `hal_memoryInit()` after `interrupts_init()`
  matches `plo/hal/aarch64/zynqmp/hal.c:98-103` exactly (zynqmp
  calls `_zynqmp_init` first because it has board-init that must
  precede MMU setup; the rpi4b port has no such board-init step,
  so we just preserve the existing `interrupts_init()` ordering).

### Observable boot signature

After Step 3 lands, the plo phase prints faster (cacheable text /
data accesses, faster framebuffer writes via the cached DDR alias)
and the existing UART markers (`hal: entry EL?`, `hal: jump
entry`, `hal: jump irq off`, `hal: jump exit el1`) all still fire
in the same order. The kernel side is unchanged — kernel `_start`
still finds caches off when it begins execution, because Step 3
adds the matching `dcacheEnable(0) / dcacheFlush(DDR) /
icacheEnable(0) / icacheInval / mmu_disable` tear-down inside
`hal_cpuJump` before `hal_exitToEL1`.

The acceptance signal for Step 3 is "boot still reaches `(psh)%`
on real Pi 4". A regression here would mean either (a) the EL3
assumption above is now wrong, or (b) the cache enable interacts
with one of the existing TD-15 / E1 probes in an unexpected way —
either of which is independently observable via UART trace and
recoverable via the `manifests/` rollback machinery.

---

## Application order and rollback

Step 2 must be applied first. It is a no-op while plo runs caches
off, so its sole purpose is to ready the flush range for Step 3.
Validate Step 2 by running the standard build loop
(`./scripts/rebuild-rpi4b-fast.sh` then `./scripts/capture-rpi4b-uart.sh`)
and confirming the boot signature is unchanged.

Step 3 is the actual cache-on transition. After it lands, snapshot
the integration state via `./scripts/snapshot-integration-state.sh`
and tag the manifest in `manifests/`. If any subsequent step
(canonical-idiom Step 4 or Step 5) regresses, restore via
`./scripts/restore-integration-state.sh manifests/<step3>.md`
and bisect from there.

Both diffs leave the kernel side untouched. Once they land,
attention shifts to the kernel `_init.S` work in synthesis-doc
Steps 1, 4, and 5
([`docs/research/round3-cache-enable-synthesis.md:176-241`](../research/round3-cache-enable-synthesis.md)).
