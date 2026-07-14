# V3D / Mesa-GL full-scope code review (2026-07-15, overnight)

**Goal (user):** deep review of everything that differentiates our v3d/mesa-GL port from upstream; fix
as many GPU issues as possible. Symptom classes: (1) dynamic-model flicker (r_dynamic-gated); (2)
occasional model geometry artifacts (misshapen torches/weapons); (3) occasional render wedging/slowdowns.
User hypothesis: the build-system migration rebased mesa onto a different upstream commit and something
was lost/changed. Also wants a rebase recommendation (forward vs stable) — decide, don't execute.

## Method / scope (established)

- Our mesa port = **9 commits** on upstream base `489aa1808f2` (`26.1-branchpoint-2793`), branch
  `phoenix-v3d-port`. Full upstream clone at `external/mesa` (origin = freedesktop). Local upstream
  **linux** clone at `external/linux` incl. the v3d **kernel** driver (`drivers/gpu/drm/v3d/`) — the
  contract our hand-written winsys must implement.
- The GL-path mesa diff vs upstream is **small**: `v3d_bufmgr.c/.h`, `v3d_resource.c`, `v3dx_draw.c`
  (comment only), `v3dx_state.c` (comment only), `v3d_context.c` (log removal), `st_atom_framebuffer.c`
  (Y_0_TOP flip). The heavy hand-written logic is the winsys `tools/v3d-driver-port/v3d_phoenix_winsys.c`
  (79 KB — replaces the kernel driver's job submission) + `v3d_phoenix_power.c` + stubs.

## GATING RESULTS

1. **User's "migration lost something" hypothesis — RULED OUT for the mesa GL diff.** Added-code sets of
   the old (Jun 27) port patch vs the current `git diff BASE..HEAD` are **identical across all 7 GL
   files** (patchcmp.py). Nothing dropped/altered. If the migration matters it is via **upstream base
   drift** (upstream v3d changed under our patch), not a lost hunk. The winsys is coord-repo, untouched
   by the mesa migration.
2. **Build consumes the working tree** (build-gl-phoenix.py compiles `external/mesa` in place via
   compile_commands; no re-clone/re-apply). So working-tree edits ARE built — BUT: (a) force rebuild by
   deleting the archive (`tools/.gpu-libs/libGL-phoenix.a` / `libv3d-phoenix.a`; archive_fresh only
   watches `tools/`, not `external/mesa`); (b) regenerate `tools/v3d-driver-port/patches/mesa-v3d-phoenix.patch`
   for persistence across a fresh clone.

## Multi-job-flush flicker theory — status: WEAKENED

`R_UploadLightmaps()` runs at the TOP of `R_DrawTextureChains` (r_world.c:1134), before world draws, so
the primary world-lightmap upload does not split the frame. (Brush-model lightmap updates in later
`R_DrawTextureChains` calls could still WAR-flush mid-frame, but alias models — the flicker locus — don't
use the lightmap texture.) Not treating multi-job as the confirmed mechanism; the review below looks for
the real defect(s).

## FINDINGS

### Contract reference (upstream linux v3d kernel driver, V3D 4.2)
Per-job cache maintenance the kernel does (v3d_gem.c `v3d_invalidate_caches`), run at BOTH bin-start AND
render-start: **L2T flush (L2TCACTL, FLM=FLUSH=clean+invalidate) FIRST, then SLCACTL slice-invalidate
(TVCCS|TDCCS|UCC|ICC)** — "outside-in" order is deliberate. One-time `L2TFLSTA=0/L2TFLEND=~0` in
init_core (+after reset). Post-render write-back only on `_FLUSH_CACHE`: `v3d_clean_caches` = **TMUWCF
(TMU write-combiner flush, wait) THEN L2T CLEAN (wait)**. MMU flush only after PTE insert + in the OOM
worker before BPOA/BPOS. Kick order: BPOS=0 → invalidate → CT0QMA/QMS/QTS → CT0QBA → **CT0QEA (kick)** →
FLDONE; render: invalidate → CT1QBA → **CT1QEA** → FRDONE. Timeout only resets if CTnCA AND CTnRA both
unchanged (progress check).

### F1 — winsys bin-start cache order reversed vs contract (LOW, likely benign)
`v3d_phoenix_winsys.c:813-816`: does **SLCACTL (813) BEFORE L2T flush (815)** at bin-start; the linux
contract is L2T-flush-then-SLCACTL ("outside-in"). At the bin→render handoff (916-918) our order is
correct (L2T then SLCACTL). Likely benign because the GPU is idle between the two invalidates (bin not
kicked until 822, so read-only slice caches can't refill stale), but it's an inconsistent deviation —
make bin-start match the contract order for safety/clarity.

### F2 — post-render clean omits TMUWCF (TMU write-combiner flush) (LOW for GLQuake)
`v3d_phoenix_winsys.c:1004-1005`: post-render does only L2T CLEAN (FLM_CLEAN); the linux `v3d_clean_caches`
does **TMUWCF flush THEN L2T CLEAN**. Matters only if shaders issue TMU/image writes (GLQuake does not;
vkQuake/compute could). Add the TMUWCF step to match the contract before any image-store workload.

### F3 — MMU flush on every submit vs after-PTE-insert (perf, not correctness)
`v3d_phoenix_winsys.c:800` flushes the whole MMU every submit (deferred from ioc_create_bo, which writes
PTEs without flushing). Linux flushes right after PTE insert + in the OOM worker. Functionally correct
(a BO used in submit N is covered by N's flush) but costs a full MMU+TLB flush per submit — doubled on
any multi-job frame. Candidate: flush in ioc_create_bo (once per BO) instead of per-submit.

### >>> APPLIED (winsys, pending netboot validation + user flicker eyeball)
- **F4 LANDED** (winsys:804): `__sync_synchronize()` → `dsb sy`. Best flicker-fix candidate — the code
  now matches its own comment ("one dsb per submit") and the described completion requirement. Rationale
  in F4 below. Also upgraded the TFU CPU-tile barrier (winsys:1142) to `dsb sy` for consistency.
- **F1 LANDED** (winsys:813-817): bin-start cache maintenance reordered to L2T-flush-then-SLCACTL
  (contract "outside-in" order, matching our own bin→render handoff). Behaviorally ~no-op (core idle)
  but contract-correct + consistent.
- F2/F3/F5: documented, NOT applied (F2 no-op for GLQuake; F3 perf-only; F5 mesa comment needs a mesa
  rebuild — not worth it for a comment). See below.

### F4 — CPU→device store barrier is `__sync_synchronize` (dmb), not dsb (LOW-MED → now the lead fix)
`v3d_phoenix_winsys.c:793`: drains CPU stores to uncached BOs before the GPU kick via
`__sync_synchronize()` (aarch64 `dmb ish` = ordering, not completion). For a non-coherent external DMA
master (V3D), a `dsb` may be needed to guarantee the uncached write reached the point of coherency
before the MMIO kick. World lightmap (same path) renders fine, so not clearly implicated — but worth
upgrading to `dsb sy`/`dsb ish` as a cheap correctness hardening.

### F5 — winding/Y-flip reasoning is self-contradictory (doc bug; fragility)
`st_atom_framebuffer.c` comment says winding is "compensated by the platform's `glFrontFace(GL_CCW)`",
but the platform (`pl_phoenix_vid.c:392`) actually sets **`glFrontFace(GL_CW)`** (and says GL_CCW causes
gray-world). The Y_0_TOP viewport Y-negate should invert winding yet CW (not CCW) renders correctly —
the interaction is empirically tuned and under-understood. World renders fine so it's not the primary
model defect, but this fragility is a candidate for subtle model-triangle culling artifacts; fix the
stale comment and add a note that the winding contract here is not mechanistically nailed down.

### F4 unified-mechanism note (why it may address all three symptoms)
Everything the GPU reads per-frame that the CPU writes into an **uncached** BO is exposed to the same
`dmb`-vs-`dsb` completion race: (1) per-draw `LightColor`/matrix **uniforms** → wrong lighting = model
**flicker**; (2) the per-draw **shader-state + vertex-attribute records** (written into the CL each
draw, carrying the VBO base+offset the VPM fetches) → a stale record = wrong vertex fetch = **misshapen
geometry** (torches/weapons); (3) a corrupted CL/tile-list read → **wedge**. A single intermittent
completion race plausibly produces all three at low rate, worst under load. F4 (`dmb`→`dsb sy`) closes
it. Unproven until HW eyeball, but it's the one fix that unifies the symptom set and is grounded in an
actual code/comment contradiction, not a mechanism guess.

## REBASE RECOMMENDATION — do NOT rebase to fix these symptoms

**Recommendation: stay on the current base; do not rebase (forward or to stable) as a fix.**

Reasoning:
- Our base is already recent: `26.1-branchpoint-2793` = mesa **main**, 2793 commits past where the 26.1
  stable branch was cut. Not stale.
- The migration lost **nothing** — the GL patch hunks are byte-identical old-vs-new (gating check 1).
- Every defect this review surfaced (F1–F5) is in our **hand-written winsys / port glue**, NOT in the
  upstream mesa v3d gallium code. The upstream v3d driver code we build is stock and correct. Rebasing
  the base cannot fix a winsys coherency/timing bug; it only risks N new regressions on a port that
  currently boots and renders.
- Our port is 9 small, clean commits on top of upstream — trivially replayable onto a different base
  later if desired.

**If a base change is wanted for maintainability (a separate, deliberate, awake decision — not a fix):**
prefer pinning to a mesa **stable release TAG** (e.g. the head of the `26.1` stable branch) over tracking
`main`. A stable tag gives reproducible builds and only-bugfix churn; `main` is a moving dev target. Do
it as: create a branch off the stable tag, cherry-pick/replay our 9 port commits, rebuild `--scope core`
+ GPU, and re-run the GLQuake + X11 HW validation before adopting. This is a maintenance migration to
schedule when the port is otherwise stable — NOT something to do speculatively overnight, and NOT
expected to change the flicker/artifact/wedge behavior (those are port-side).
