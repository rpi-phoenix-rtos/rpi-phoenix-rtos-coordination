# #67 torch/alias-model mangle — producer-path analysis (2026-07-26, read-only research)

Read-only re-analysis of the residual #67 glitch (wall-torch flames + occasional monsters
render as boot-varying red/black angular spikes; guns/viewmodel fixed). Prior localization:
`docs/inprogress/2026-07-24-quake-glitch-coherency-localization.md` (fix-A = a waited-L2T-flush
before the CT0 bin kick; #67 declared RESOLVED+mitigated there, with a sub-perceptual residual on
the heaviest frame F0070).

This doc was tasked to reason from the **boot-to-boot-varying** evidence toward a PRODUCER-side
cause (CPU VBO write not landing in RAM / uninitialized-or-aliased BO / mapping-attribute
mismatch) and to state whether fix-A is the right fix. **The central finding overturns the
task's framing premise** (see §3): boot-variance does not discriminate producer from consumer,
because the mechanism fix-A targets is a *race*, and races are nondeterministic by construction.

---

## 1. Deliverable 1 — Linux L2C/L3 no-op claim + port-matches-Linux claim: **VERIFIED TRUE**

Claim: "On V3D 4.2, Linux's `v3d_invalidate_l2c` and `v3d_flush_l3` are effectively no-ops
(version-gated returns), and the Phoenix winsys L2T-flush(FLM=0)+SLCACTL sequence already
MATCHES Linux `v3d_gem.c` cache maintenance." **This is TRUE.** Citations:

- `external/linux/drivers/gpu/drm/v3d/v3d_gem.c:145-159` `v3d_flush_l3`: the **entire body** is
  under `if (v3d->ver < V3D_GEN_41)`. V3D 4.2 (`V3D_GEN_42`) is `>= V3D_GEN_41` → the function
  does nothing. No-op on 4.2. ✓
