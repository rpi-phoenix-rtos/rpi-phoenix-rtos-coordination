# q3dm7 V3D binner stall — source-level root-cause analysis

Date: 2026-08-22
Scope: read-only comparison of our winsys vs Mesa (`external/mesa`) vs Linux
(`external/linux/drivers/gpu/drm/v3d`). NO builds, NO HW. Produces a ranked
root-cause hypothesis + a concrete proposed code change to build/HW-test next.

## The HW-confirmed wedge signature (recap)

Running quake3 `q3dm7` (5823 faces), ~50% of boots the V3D binner freezes. The
instrumented dump at the wedge shows, decisively:

- `int_sts = 0` — no FLDONE, **no INT_OUTOMEM**, no error IRQ pending.
- `int_qpu = 0x000` — no coordinate/vertex-shader QPU interrupt pending.
- `bpoa = 0, bpos = 0, ovf_armed = 0` — the binner-overflow pool was **never armed**;
  the binner never signalled out-of-memory.
- `bpca = 0x08e54000, bpcs = 0x0007f000` — the PTB is parked partway through the
  current tile-alloc block with **520 KiB (0x7f000) still free** in that block.
- `gmpvio = 0`, `MMU_VIO_ADDR = 0`, `MMU_VIO_ID = 0` — no GMP protection fault,
  no MMU page fault.
- `ct0ca = 0x048a629e`, parked **inside its own valid BCL BO** `[0x48a6000..0x48a635d]`
  (single BO-handle-46 match), i.e. offset 0x29e ≈ 670 bytes into an ~861-byte list.
- `ct0cs = 0x00000020` (CT0CS bit 5 set).
- Data-dependent + deterministic *within* a boot (re-submitting the same frame
  re-hangs at the same `ct0ca` across GPU resets), intermittent *across* boots (~50%).
  `q3dm1` (1942 faces) never wedges. Linux/RPi-OS renders `q3dm7` on the same silicon.

## Hypothesis ranking

### (a) Undersized tile_alloc / tile_state — REFUTED by the dump

The dump forecloses this directly: `bpcs = 0x7f000` means the binner still had
520 KiB free in its *current* tile-alloc block, `bpoa/bpos = 0` and `ovf_armed = 0`
mean the overflow pool was never armed, and `int_sts = 0` means INT_OUTOMEM never
fired. The binner did not run out of tile-alloc memory, so it cannot be stalled
"silently blocked on a write" for lack of space either — the PTB only stalls for
memory *after* raising OUTOMEM (`external/linux/.../v3d_irq.c` overflow path;
`v3d_phoenix_winsys.c:1001-1035` services exactly that IRQ), which did not happen.

For completeness, the sizes are Mesa-computed and the port does **not** override
them. `v3d_phoenix_winsys.c:998` programs `CT0QMA/QMS` straight from `s->qma/s->qms`,
which Mesa fills in `external/mesa/src/gallium/drivers/v3d/v3d_job.c:702-703`
(`qma = job->tile_alloc->offset`, `qms = job->tile_alloc->size`) from
`v3d_tile_alloc_sizes()` (`external/mesa/src/broadcom/common/v3d_util.c:271-313`).
For 1920×1080, 64×64 tiles → 30×17 = 510 tiles:

- `tiles_size = 510 * 128 = 65 280` B → `align(…,4096) = 65 536` → `+8192 = 73 728`
  → `+ MIN2((65 280*draws)/2, 512 KiB)` (caps at 512 KiB once draws ≥ ~16)
  → **tile_alloc ≈ 570–584 KiB**, plus the (unbounded-in-Linux) 256 KiB overflow blocks.
- `tile_state = 510 * 256 = 130 560` B ≈ 127.5 KiB.

This is identical to what Mesa+Linux would allocate. Not the differentiator.
**Rank: refuted.**

### (b) CPU→GPU coherency race — CPU side CLOSED; only the GPU-internal side remains

The CPU-side coherency window is closed for every buffer the binner reads:

- `v3d_phoenix_winsys.c:938` issues `dsb sy` (completion barrier, not `dmb`) before
  the first GPU MMIO poke, draining all prior CPU stores to the point of coherency
  the non-coherent V3D DMA master observes.
