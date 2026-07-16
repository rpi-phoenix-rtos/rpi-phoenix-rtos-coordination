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

### Reviewed CLEAN (positive findings — no defect)
- `ioc_create_bo` (winsys:438): VA-collision detect, BO zeroing (avoids garbage tile-lists), per-page
  `va2pa`, scanout double/triple-buffer aliasing — sound.
- `apply_core_regs`/`winsys_init`/`reset_reinit_core` (winsys:678/250/743): faithfully implement the
  linux `v3d_init_core`+`v3d_mmu_set_page_table`+reset contract — MMU fault config (ENABLE+ABORT+INT),
  illegal-access scratch page, MMUC enable, L2T whole-cache flush range, GFXH-1383 HUB_AXICFG burst cap,
  MISCCFG (QRMAXCNT+OVRTMUOUT), GMP idle-on-reset (v3d_idle_axi mirror). No missing init register.
- Scanout double-buffer (winsys:340): synchronous render to off-screen buffer → page-flip after
  completion → live buffer never GPU-written; no tearing path.
- mesa GL diff (v3d_bufmgr/resource/context/draw/state/framebuffer): small, correct, well-gated
  (scanout/cacheable flags + render-to-scanout + Y_0_TOP all gated on full-screen RTs).
- Multi-job color+depth preservation: our synchronous winsys + post-render L2T CLEAN + next-submit L2T
  FLUSH (with the correct whole-cache range) satisfies the upstream store-before-load contract.

### VALIDATION (F1+F4, netboot 20260715-003609)
Boots to psh; GLQuake renders **~38–42 fps**; wedge rate = **1** (within the 0–2 baseline); no fault/
crash. **No regression.** The aggregate flicker metric is VFX-confounded so it cannot confirm the flicker
is fixed — that needs a HW eyeball. SD image built + flashed for the user to test. The wedge persisting
(1×) means the dsb does not by itself cure the depth-drain-stall wedge (a distinct HW-marginal issue);
whether it reduces flicker/misshapen is the open eyeball question.

## FLICKER ROOT CAUSE FOUND (2026-07-15) — non-deterministic single-buffer → render-to-scanout tearing

**The dynamic-model flicker is single-buffer render-to-scanout TILE TEARING, gated by a
non-deterministic per-boot framebuffer-buffering mode.** Strong evidence, unifies every prior
observation, and explains why every earlier fix (EZ, barrier, full-upload, dsb) failed.

### Evidence
- The scanout buffering mode is read from a VideoCore mailbox grant (`GET_VIRTUAL_WH`,
  `v3d_phoenix_power.c:272`); plo requests a ≥2× virtual fb, the firmware grants what it can. Across
  archived UART `scanout init` lines the grant is **non-deterministic per boot of the same build**:
  `virt_h=3240` (TRIPLE-buffer), `2560`/`2160` (double), or **`0` (SINGLE-buffer)**.
- **Same dsbfix build, two boots, opposite result:** netboot `20260715-003609` came up `virt_h=3240`
  TRIPLE-buffer → my validation saw clean 38–42 fps, no tearing. The user's SD boot `20260715-074348`
  came up `virt_h=0` SINGLE-buffer → flicker. That is the whole mystery: the flicker tracks the boot's
  buffering mode, not the code.
- In SINGLE-buffer mode the winsys STILL scanout-backs the RT to buf0 (the live displayed fb)
  (`ioc_create_bo` else-branch, winsys:498-500) → the GPU paints tiles into the buffer the display is
  concurrently scanning → **tile tearing**. `gl_flashblend 1` (slow alpha spheres) made it blatant
  ("blocks rendered independently at different speed") — plus a 382-event RENDER-TIMEOUT wedge storm
  (separate flashblend bug).

### Why this unifies everything
- **r_dynamic-gated:** r_dynamic ON = slower frames (per-frame lightmap uploads + more draws) → the GPU
  paint lags the scanout more → tearing visible. r_dynamic 0 = fast frames complete before the scanout
  catches up → no visible tear = "no flicker". (r_dynamic gates tear *visibility* via frame time.)
- **"monsters you aim at in intense action":** entities are drawn LAST (after the world), so they are
  the content most likely caught mid-paint by the concurrent scanout; combat = slowest frames.