- `v3d_gem.c:164-173` `v3d_invalidate_l2c`: `if (v3d->ver >= V3D_GEN_33) return;` at line 167.
  4.2 ≥ 3.3 → returns immediately. No-op on 4.2 ("the L2 cache for uniforms and instructions on
  V3D 3.2"). ✓
- Therefore `v3d_invalidate_caches` (`v3d_gem.c:249-261`) reduces on 4.2 to exactly
  `v3d_flush_l2t` + `v3d_invalidate_slices`:
  - `v3d_flush_l2t` (176-191): `L2TCACTL = L2TFLS | SET_FIELD(FLM_FLUSH, FLM)`.
    `V3D_L2TCACTL_FLM_FLUSH == 0` (`v3d_regs.h:264`), so this is a **bare `L2TFLS`** write.
  - `v3d_invalidate_slices` (239-247): `SLCACTL = 0xf` in each of TVCCS/TDCCS/UCC/ICC = `0x0f0f0f0f`.
- Port `tools/v3d-driver-port/v3d_phoenix_winsys.c`:
  - pre-bin (937-940): `l2t_flush_wait; c0[L2TCACTL]=L2TCACTL_L2TFLS; l2t_flush_wait; c0[SLCACTL]=SLCACTL_INVAL_ALL(0x0f0f0f0f)`.
    `L2TCACTL_L2TFLS` alone = FLM field 0 = FLM_FLUSH. **Byte-identical opcodes to Linux.** ✓
  - bin→render handoff (1059-1062): same pair. ✓

**Verdict: the port's cache-maintenance *opcodes* match Linux for V3D 4.x. The fix is NOT a
missing Linux cache op.** (Confirms and re-cites the prior doc's §1.)

### 1a. The load-bearing corollary the prior doc did not wire in

`v3d_flush_l2t`'s own comment (`v3d_gem.c:179-185`) states: *"While there is a busy bit
(V3D_L2TCACTL_L2TFLS), we don't need to wait for completion before dispatching the job — L2T
accesses will be stalled until the flush has completed."*

So on the L2T path the HW **self-stalls** consumers until the flush completes. That means:

- The port's `l2t_flush_wait` spins (762-766) and fix-A's **second** `l2t_flush_wait; L2TFLS;
  l2t_flush_wait` (winsys 958-960) are **beyond what Linux does** and buy **nothing for L2T
  coherency** — the hardware already guarantees it.
- Consequently **fix-A's empirical 8/9 win comes from the LATENCY it injects, not from a cache
  primitive.** The extra MMIO round-trips (each `l2t_flush_wait` polls `L2TCACTL` over the
  peripheral bus) simply delay the CT0 kick by a few microseconds. This single fact reconciles the
  prior doc, the task's reframe, and the F0070 residual (a latency proxy is timing, never a
  guarantee — so the heaviest frame occasionally still loses the race). Anchor everything below on it.

---

## 2. Deliverable 2 — PRODUCER path for the alias VBO, Phoenix vs Linux/Mesa

The alias VBO is built once at map load: `external/quakespasm/Quake/gl_mesh.c:446`
`GLMesh_LoadVertexBuffer`, packing per-vertex `meshxyz_t = { GLubyte xyz[4]; GLbyte normal[4]; }`
(511-522, `xyz[3]=1` for 4-byte compression), uploaded via `GL_BufferDataFunc(GL_ARRAY_BUFFER,
totalvbosize, vbodata, GL_STATIC_DRAW)` (548). Indices likewise `GL_STATIC_DRAW` (492).

### 2a. How the VBO BO is allocated — **Normal-Non-Cacheable, and stable-VA for the whole map**

- Gallium: a VBO is `PIPE_BUFFER` → `should_tile = false`
  (`external/mesa/src/gallium/drivers/v3d/v3d_resource.c:878-879`), i.e. RASTER, `height 1`.
- `v3d_resource_bo_alloc` (96-155) computes `create_flags`: `V3D_CREATE_BO_SCANOUT` **only** for a
  `PIPE_BIND_RENDER_TARGET` ≥1024×768 (141-143). A VBO matches neither → **`create_flags = 0`**.
  It is never `V3D_CREATE_BO_CACHEABLE` (that path is documented DISABLED, 119-140).
- `v3d_bo_alloc_flags` (`v3d_bufmgr.c:124-193`): `cacheable = flags & V3D_CREATE_BO_CACHEABLE`
  (`v3d_bufmgr.h:73 == 1u<<0`) = false; `scanout = flags & (1u<<1)` = false.
- Winsys `ioc_create_bo` (winsys 489-622): with `flags == 0`, the else-branch (576-609) takes
  `mapflags = MAP_CONTIGUOUS | MAP_ANONYMOUS`, and because `(c->flags & 0x1u)==0` adds
  **`MAP_UNCACHED`** (584-586). So the VBO BO is uncached contiguous DRAM.
- Phoenix kernel: `MAP_UNCACHED → PGHD_NOT_CACHED` (`vm/map.c:541-542`) → the ttl3 descriptor gets
  `MAIR_IDX_NONCACHED` = `MAIR_NOR_NC` = **Normal Non-Cacheable**
  (`hal/aarch64/pmap.c:469-470`, MAIR defs 62/66). Not Device/strongly-ordered — plain Normal-NC.
- **Stable VA:** the VBO is `GL_STATIC_DRAW`, created once and kept live for the whole map, so its
  GPU VA is **not** freed/recycled per frame (contrast the per-frame CL/tile-state BOs). Its PTEs
  are written once (winsys 606-609) and never rewritten.

Linux/Mesa contrast: on the real kernel these BOs come from `dma_alloc`-backed shmem that is
coherent-by-construction (or handled by `v3d_invalidate_caches` on the GPU side); the driver never
hand-rolls page attributes. The Phoenix port hand-maps Normal-NC + a flat MMU PT — functionally
equivalent for a read-only-by-GPU static buffer.

### 2b. CPU write path — is the data guaranteed in RAM before the GPU fetches it? **Yes.**

- `glBufferData` → `u_default_buffer_subdata` → `v3d_resource_transfer_map`. For a **non-tiled**
  buffer the map returns the **direct** uncached BO pointer (`v3d_resource.c:410-418`,
  `buf = v3d_bo_map(rsc->bo)`), and `v3d_bo_map` returns the winsys uncached mapping unchanged
  (`v3d_bufmgr.c:515-556`, 534-537). The `memcpy` therefore writes **straight to Normal-NC DRAM**.
- `transfer_unmap` (`v3d_resource.c:157-190`): the tiled store path (170-183) runs **only** when
  `trans->map` was malloc'd, which happens **only for tiled** resources (390). A non-tiled buffer's
  `trans->map` is NULL, so there is no second copy — the direct write is the write. No cached
  staging buffer is involved.
- Drain to RAM: `ioc_submit_cl` issues **`dsb sy`** (winsys 912) at the top of *every* submit. For
  a static VBO written at load, dozens of `dsb sy` execute before the alias model is ever drawn.
  `dsb sy` is a completion barrier that waits for the Normal-NC store to reach the point of
  coherency the external GPU master observes (the comment at 890-911 explains exactly this and why
  it must be `dsb`, not `dmb`). **The VBO data is in DRAM long before the binner fetches it.**

### 2c. Could the BO be UNINITIALIZED or ALIASED (stale dirty CPU line clobbering DRAM)? **No.**

This was the strongest producer candidate (a freed page's dirty lines in the kernel's *cached*
linear alias evicting asynchronously into the uncached BO → boot-varying DRAM). It is **refuted**
on this kernel:

- Winsys zeroes every non-scanout BO at create: `memset(cpu, 0, pages*_PAGE_SIZE)` (winsys 600),
  precisely because `mmap(MAP_CONTIGUOUS)` returns non-zeroed DRAM (593-599). Zeros are written
  Normal-NC → straight to DRAM.
- The kernel closes the mismatched-alias hole itself: `_pmap_cacheOpBeforeChange`
  (`hal/aarch64/pmap.c:256-285`) **cleans the data cache** whenever a page transitions
  cached→non-cached or is unmapped (condition 270-272; `hal_cpuFlushDataCache` on the live VA at
  276, or via the scratch page at 281-282). So a page freed by a prior *cached* owner has its dirty
  lines flushed before/at reuse; no dirty line survives to evict into the VBO later.
- (One caveat worth noting, not a live bug on A72: the scratch-page branch's comment at
  `pmap.c:280` flags that it assumes a PIPT data cache. The Cortex-A72 data caches **are** PIPT, so
  the by-PA maintenance is correct here.)

So there is no cached alias able to corrupt the VBO's DRAM, and the source bytes are byte-identical
across boots (prior doc, all 41 models). **Every producer link — allocation attribute, write path,
drain, initialization, alias coherency — is deterministic. The DRAM at the VBO's PA is byte-
identical across boots.**

### 2d. MMU/mapping attributes of the GPU-side mapping

The VBO's GPU mapping is a flat-PT PTE with `PTE_W|PTE_V` (winsys 608), and the per-submit
`mmu_flush_tlb` (winsys 919; 773-780) clears the MMU PTE cache + TLB before each job, matching
`v3d_mmu_flush_all`. The GPU therefore always translates the VBO VA correctly. No attribute
mismatch on the GPU side.

---

## 3. Deliverable 3 — single most-likely root cause (reasoned from §1+§2)

**The task's discriminating premise is false, and that is the key result.** The reframe assumed "a
consumer-side cache-invalidate race would yield STALE-BUT-CONSISTENT wrong data, so boot-varying
garbage ⇒ producer." But the consumer mechanism at issue — the coordinate shader's vertex fetch
beating a **fire-and-forget `SLCACTL` slice-cache invalidate that has no completion primitive on
V3D 4.2** — is a **race**, and a race is nondeterministic by construction. Its outcome depends on
sub-microsecond timing (V3D clock/thermal phase at cold power-on, DRAM refresh phase, exact
slice-cache residency) that varies boot-to-boot. **Boot-variance is fully consistent with the
consumer race and therefore does NOT point to the producer.**

Combined with §2 (the producer path is deterministic end-to-end and the VBO DRAM is byte-identical
across boots), the evidence converges on **one** root cause:

> **The GPU vertex slice caches (TVCCS/TDCCS) are invalidated per submit by a `SLCACTL` write that
> is fire-and-forget on V3D 4.2 (no busy/completion bit). The binner's coordinate shader can begin
> fetching alias-model vertices before that invalidate has settled, so it reads a stale/partially-
> invalidated slice-cache line instead of the (correct) DRAM. Whether it wins or loses the race is
> timing-dependent → the model collapses to a fan/wedge on some cold boots and not others. The
> per-frame churned CL/tile-state/uniform BOs (recycled VAs) alias the same slice-cache sets,
> supplying the garbage the losing fetch reads.**

This is the prior doc's mechanism, now positively re-established (not merely assumed) by refuting
every producer alternative. Small alias models (torches) are affected exactly like larger ones —
the fetch is the same coordinate-shader path regardless of vertex count; "small" only means the
whole model fits the racing window, so a single bad fetch collapses all of it.

### The one gap the prior work never closed — hand this to HW as THE decider

The prior "data byte-identical" claim was verified on the **source arrays**, never on the **DRAM at
the VBO's PA at draw time**. That is the exact producer/consumer discriminator, and the machinery to
close it already exists in the winsys:

> With `V3D_BIN_CRC`-style instrumentation, CRC32 the **uncached** bytes of the alias VBO
> (`gpuva_to_cpu(vbo_gpuva)`, winsys 651-659; `crc32_le`, 670-680) at the CT0 kick for a torch/door
> frame, across ≥5 fresh cold boots. **Identical CRCs while the geometry still glitches = the DRAM
> is correct and the producer is dead → the SLCACTL race is confirmed.** (If — contrary to §2 — the
> CRCs differ across boots, the producer IS live and the patch in §4 becomes the fix.)

Do not assert a producer root cause the static evidence contradicts; run this CRC and let it decide.

---

## 4. Deliverable 4 — one minimal candidate patch + is fix-A relevant / should it be replaced

### Candidate patch (producer hardening — and the producer discriminator)

The task asks for the change that makes the VBO data *deterministically* reach the GPU. The minimal,
single-site version, in the **winsys**, `ioc_create_bo`, immediately after the existing zeroing
`memset` (winsys 600), before the PTEs are published:

```c
/* Producer hardening: after zeroing an uncached BO, clean+invalidate the CPU cache for its
 * physical range so no stale dirty line from a prior (cached) owner of these pages can later
 * evict into this Normal-NC buffer that the GPU reads directly from DRAM. dc civac works by PA
 * (A72 D-cache is PIPT) even though `cpu` is a Normal-NC VA; needs SCTLR_EL1.UCI=1 (kernel sets
 * it — same op already used on the readback path, v3d_resource.c:295-306). */
for (uintptr_t a = (uintptr_t)cpu, e = a + pages*_PAGE_SIZE; a < e; a += 64)
        __asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
__asm__ volatile("dsb sy" ::: "memory");
```

(Equivalent alternative placement: Mesa `v3d_resource_transfer_unmap`, after a non-tiled
`PIPE_MAP_WRITE` buffer store — hardens the upload rather than the allocation. The winsys site is
preferred: one place, covers *all* uncached BOs (VBOs, CLs, tile-state), and mirrors the existing
`memset` rationale.)

**Honest assessment of this patch: it is predicted to be a NO-OP fix.** Per §2c the kernel
(`_pmap_cacheOpBeforeChange`, pmap.c:256-285) already flushes the cached alias on cacheability
transition/unmap, so there is no stale dirty line for `dc civac` to catch. **Its real value is as
the producer discriminator** — build it, and if #67 torch frames become cross-boot-deterministic
(≥5 fresh boots, distinct-per-frame scorer), the producer WAS live after all and this is the fix.
If they still glitch (predicted), the producer branch is conclusively dead and the SLCACTL race
(§3) is confirmed — pursue the deferred slice-invalidate-completion primitive, not another
producer patch. Pair it with the §3 VBO-DRAM CRC in the same build so one HW run settles it.

### Is fix-A relevant to torches, and should the fix replace it?

- **Relevant: YES.** fix-A (winsys 941-960) is a **global, non-size-gated** barrier executed before
  *every* CT0 kick, so it applies to a wall-torch's tiny VBO exactly as to a large monster. There is
  no small-model bypass. (This is consistent with the prior doc's report that fix-A made 8/9 model
  frames deterministic — torches included.)
- **Replace with the §4 producer patch? NO — additive at most.** fix-A works only as a **latency
  proxy** (§1a: the waited L2T flush is redundant for coherency; it just spends microseconds so the
  fire-and-forget SLCACTL settles). A producer patch addresses a different (and, per §2, non-
  existent) failure mode, so it cannot substitute for the timing fix. There is **no known
  SLCACTL-completion register on V3D 4.2**, so a *correct* replacement for fix-A (a guaranteed
  slice-invalidate barrier) does not exist to write today — that is the genuine deferred deep item,
  and it is what the F0070 residual is waiting on.
- **If fix-A's fps cost matters:** the cheapest principled trim is to drop fix-A's *first*
  `l2t_flush_wait` (redundant per §1a — the HW self-stalls) and keep only the settle spins;
  but any such tuning is still a timing hack and must be re-validated at ≥5 fresh boots with the
  distinct-per-frame scorer (the prior doc's METHOD LESSON: N≤5 can look clean by luck).

---

## 5. Deliverable 5 — how the orchestrator should test on HW

Build **one** instrumented winsys/libv3d and run it against the existing DET cross-boot harness so a
single HW session settles the producer question and validates any patch:

1. **Build:** rebuild `libv3d-phoenix.a` via `tools/v3d-driver-port/build-v3d-phoenix.py`
   (incremental) with:
   - the §4 `dc civac`+`dsb` producer-hardening in `ioc_create_bo`, **and**
   - a `V3D_BIN_CRC`-style CRC of the alias VBO's uncached bytes at the CT0 kick (reuse
     `gpuva_to_cpu` + `crc32_le`, winsys 651-680), logged per torch/door frame.
   Then rebuild the DET Quake (`tools/quakespasm-port/build-quakespasm-det.py`;
   `SCR_DetTick` in `external/quakespasm-det/Quake/gl_screen.c`) against the fresh libv3d.
2. **Run ≥5 FRESH cold boots** (not a single pair — prior METHOD LESSON) with the fixed-timestep
   deterministic demo (`host_framerate 0.05`, `r_dynamic 0`, `r_drawentities 1`) capturing the
   torch/door frames (the F0050/F0060/F0090/F0100 class, which diverged 9–28% baseline).
3. **What to look for:**
   - **VBO CRC identical across all boots** (expected per §2): producer is dead — the DRAM the GPU
     reads is correct; do NOT chase producer fixes further.
   - **Torch/door crops cross-boot-deterministic AND correct** (the scene renders the same, correct
     geometry every boot): the build fixes #67. If this happens *with* the producer patch while the
     CRC was identical, that would be surprising (§2c predicts the patch is a no-op) — re-check for a
     confound before crediting the producer patch.
   - **CRC identical but crops still vary** (predicted): the SLCACTL race is confirmed; keep fix-A as
     the latency mitigation and open the deferred item = a guaranteed V3D-4.2 slice-invalidate
     completion primitive (none currently known). The residual heavy frame (F0070) lives here too.
   - Watch for `RENDER TIMEOUT` / `BIN TIMEOUT` lines (winsys 1010/1100) and `render_recoveries`
     (830) — the producer `dsb`/civac must not perturb the wedge-mitigation timing.

---

## Status

- [x] **Deliverable 1** — Linux L2C/L3 no-op on 4.2 + port matches Linux L2TFLS(FLM=0)+SLCACTL:
      **TRUE**, cited (`v3d_gem.c:145-261`, `v3d_regs.h:264`, winsys 937-940/1059-1062).
- [x] **Deliverable 1a** — Linux says L2T flush needs no completion wait (`v3d_gem.c:179-185`) ⇒
      fix-A's win is LATENCY, not a cache primitive. Reconciles prior doc + reframe + F0070 residual.
- [x] **Deliverable 2** — producer path traced end-to-end: VBO is Normal-NC, stable-VA, direct
      uncached write, `dsb sy`-drained, memset-initialized, and the kernel flushes the cached alias
      on cacheability change → **producer path is deterministic; VBO DRAM byte-identical across boots.**
- [x] **Deliverable 3** — root cause = the **fire-and-forget SLCACTL slice-invalidate has no
      completion primitive on V3D 4.2; the coordinate-shader vertex fetch races it** (boot-varying
      *because* it is a race). The reframe's producer premise is refuted; the open gap = a DRAM-at-PA
      CRC nobody has run.
- [x] **Deliverable 4** — candidate patch = winsys `dc civac`+`dsb` after the alloc `memset`;
      honestly a **predicted no-op fix / producer discriminator**. fix-A **is** relevant to torches
      (global barrier) and the producer patch is **additive, not a replacement**; a true replacement
      needs a slice-invalidate completion primitive that does not exist on 4.2 (deferred).
- [x] **Deliverable 5** — one instrumented build (producer patch + VBO CRC) against the DET harness,
      ≥5 fresh boots, with a decision tree that settles producer-vs-race in a single HW session.