- Every BO the binner consumes is mapped **uncached** (Normal-Non-Cacheable), so
  `dsb sy` is sufficient and no CPU cache-clean is needed. Confirmed end-to-end:
  the CL, tile_alloc, tile_state, shader-record and program BOs are allocated with
  the default `v3d_bo_alloc()` (flags = 0) — `external/mesa/.../v3d_cl.c:63,94`,
  `v3d_job.c:566,568`, `v3d_program.c:541` — and the winsys only drops `MAP_UNCACHED`
  when `flags & 0x1` is set (`v3d_phoenix_winsys.c:604-606`). The only flag Mesa
  ever passes on this port is `V3D_CREATE_BO_SCANOUT` for large RTs, with
  `V3D_CREATE_BO_CACHEABLE` explicitly **disabled** in `v3d_resource.c:141-145`.
  So no binner-input BO is CPU-cacheable; there is no dirty-cache-line hole.

What is *not* closed is the **GPU-internal** coherency of a **recycled GPU VA**.
The port reuses freed GPU VAs (`V3D_VA_NO_RECYCLE = 0`, `v3d_phoenix_winsys.c:185-190`;
`va_free`/`vahole` recycling). Per submit it flushes the MMU TLB
(`mmu_flush_tlb`, `:960`) and flushes L2T + invalidates the slice caches
(`:978-994`), which *should* drop stale GPU-side lines for a reused VA — but this is
the one remaining mechanism by which the binner could read stale GPU-cached/PTE
data for a VA that previously backed a different BO. This matches the owner's own
tracked lead ("stale-PTE/VA-recycle") and, crucially, matches the **boot-variance**:
the specific VAs assigned to q3dm7's BOs depend on allocation order/history, which
differs across boots, so whether a given frame lands a poisoned recycled VA is a
per-boot dice-roll — yet deterministic once the layout for that boot is fixed. A
pure per-frame CPU-store race would instead vary frame-to-frame *within* a boot.
**Rank: leading class (GPU-internal / stale-VA-recycle sub-case).**

### (c) TILE_BINNING_MODE_CFG mismatch — REFUTED as a per-map differentiator

`TILE_BINNING_MODE_CFG` is emitted by **shared upstream Mesa** BCL packing
(`external/mesa/.../v3dx_draw.c:85-102` for V3D 4.2: width/height, RT count,
`multisample_mode_4x`, `double_buffer_in_non_ms_mode`, `maximum_bpp`, and the
initial/overflow block-size enums that must match `V3D_TILE_ALLOC_INITIAL_BLOCK_SIZE`
= 128, `v3d_limits.h:88-89`). These values are a function of the framebuffer and
render state, which are **identical** for q3dm1 and q3dm7 at the same resolution.
A config bug here would wedge both maps equally; q3dm1 never wedges.
**Rank: refuted as the differentiator.**

### (d) Coordinate-shader / VPM / QPU-concurrency stall — the other leg of the leading class

`ct0ca` parked *cleanly inside the correct BO, in range*, with zero error status is
the classic signature of a binner blocked on a CL item that is **awaiting a
sub-unit** (coordinate-shader QPUs producing transformed vertices into VPM, or a
VCD/VPM resource), rather than a corrupt-branch/wild-address failure (which would
drive `ct0ca` out of range). This scales with geometry (q3dm7 ≈ 3× q3dm1 faces →
more coord-shader work / VPM pressure per tile), fitting the data-dependence.

Note a real but **secondary** port divergence in this area: the winsys writes
`CTL_MISCCFG = (QRMAXCNT=2)<<1 | OVRTMUOUT` once at init
(`v3d_phoenix_winsys.c:842-848`), whereas Linux **never** writes MISCCFG on V3D 4.2
(`v3d_gem.c` sets invariant state but leaves MISCCFG at the firmware default;
`v3d_regs.h:241-244` documents the field). QRMAXCNT is the bin-vs-render QPU-reserve
split; a wrong split can starve coordinate-shader QPUs on heavy-geometry frames.
This is demoted, not headlined, for two reasons: (1) it is **boot-invariant** (same
value every boot) so it fits "always/never wedge", not the ~50% split, unless it
merely *narrows the margin* that a per-boot factor then tips; (2) it was already
A/B-tuned (the `:844-847` comment claims 2 "zeroes both wedge classes here"), so
re-turning that knob is re-treading tried ground. Keep it as a fallback lever, not
the primary fix.

### Non-causes explicitly cleared

- **L2T flush waited vs Linux fire-and-forget** (`v3d_phoenix_winsys.c:978-994` vs
  `v3d_gem.c:177-191`): waiting for the flush to *complete* is strictly safer than
  Linux's hardware-interlock reliance — it cannot make the binner read a not-yet-
  flushed line. Not a wedge cause.
- **SLCACTL reordered to the front** (`v3d_phoenix_winsys.c:953` vs Linux issuing it
  last in `v3d_invalidate_caches`): the core is idle between the invalidate and the
  CT0 kick, so nothing refills the slice caches — behaviourally equivalent. Not a
  wedge cause.

## Most-likely root cause (single statement)