- **`r_drawentities 0` killed it:** removed the last-drawn, most-tear-prone content.
- **dsb / full-upload / EZ did nothing:** the data the GPU reads is correct — it's a display/render
  concurrency (present-path) problem, not a data/coherency/shader problem.
- **Intermittent across all my tests:** the buffering mode varied per boot.

### FIX (attended — plo/present-path, needs HW validation, and validation is non-deterministic)
Two options, not landed tonight (per scope + the validation can't be trusted on a non-deterministic
netboot that may come up multi-buffer):
1. **Winsys robustness (preferred, self-contained):** when the grant is < 2× physical (single-buffer),
   do NOT scanout-back the RT — leave it a normal DRAM BO so the present path does the atomic
   blit-resolve (render off-screen → GPU-blit a COMPLETE frame to the fb). Eliminates tearing regardless
   of the firmware grant. Change is small (winsys `ioc_create_bo` single-buffer branch) BUT requires the
   blit-resolve present path to be verified working, on a boot that actually comes up single-buffer.
2. **plo/firmware:** make the virtual-fb grant deterministic (always ≥2×) so double/triple-buffer always
   engages. Touches boot/display allocation.

### User confirmation test (cheap, decisive)
Boot the card, check the UART `scanout init` line: **`virt_h=0` (single) should flicker; `virt_h=3240`
(triple) should be clean** — same card. Reboot until you see both modes and confirm the correlation.

## 26.2 REBASE ATTEMPT (2026-07-16) — builds+links cleanly but WEDGES at runtime; reverted

Per user request, rebased our 9 v3d port commits from `26.1-branchpoint+2793` onto the **mesa 26.2**
stable branch (`mesa-26.2.0-rc1`, 2298 commits ahead; clean forward rebase, our base is an ancestor).

- **Rebase: clean, 0 conflicts.** Port GL diff byte-identical after replay. Preserved on branch
  `external/mesa: phoenix-v3d-port-26.2` (1584b1a); backup of pre-rebase on `phoenix-v3d-port-pre-26.2`
  (b234aa4).
- **Build: needed only 2 source-list additions** for 26.2 refactors — `src/broadcom/common/v3d_submit_util.c`
  (multisync submit helpers: `v3d_multisync_init/free`, `v3d_submit_ext_set`) and
  `src/compiler/nir/nir_convert_address_format.c` (split out of `nir_lower_explicit_io.c`). Then libGL
  (325 objs), libv3d (410 objs), libquakespasm all link with **0 undefined symbols**. (Also had to
  recreate `/tmp/mesa-pyenv` — the meson venv was `/tmp`-cleaned; not a 26.2 issue.)
- **Runtime: REGRESSION — constant render wedge.** GLQuake on 26.2 runs at **~5.5 fps with 337
  RENDER-TIMEOUT wedge events** (vs 38–42 fps pre-rebase). Signature: all CT1 (render) timeouts, 0 bin;
  `ct1ca` lands OUTSIDE the RCL range (`wedge_op=0x70`), i.e. CT1 **branches out of its RCL into a wrong
  address**. This is a 26.2 **RCL / tile-list / submit-format change** that our hand-written winsys
  (`v3d_phoenix_winsys.c`, which reimplements the drm/v3d kernel job submission) no longer matches —
  the "upstream drift breaks the winsys" risk this review flagged, now concrete.

**Decision: NOT shipped.** Reverted mesa to the pre-rebase base; the SD card for the user carries the
working pre-rebase build + the force-buffer flicker fix. The 26.2 rebase needs winsys work to match
26.2's RCL/tile-list layout (audit `V3DX(emit_rcl)` tile-list base + branch + the multisync submit args
vs our `ioc_submit_cl` CT1 setup) — a deliberate, HW-validated effort, not an overnight change. The
rebased branch is preserved for that.

**This supersedes the earlier "don't rebase" recommendation with data:** the rebase is mechanically
trivial but the winsys is coupled to the mesa RCL format, so a base move requires winsys updates. Pin a
stable tag only together with that winsys work.

## 26.2 REBASE — COMPLETED + WORKING (2026-07-16): the wedge was Early-Z