The binner is **blocked mid-BCL on a sub-unit (coord-shader/VPM) or on a stale
GPU-side read of a recycled GPU VA** — a GPU-internal condition, not a tile-alloc
shortage, not a CPU-store race, not a CL config mismatch. The ~50%/boot,
deterministic-within-boot, geometry-scaled profile most specifically implicates the
**recycled-VA path** (`V3D_VA_NO_RECYCLE = 0`), whose per-boot address layout decides
whether q3dm7's larger working set lands a poisoned VA.

**A pure source comparison cannot discriminate the two legs of this class** (coord/VPM
stall vs stale-VA read). Both are consistent with every observed register. The
honest, highest-value next step is therefore an **instrument**, delivered as the
concrete code change below.

## Proposed change #1 (build + HW next): make the wedge dump show the stalling item

The current `BIN TIMEOUT` dump does not reveal *what* the binner is parked on. It
dumps 40 words **from `bcl_start`** (`v3d_phoenix_winsys.c:1073-1079`) = the first
160 bytes, but the freeze is at `ct0ca` = offset 0x29e ≈ **word 167** — the dump
never reaches it. Add, in the `if (spins == 0)` block (`v3d_phoenix_winsys.c:1044-1082`):

1. **A CL window centred on `ct0ca`.** `uint32_t *cp = gpuva_to_cpu(c0[0x0110/4] & ~3u);`
   then print ~16 words before/after and decode the leading byte of the word at
   `ct0ca` as the CLE opcode. This is the single most diagnostic add:
   - a PRIMITIVE / vertex-array draw item ⇒ binner waiting on coord-shader/VPM (leg d);
   - a BRANCH / semaphore / sub-list item ⇒ CL-flow / tile-list wait;
   - zeros / a wild opcode ⇒ stale GPU-side read (leg b, recycled-VA).

2. **The CLE progress + status registers** (offsets from `external/linux/.../v3d_regs.h`):
   `CT0LC` (0x120), `CT0PC` (0x128), `PCS` (0x130), `BFC` (0x134). A frozen `CT0PC`
   with `ct0ca` frozen confirms the binner made no primitive progress; `PCS`/`BFC`
   localise which control-list executor is busy.

3. **The front-end debug/stall registers** `ERR_FDBGO` (0xf04), `ERR_FDBGB` (0xf08),
   `ERR_FDBGR` (0xf0c), `ERR_FDBGS` (0xf10). `FDBGS` has explicit per-stage *STALL*
   bits (`v3d_regs.h:485-498`) that directly name where the pipeline is wedged — the
   decisive read for a silent stall.

4. **Decode `ct0cs`** (already captured as `c0[0x0100/4]`, value 0x20 = bit 5). The
   Linux header only defines the offset (`v3d_regs.h:292`), not the bits, so this bit
   needs the V3D 4.2 TRM CTnCS decode; capture it labelled so it can be looked up.

Diff idea (illustrative, inside the existing timeout block ~`:1053-1057`):

```c
/* localise the silent stall: CL item at ct0ca + FE debug/stall regs */
uint32_t ca = c0[0x0110/4];
uint32_t *cp = (uint32_t *)gpuva_to_cpu(ca & ~3u);
fprintf(stderr, "v3d-winsys: BIN CT0 ct0cs=0x%08x ct0lc=0x%08x ct0pc=0x%08x "
    "pcs=0x%08x bfc=0x%08x\n", c0[0x0100/4], c0[0x0120/4], c0[0x0128/4],
    c0[0x0130/4], c0[0x0134/4]);
fprintf(stderr, "v3d-winsys: BIN FDBG o=0x%08x b=0x%08x r=0x%08x s=0x%08x\n",
    c0[0x0f04/4], c0[0x0f08/4], c0[0x0f0c/4], c0[0x0f10/4]);
if (cp) { fprintf(stderr, "v3d-winsys: BIN CL@ct0ca(0x%08x) op=0x%02x:", ca,
    (*cp) & 0xffu); for (int i=-4;i<8;i++) fprintf(stderr, " %08x", cp[i]); 
    fprintf(stderr,"\n"); }
```

This is env-free, zero-cost on the happy path (only runs on a wedge), touches no
submit-timing code, and turns the current ambiguous dump into a decisive one.

## Proposed change #2 (candidate fix, gated on what #1 shows)

If #1's CL-at-`ct0ca` decode shows a valid primitive/draw item **and** `FDBGS`
shows a coord/VCD/VPM stall while `int_qpu = 0`, the recycled-VA lead is strongly
implicated. The scaffolding for the A/B already exists and was only "held off"
because the prior ~15-30% base rate made a 12-boot A/B inconclusive
(`v3d_phoenix_winsys.c:175-190`); at the current ~50% rate an A/B is now tractable:

- Set `V3D_VA_NO_RECYCLE 1` (`v3d_phoenix_winsys.c:185`) → monotonic bump VA, no
  reuse (the `#if` already grows the PT window to 512 pages / 2 GiB). If q3dm7's
  wedge rate drops to ~0 with no other change, stale-VA-recycle is confirmed and the
  permanent fix is a per-`va_free` GPU-side invalidation of exactly that VA range
  (or deferring VA reuse until an L2T flush + MMU TLB clear has provably retired the
  old mapping), rather than the global monotonic hack.

If instead #1 shows a VCD/VPM stall independent of VA reuse, the coord-QPU-scheduling
lever (`CTL_MISCCFG` QRMAXCNT, `v3d_phoenix_winsys.c:848`) is the next A/B — but only
after #1 rules the recycled-VA leg out, per the reasoning in (d).

## Files cited

- `tools/v3d-driver-port/v3d_phoenix_winsys.c` — submit path `:908-1140`; CPU drain
  `dsb sy` `:938`; SLCACTL front `:953`; MMU TLB flush `:960`; L2T flushes `:978-994`;
  CT0 program `:998-1000`; OOM servicer `:1001-1035`; BIN TIMEOUT dump `:1044-1082`;
  BO cacheable gate `:604-606`; VA recycle knob `:175-190`; MISCCFG write `:842-848`.
- `external/mesa/src/broadcom/common/v3d_util.c:271-313` — `v3d_tile_alloc_sizes`.
- `external/mesa/src/gallium/drivers/v3d/v3d_job.c:553-568,701-706` — tile BO alloc + qma/qms/qts.
- `external/mesa/src/gallium/drivers/v3d/v3dx_draw.c:85-113` — TILE_BINNING_MODE_CFG (4.2).
- `external/mesa/src/gallium/drivers/v3d/v3d_resource.c:141-145` — CACHEABLE disabled; SCANOUT only.
- `external/mesa/src/gallium/drivers/v3d/v3d_cl.c:63,94`, `v3d_program.c:541` — default (uncached) BOs.
- `external/linux/drivers/gpu/drm/v3d/v3d_sched.c:211-270` — `v3d_bin_job_run`.
- `external/linux/drivers/gpu/drm/v3d/v3d_gem.c:145-261` — `v3d_invalidate_caches` / `v3d_clean_caches`.
- `external/linux/drivers/gpu/drm/v3d/v3d_regs.h:241-244,292-342,480-498` — MISCCFG, CLE + PTB + FDBG regs.

---

## HW RESULTS (2026-08-22, change-#1 instrumentation landed, winsys f05b6c8)

Ran `quake3 +devmap q3dm7` × 4 boots (t1/t2 clean, t3 wedged 3×, prior q3wedge-t1 wedged 1×). The
CL@ct0ca + FDBG dump resolved **two coexisting wedge modes**:

- **MODE A — CT0 binner HALT, no MMU fault.** `ct0pc=0 bfc=0` (zero binner progress), `int_sts=0`,
  `vio_addr=vio_id=0`. The submitted binner CL is a malformed **19-byte** range whose BO (handle 7,
  single-match, valid CPU mapping) contains the stale ASCII string `"phxgl: capture FBO 1920x1080
  status=0x8cd5"` — i.e. the binner is handed a CL BO holding wrong/stale content, and halts on the
  garbage opcode. `FDBGS=0x07`.
- **MODE B — CT1/MMU fault.** Partial progress (`ct0pc>0, bfc>0`) then a REAL MMU violation
  (`vio_addr=0x765d4a0, vio_id=0xe`=client14, `FDBGS=0x47`) at a garbage VA.

**Capture-hook is NOT the differentiator:** `phxgl_capture_gl` (sdl2 glue, sdl_phoenix_glctx.c:262)
runs exactly once every boot (clean AND wedged), always `status=0x8cd5`
(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT). So the "phxgl:" string is a **marker** of BO/memory
reuse, not the cause. This reconciles with the winsys:180 note ("zeroing BOs changed content not
rate ⇒ not a memory/cache effect") for MODE B.

**Two follow-ups identified:** (1) a SEPARATE deterministic bug — the capture FBO is always
incomplete (0x8cd5, missing attachment) — worth fixing for harness reliability; (2) MODE A's
question: why is a 19-byte CL BO with unwritten/stale content handed to the binner? The wedge has a
working mitigation (reset+drop; #13 daemon reset_reinit_core recovers it), so deep root-fix stays
banked behind the finalization queue.