Follow-up to the wedge above: the 26.2 constant render-wedge is **Early-Z triggered**, not compiler/CL
drift. Discriminator (force `V3D_EZ_DISABLED` in `v3d_update_job_ez`, rebuild 26.2, netboot):
- EZ-on 26.2: 337 render timeouts, ~5.5 fps.
- **EZ-off 26.2: 0 wedges, ~38 fps, triple-buffer — HW-verified (netboot 20260716-165718).**
- **Frames visually correct** (read the HDMI grabs): green mossy world + wall torch + demon-face relief,
  weapon viewmodel, a blood-spattered ogre with correct geometry, and a grenade explosion — all render
  right, no garbage/misshapen/wedge artifacts.

Root: this port's V3D 4.2 render pipeline hits the HW-marginal depth/fragment drain stall (fdbgs=EZTEST)
with EZ enabled; 26.2's codegen makes it fire EVERY frame (pre-26.2 it was occasional, ~0–2/session).
Fix = keep EZ off on 26.2 (mesa commit `671c4f08`, `v3dx_draw.c`). EZ early-rejects overdraw, but at
1080p GLQuake isn't overdraw-bound so there's no measurable fps cost. TODO(v3d-26.2-ez): root-cause why
26.2 makes the stall constant, to re-enable EZ (perf lever) later.

The 26.2 rebase is on `external/mesa` branch **`phoenix-v3d-port-26.2`** (`671c4f08`: rebased port + the
2 source-file additions + EZ-off). Coord `v3d-core-sources.txt` carries the 2 new files. **Reproducibility
follow-up (deliberate adoption, user's call — "local commits only"):** to make 26.2 the default, push
`phoenix-v3d-port-26.2` to the mesa fork base, update the `bootstrap-linux-host.sh:124` mesa pin
(`b234aa4` → `671c4f08`), and regen `patches/mesa-v3d-phoenix.patch`. The LOCAL tree builds + the SD card
below use `671c4f08` directly, so the user's test needs none of that.

## FLICKER FIX SHIPPED (pre-rebase → now 26.2): force-multi-buffer retry
The `v3d_phoenix_fb_virtual_height` GET retry (commit; winsys/power) recovers multi-buffering from the
shared-mailbox response race that was demoting SD boots to single-buffer → render-to-scanout tearing.
Netboot-validated no-regression (still triple-buffer, 38–42 fps). SD efficacy is the user's return test.

## REBASE RECOMMENDATION — superseded above (rebase is winsys-coupled)

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

## FLICKER (entity-blink) — reset + current wall (2026-07-16 late)

User's precise description on the 26.2+EZ-off SD card: rendering FAST, but **all dynamic/animated models
(monsters, buttons, moving gates, rotating item pickups) blink on/off SYNCHRONOUSLY at a regular
frequency; the world (walls/sky/water) is perfectly stable; worse with >4 visible dynamic models.** "As
if the phase that draws these objects is choking." This REFUTES the tearing hypothesis (tearing is
spatial seams; this is temporal, whole-object, all-entities-together).

Established:
- The entity-drawing phase intermittently produces nothing — all entities drop together for a frame.
- The frame splits into ~2 GPU render jobs/frame (CLEAN-RCL count vs QSFPS): world job + entity job.
- SD comes up SINGLE-buffer: `virt_h=0`, "GET_VIRTUAL_WH still failing after 8 retries" — the mailbox
  retry did NOT beat SD's shared-mailbox response race. So SD is stuck single-buffer.

Wall: could NOT reproduce the all-entity-blink on netboot (multi-buffer, virt_h=3240). Two 90s captures
(ez262, mm4) reaching demo2/demo3: every flagged blink is VFX (explosions, projectiles, lighting
flashes), NOT an entity-drop — BUT the netboot demos in these captures don't clearly show a >4-static-
models scene, so it's inconclusive whether multi-buffer eliminates the blink. And SD (the reproducing
case) can't be self-tested (needs the card in the Pi).

Leading (unconfirmed) hypothesis: single-buffer-specific — SD's single-buffer render-to-scanout + the
multi-job split makes the late entity job's output intermittently not-shown. Fix would be reliable
multi-buffering on SD (the retry fails; robust fix = plo passes the boot-time granted virtual height
forward so the winsys doesn't depend on the flaky runtime GET). Needs user confirmation of the
single-buffer↔flicker correlation before building the (plo/graphmode/winsys) fix.
