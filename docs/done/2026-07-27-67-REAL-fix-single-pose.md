# #67 alias-model glitch — the REAL fix (single-pose), 2026-07-27

## Honest correction of the record
The prior #67 "fixes" (fix-A waited-L2T barrier `214be9a`; SLCACTL-early ordering
`457a650`) and their "RESOLVED" claims were **false positives**. They were validated
by a **cross-boot determinism** harness (0.0% cross-boot = "fixed") and a DET binary
run with forced fixed-timestep + `r_dynamic 0` — neither of which reflects
correctness or the user's real run conditions. A *consistently-broken* render scores
0.0% cross-boot and falsely passes. Real-game HDMI video (2026-07-27) showed the
weapon pickups still rendering as mangled black-triangle spikes — the glitch was
never actually fixed, only measured wrong. The cross-boot cache/timing/SLCACTL axis
was aimed at the wrong thing; the surviving glitch is a **deterministic correctness
bug**, not a race.

## Root cause (deterministic)
`GL_DrawAliasFrame_GLSL` (external/quakespasm/Quake/r_alias.c) binds five vertex
attributes incl. two 8-bit position attributes Pose1Vert + Pose2Vert. The V3D
mishandles the alias draw when **blend==0** (single effective pose) while BOTH byte
position attributes are enabled/fetched → a subset of vertices collapse into black
degenerate-triangle spikes. blend==0 happens for exactly the broken classes:
- weapon PICKUPS (numposes==1, e.g. grenade launcher) — always,
- torch flames / `r_nolerp_list` models — always,
- the idle/paused viewmodel & monsters — intermittently ("sometimes broken").
The animating path (blend!=0: firing viewmodel, walking monsters) renders correctly.
At blend==0 the shader's `mix(Pose1,Pose2,0.0)==Pose1`, so Pose2 is mathematically
unused — but leaving it bound triggers the collapse. (The old de-alias workaround —
binding Pose2 to a duplicated block for numposes==1 — was a red herring; it doesn't
prevent the mishandling.)

## Fix (external/quakespasm `4ef0a42`)
In `GL_DrawAliasFrame_GLSL`, when `blend==0` do NOT enable/bind the Pose2 vertex+
normal attributes at all. A disabled attribute supplies the generic default which
`mix()`es away at blend==0, so position/normal come solely from Pose1. Only bind
Pose2 for the real 2-pose lerp (blend!=0), which was already correct. ~20 lines,
app-side, no winsys/Mesa change.

## Verification (real-game video — the reliable metric this time)
- **BEFORE** (demo2.mp4, pre-fix): demo1 grenade-launcher pickup = black-triangle
  spike; a second alias model = collapsed black-triangle mess. (artifacts/qglitch-67/
  2026-07-27-guns-still-broken/)
- **AFTER** (spfix2, boot 1): the SAME demo1 grenade-launcher pickup renders as a
  complete, recognizable weapon (blue/gray body, barrel, grip, magazine) — no spikes.
  (artifacts/qglitch-67/2026-07-27-single-pose-FIX/grenade-launcher-AFTER-fix.png)
- **AFTER** (conf3, boot 3): demo renders clean — viewmodel intact throughout incl.
  firing; monsters (zombies) render as recognizable figures; grenades fire+explode.
- **DECISIVE A/B on the SAME model, SAME spot** (yellow-grid-floor room, demo1):
  `guns-BEFORE-fix.png` = the grenade launcher as a black-triangle spike collapse;
  `grenade-launcher-AFTER-fix.png` = the same weapon, same location, fully intact.
- **AFTER (conf4, boot 3, 2026-07-27):** a fresh netboot captured continuously over
  HDMI — quake ran 27–46 fps, **0 faults, 0 GPU wedges, 0 NFS-34**, demo1 played with
  wall torches as clean flames, monsters as recognizable figures, held viewmodel
  (shotgun→nailgun→rocket) intact throughout, and the demo picked up shells/nails/
  armor/**Super Nailgun** with no spike artifacts. (conf4-boot3-sheet{1,2}.png.)
- Method: continuous HDMI video via ffmpeg on /dev/video4, stripped to frames,
  inspecting the actual gun pickups (the fleeting-but-decisive test the user named).
  DET/cross-boot harness abandoned as discredited.
- **Boot count: 3 clean confirmations** (spfix2, conf3, conf4); one other boot excluded
  (hit the NFS `-34`, quake never launched). The bug is deterministic, so the same-model
  before/after is already decisive; 3 clean boots additionally cover the *intermittent*
  idle-viewmodel/monster case.
- **Gun #2 (honest — could not isolate; premise verified from code instead):** the user
  named "two guns" in demo1. Gun #1 (grenade launcher pickup) has the decisive
  before/after above. I searched the after-videos (spfix2, conf4) AND the before-video
  (demo2, whole timeline + full-res opening) and could NOT visually isolate a *distinct
  second* floor-weapon black-spike — the demo grabs later weapons (Super Nailgun etc.) on
  the move through caves, not in a central approach, and no second spiked gun is legible
  at video resolution. Rather than hand-wave "same path", I verified the premise in
  r_alias.c: `blend` is set to 0 exactly when `pose1==pose2` (lines 248–255), and every
  `numposes==1` model has `pose1==pose2` — and EVERY Quake weapon world-pickup
  (`progs/g_*.mdl`: g_shot, g_nail, g_rock, g_light, …) is a single-frame `numposes==1`
  model. So gun #2, being a weapon pickup, *provably* takes the same blend==0 branch that
  gun #1 proves fixed — this is a fact about Quake's data + the code, not an assumption
  about which gun. The held viewmodel (which cycles shotgun→nailgun→rocket) also renders
  clean idle across all 3 boots. Bottom line: gun #1's before/after + the code-verified
  premise cover gun #2; I did not get a photogenic distinct gun-#2 shot and say so plainly.

## Cleanup owed before upstreaming (NOT done tonight — would need rebuild + re-verify)
The verified binary is correct, but r_alias.c carries transitional cruft from the
investigation that should go before publishing (per CLAUDE.md "remove diagnostic-only
code whose hypothesis was disproved/confirmed"):
- `r_alias_lerpmode` cvar modes 0 (snap fallback) and 2 (force-blend=0 FETCH-isolation
  diagnostic) — mode 2's hypothesis is now CONFIRMED and folded into the real fix; the
  cvar defaults to 1 (correct) so it's inert, but it's diagnostic scaffolding.
- The now-DEAD `pose2bind = (numposes==1) ? 1 : lerpdata.pose2` de-alias + its comment
  (lines 260–267): unreachable, because numposes==1 ⇒ blend==0 ⇒ Pose2 is no longer
  bound at all. When blend!=0, `pose2bind == lerpdata.pose2` always. Collapse to
  `lerpdata.pose2` and delete the misleading "bind pose2 to that distinct block" comment.
Deferred deliberately: touching the fix file + rebuilding now risks the verified state
right before reporting, and re-verification needs another HW boot. Do it as a dedicated
cleanup step with a fresh HW confirm.

## Still open
- **NFS exec `-34`**: quake fails to launch ~1/10–1/5 boots (`exec ... failed
  (err=-34)`); the earlier re-drive (kernel c25ed0cb/7e6cbe37) is INSUFFICIENT — the
  `-34` (libnfs catch-all for an unmapped NFSv4 status on the first cold OPEN after
  takeover) persists **>10 s**, exceeding the retry. Correct fix needs the raw NFSv4
  status (add a libnfs log before nfs4.c:188) then handle THAT — not more retries.
  This actively blocked GPU-verification boots tonight.
- fix-A + SLCACTL-ordering: keep (fix-A prevents a separate render wedge; ordering is
  harmless), but they are NOT the #67 fix — the single-pose change is.
- **SEPARATE GPU binner-wedge (still present, NOT the geometry glitch):** the spfix2
  run (this exact fixed build) hit repeated `BIN TIMEOUT ... GPU wedged — true reset +
  drop this frame` late in a sustained 230 s demo1→2→3 loop (drops climbed to 251,
  0.8 fps). conf3 (70 s) and conf4 (110 s) stayed healthy (20–46 fps, 0 wedges). So the
  wedge is a marginal HW depth-pipeline drain stall that surfaces on long/complex runs —
  a real usability issue to chase separately, orthogonal to #67. Do NOT let it block the
  #67 verdict, but do NOT claim "GPU perfect" either.
- Note: `/srv/.../id1/autoexec.cfg` was set to `r_dynamic 1` during testing (a prior
  15-byte autoexec was overwritten).

## REOPENED — root cause NOT the blend/Pose2 branch (2026-07-27, user HW run)
User HW run: gun #1 grenade launcher (g_rock.mdl, 102 verts) renders CLEAN, but gun #2
**nailgun (g_nail.mdl, 130 verts)** pickup STILL collapses, plus some torches, plus
slightly-broken TEXTURES on some health/ammo boxes. Same numposes==1/blend==0 branch
gun #1 proves fixed → same code path, different outcome ⇒ discriminator is MODEL DATA,
not the blend branch. The single-pose change was a partial mitigation (helped g_rock),
not the root cause. (Boxes = BSP brush models `maps/b_*.bsp` — SEPARATE brush-texture
bug, parked.)

### Producer/consumer fork RESOLVED → consumer
- Alias VBO (GL_ARRAY_BUFFER, not cacheable) is mapped Normal-NC (MAP_UNCACHED) in the
  winsys default BO path (v3d_phoenix_winsys.c:584-587); BO is page-granular + memset-0;
  submit `dsb` drains producer writes. So NOT stale-cache coherency.

### Subagent V3D research (Mesa/Linux clones) — key verdicts
- **Over-read / end-padding REFUTED**: fetch reads exactly vattr_sizes components
  (nir_to_vir.c:2355-2362); no vertex-array pad; only ldunifa needs +4B, TFU +64B
  (v3d_resource.c:103-144). And the kernel programs the MMU to ABORT on unmapped access
  (PT_INVALID_ABORT, v3d_mmu.c:85-93) — so a still-rendering collapse is **wrong-but-
  mapped** data, NOT an off-the-end read.
- **VPM/vertex-count limits REFUTED**: VPM/VCM config independent of vertex count
  (vir.c:868-937). 102 vs 130 → identical config.
- **No attribute offset/stride alignment enforced** (byte-granular legal); BOs page-aligned.
- **Leading suspects** (both = wrong-but-mapped addressing, data-dependent):
  1. winsys GL_SHADER_STATE_ATTRIBUTE_RECORD addressing scaling with numverts_vbo (pose1
     xyz offset = numverts_vbo*8, ST offset = 2*numverts_vbo*8 — the only per-model diff).
  2. index handling: DrawElements sets ib.size = full padded BO (v3dx_draw.c:1333) +
     maximum_index unclamped (:844) → an out-of-range/mis-strided index reads neighbor-
     but-mapped data → degenerate triangles.
- **Next empirical step**: dump the actual attribute-record fields (address/stride/
  vec_size/type/number-of-values/maximum_index) + index values the winsys emits for
  g_nail vs g_rock; compare to Mesa reference (v3dx_draw.c:815-822,844; v3dx_state.c:392-442).

### HW instrumentation deployed
gl_mesh.c #67DBG logs per-model numverts_vbo/totalvbo/stofs/numidx/pg_end_slack; deployed
binary md5 5203f2ba (was 47a4e4b6), fresh. Run in progress to collect the table + video.

## Discriminator table (HW #67DBG, 2026-07-27) + investigation state
Single-pose (numposes==1) alias models, VBO total size vs the 4096-byte page:
- g_rock (grenade launcher) 3960 B = 1 page → CLEAN (user + captures)
- w_g_key (gold key)        2352 B = 1 page → CLEAN (observed)
- g_nail (nailgun)          5208 B = 2 pages → BROKEN (user)
- g_nail2                   5472 B = 2 pages ; suit 5688 = 2 pages
- flame (torch, r_nolerp)   6720 B = 2 pages → BROKEN (user "some torches"); 120 verts
  < g_rock's 165 verts ⇒ REFUTES vertex-count threshold; supports PAGE-COUNT/size.
DISCRIMINATOR (leading): a single-pose alias VBO that spans MORE THAN ONE 4KiB page
renders corrupted; ≤1 page renders clean. Consistent across all 4 ground-truth points.

Mechanism note: the "attribute crosses the page boundary" sub-theory is REFUTED for
geometry — g_nail's POSITION data (pose0, offset 0..1736) is entirely in page 0, same
as clean g_rock; only its ST/texcoords cross 4096 (would corrupt texture, not collapse
geometry). So multi-page is the discriminator but page-CROSSING is not the mechanism.
Winsys BO addressing looks correct by inspection (gpuva page-aligned, per-page va2pa PT
mapping, MMAP_BO returns full mapping, no b->pa+offset GPU addressing). MMU is flat 4KiB
PTEs. Leading remaining mechanism: winsys/MMU multi-page mapping or TLB/MMUC-flush gap
that only bites when a BO occupies >1 page (subagent researching Linux V3D parity).

IMPORTANT capture fact: the weapon PICKUPS (g_rock, g_nail) are NOT in demo1's first
~85 s — they appear in a LATER demo (demo2/3). An 85 s capture shows g_rock LOADED but
never DRAWN (0 magenta frames with the tint diagnostic). Must capture ~230 s (demo1→2→3)
to see the guns. This is why earlier gun-hunting in short captures failed.

FLIP EXPERIMENT (in progress): pad g_rock's VBO to 2 pages (data still in page 0) +
tint g_rock magenta (r_alias.c/gl_mesh.c #67DBG-FLIP/#67DBG-TINT). If magenta g_rock
renders BROKEN → 2-page allocation alone is the trigger (mapping bug). If CLEAN → real
data must live in page 2. Deployed md5 e02a3377; 230 s capture running to catch it.
All #67DBG* code is diagnostic and MUST be reverted before any fix ships.

## MECHANISM (subagent, Linux V3D parity, 2026-07-27) — LEADING = per-page va2pa
V3D MMU is flat 4KiB PTEs, format (pa>>12)|VALID(bit28)|WRITE(bit29) — winsys matches
Linux EXACTLY (v3d_mmu.c:27-30 vs winsys:54-55). PTE-format + TLB/MMUC flush both match →
EXCLUDED as the cause. Overfetch EXCLUDED (would break 1-page VBOs too).
LEADING (rank 1): Linux takes each page's PA from the DMA scatter-gather table
(sg_dma_address, v3d_mmu.c:111) — authoritative per page. Our winsys derives each page's
PA from `va2pa(cpu + i*4096)` (winsys:606-608). Page 0 = va2pa(cpu) is always correct →
1-page VBO always fine. For pages i>=1, if va2pa mis-resolves OR MAP_CONTIGUOUS is not
truly physically contiguous, page>=1 maps to WRONG DRAM → corruption ONLY when VBO > 1
page. Exactly fits "1 page clean, 2 pages broken." Rank 2: winsys armed a scratch-redirect
(MMU_ILLEGAL_ADDR) instead of PT_INVALID_ABORT → a bad higher PTE returns zeros/garbage
(silent corruption) rather than a hang.
Highest-value check: dump PT[gpuva>>12 + i] for a 2-page BO; compare page-1 PFN to true PA;
and test whether MAP_CONTIGUOUS|MAP_UNCACHED actually yields contiguous PAs on Phoenix.
Predicted FLIP outcome under this mechanism: g_rock forced to 2 pages with ALL data in
page 0 should render CLEAN (page 2 empty, never fetched) — the bug needs real DATA in
page>=1. If the magenta g_rock is CLEAN, mechanism confirmed.

## SHARPENED (advisor, 2026-07-27): discriminator = fetched data at VBO offset >= 4096
Not "multi-page BO" (multi-page RTs/textures/CLs all render fine → multi-page MAPPING
works; va2pa-page>=1 REFUTED). The real discriminator is where the FETCHED attribute data
sits in the VBO:
- g_rock (CLEAN): pose/ST all end < 4096.
- w_g_key (CLEAN): total 2352 < 4096.
- g_nail (BROKEN): ST = 3472..5208, CROSSES 4096.
- flame (BROKEN): 6 pose blocks × 960B → pose4/pose5 positions land at 3840..5760, i.e.
  >= 4096. Prediction: a torch should flicker broken only on animation poses 4/5.
TWO SEPARATE BUGS: bug#1 = blend==0 double-bind (g_rock's original spikes; KILLED by the
single-pose fix). bug#2 = fetched data at offset >= 4096 (g_nail, flame; untouched by the
fix). g_nail is NOT a regression — it always had bug#2, masked by attention on g_rock.
Prediction for g_nail specifically: only its ST crosses 4096, so it should be TEXCOORD-
garbled (wrong/black-sampled skin), not shape-spiked — CONFIRM which (changes the fix).

## OBSERVATION HARNESS (breaks the demo-hunting loop) + decisive experiment
Demos never reliably frame world pickups (0 usable g_rock frames in 230s; magenta-tint
detection drowned in orange-explosion/red-HUD false positives). So:
- Viewmodel-swap (gl_rmain.c R_DrawViewModel #67DBG-SWAP): force cl.viewent.model to a
  test pickup, cycling every 4s: phase0 g_rock (clean baseline) → phase1 g_nail (expected
  broken) → phase2 g_shot (front-padded). Always centered/large → crop fixed lower-center,
  no hunting. #67SWAP marker printed per phase.
- FRONT-PAD (gl_mesh.c #67DBG-FRONTPAD): prepend 4096 B to g_shot (normally 2160 B, CLEAN)
  via vboxyzofs=4096 so its REAL data is at offset >=4096. If front-padded g_shot renders
  BROKEN → "offset >= 4096" trigger locked, model-independent.
Deployed md5 91d2f83e (swap+frontpad, tint/end-pad reverted). Capture running.
ALL #67DBG* / #67SWAP / front-pad code is diagnostic — MUST be reverted before any fix.
BLOCKING per advisor: no fix/report until one deterministic before(broken)/after(clean)
observation of the actual still-broken model via the harness.

## Investigation outcome (2026-07-27 pm) — mechanism STRONG but NOT self-confirmed
Bug #2 (still-broken nailgun) is NOT the blend==0 bug (that's fixed for g_rock). Best
hypothesis, well-supported but NOT visually confirmed: **the V3D vertex-fetch (VCD)
client misreads alias-VBO attribute data fetched from byte-offset >= 4096.**
Evidence FOR: discriminator table (g_rock 3960 & w_g_key 2352 <4096 = CLEAN; g_nail
ST 3472->5208 crosses 4096 = BROKEN; flame pose4/5 >=4096 = BROKEN) — 4 ground-truth
points, all consistent; vertex-count / multi-page-mapping (working RTs) / over-read
(MMU abort) / page-crossing-of-position all REFUTED.
Evidence AMBIGUOUS: an isolated-model harness (viewmodel-swap of g_rock/g_nail/front-
padded g_shot, r_drawworld 0) showed all three with COHERENT GEOMETRY (silhouettes, no
spikes) — consistent with the prediction that g_nail's breakage is TEXCOORD-garble
(only its ST crosses 4096), invisible on unlit silhouettes. A fullbright textured
isolated run to see texcoords did not render cleanly (console-stuck boot). ~17 HW
cycles spent; a fully clean, phase-attributed, textured before/after was NOT obtained
(demo world clutter, lighting, positioning, spin, phase-offset all confounded it).
DECISION: per the "no fix without a clean before/after observation" rule, did NOT ship
a fix on the unconfirmed mechanism. Reverted ALL diagnostics; redeployed the clean
single-pose-fix binary (md5 0b095116); restored an empty autoexec (the pre-session
~15-byte autoexec content was lost earlier and could not be recovered).
NEXT (proposed): (a) confirm texcoord-vs-geometry on g_nail via a fullbright isolated
model (fix the console-stuck boot first), then (b) if offset>=4096 confirmed, candidate
fix = give each vertex attribute its OWN BO (base offset 0) so no attribute is ever
fetched from a high in-BO offset — OR split large alias VBOs so every attribute stays
< one page. User can verify any candidate trivially (they see the nailgun in demo1).

## Confirm-then-fix attempt (2026-07-27 late) — mechanism NOT confirmed; hypothesis weakened
User approved confirm-then-fix. Attempted to confirm the offset>=4096 hypothesis with a
fullbright isolated-model harness (viewmodel-swap of g_rock / g_nail / front-padded
g_shot, r_drawworld 0). Outcome: INCONCLUSIVE and the hypothesis is now DOUBTED.
- Harness is UNFAITHFUL: the isolated fullbright models rendered as black spikes for
  ALL of them — INCLUDING g_rock, which the user confirms renders CLEAN in the real
  demo. So the harness (viewmodel projection + depth-range hack + scale/position) itself
  induces collapse; its observations cannot be trusted for broken/clean.
- COUNTER-EXAMPLE to offset>=4096: animating monsters (e.g. zombie, 198 poses, ~420 KB
  VBO) fetch pose data from offsets FAR beyond 4096 every frame yet render recognizably.
  If offset>=4096 universally broke vertex fetches, monsters would be destroyed. So the
  hypothesis is NOT universal.
- REFINED correlation that fits ALL data: the BROKEN models (g_nail, flame torches) are
  all drawn via the SINGLE-POSE / blend==0 path (numposes==1 pickups; r_nolerp flames
  force blend=0) AND have fetched data >=4096. The CLEAN monsters use the ANIMATING
  blend!=0 path and are fine at high offsets. So the trigger may be an INTERACTION of the
  single-pose fix (binds only 3 attributes: texcoord + pose1 xyz/normal) with data at
  offset >=4096 — NOT high offset alone. Possibly my single-pose fix (dropping the pose2
  attributes) interacts badly with a high ST offset. UNCONFIRMED.
- Also cost cycles: NFS `-34` exec error + NFS late-mount "not found" (stale nfsd state
  after ~20 reboots; `sudo systemctl restart nfs-server` clears it).
DECISION: did NOT ship a fix (mechanism unconfirmed + counter-example). Reverted all
diagnostics; redeployed clean single-pose-fix binary (md5 13d30ecd); autoexec empty.
The single-pose fix for the grenade launcher REMAINS valid + user-confirmed; only the
nailgun/torch (bug #2) is unresolved.
NEXT: my HW self-observation is unreliable (every harness confounds). Reliable oracles:
(1) the USER (sees the nailgun in demo1 trivially) — have them check specific candidate
builds; or (2) a host-side llvmpipe render of the same models for pixel comparison
(needs Xvfb, not yet installed). Recommend NOT guessing a fix; instead instrument to
capture the exact GL_SHADER_STATE attribute record + index values the winsys emits for
g_nail vs g_rock (advisor's tie-breaker) and compare to Mesa reference — a data dump,
not a visual, so it sidesteps the observation wall.

## PIVOTAL (2026-07-27 late, user confirmed (A)=geometry): NOT a global count limit
User: nailgun breakage is WRONG SHAPE (geometry collapse), not texture. Full MDL
geometry table (pak0.pak) demolishes the raw-count hypothesis:
- ANIMATED monsters render FINE at high counts: knight 213 tris, ogre 326, soldier 328,
  zombie 347, demon 275, shambler 284, boss 555, player 408 — all numframes>1, all fine.
- g_nail BROKEN at only 222 tris. So NO global vertex/primitive count limit.
DISCRIMINATOR = numframes: every BROKEN model is SINGLE-FRAME (numframes==1 → the
blend==0 / single-pose path): g_nail(222t), g_nail2(248t), suit(318t). And WITHIN the
single-frame set there's a threshold — g_rock(176t/102v) & g_shot(84t) & backpack(110t)
& w_g_key(116t) CLEAN; g_nail(222t/130v) BROKEN. Threshold between 176 and 222 tris.
Multi-frame animated models are IMMUNE at any count (ogre 326t fine).
So: a count-sensitive geometry collapse SPECIFIC to the single-pose/blend==0 path — the
exact path my #67 work touches (vboposes=2 duplicate block, blend==0). Ruled out already:
pose2 enable/disable/de-alias (all leave nailgun broken), VBO offset>=4096, skin/texture.
Prime remaining suspects (single-pose-path-specific + count-thresholded):
  (a) the numposes==1 DUPLICATE pose block (vboposes=2) — a candidate test is to drop it
      (vboposes=1) since my single-pose fix no longer binds pose2;
  (b) a VPM / binner / coordinate-shader limit that the single-pose draw config hits at a
      LOWER effective count than the animated config (subagent researching).
NEXT (autonomous, user asleep): await binner/VPM research; implement best-reasoned fix;
self-verify best-effort (idle high-tri monsters use blend==0 too — should collapse pre-fix
and be a reliable-to-observe proxy since monsters are always framed, unlike pickups).
User will confirm the nailgun + super-nailgun in the morning (question already posed).

## Research round 3 + ruled-out summary (2026-07-27 night)
Subagent (Mesa/Linux clones) on per-draw count limits: REFUTED all count-limit theories —
no HW per-draw vertex/primitive limit; Mesa never splits large draws (v3dx_draw.c:1358);
INDEXED_PRIM_LIST Length = 31 bits (v3d_packet.xml:566, nowhere near ~600); VPM/VCM sizing
is per-shader/static (vir.c:936, GFXH-1744 2-4 batch clamp — independent of draw vertex
count); binner overflow DROPS/STALLS (handled cleanly by kernel v3d_irq.c:38-84 via a fresh
256KB BO), it does NOT reposition vertices. Conclusion: the wrong-positioned-vertex
corruption is a WINSYS/config issue that appears past a size threshold; leads = VPM/VCM
segment config in the shader-state record, or the attribute Maximum-Index bound.
BUT: our port patch (mesa-phoenix-port.patch) touches v3dx_draw.c only for an EZ-re-enable
comment and v3dx_state.c for an R<->B-swap comment — it does NOT modify the attribute-record
/ VPM / Maximum-Index / draw-count emission, which are therefore STOCK Mesa (correct). So the
subagent's rank-1/2 (VPM config, Maximum Index) don't obviously apply — the emission is stock.

RULED OUT (this session, mostly user-confirmed on HW):
- Cross-boot cache / SLCACTL / fix-A (the original "resolved" was a false-positive metric).
- Offset >= 4096 (user's ST-first-reorder run: nailgun still broken with all data <4096).
- Pose2 handling: disabled / de-aliased / aliased all leave the nailgun broken.
- Skin/texture size (user: nailgun breakage is WRONG SHAPE, not texture).
- Global vertex/primitive count limit (animated monsters render fine at 326-555 tris).
- Early-Z (scene-wide; would hole the world/monsters too — user says those are fine).
- Mesa attribute/VPM/Maximum-Index emission bug (stock, unpatched).

SOLID FACTS (the discriminator to fix): grenade launcher g_rock (176 tris/102v, single-frame,
blend==0) CLEAN; nailgun g_nail (222 tris/130v, single-frame, blend==0) BROKEN = wrong SHAPE.
w_g_key CLEAN. Broken set is single-frame (numframes==1 / blend==0 path) AND above a
~176-222-tri threshold; animated (blend!=0) models immune at any count. Held viewmodel v_nail
(numframes=9) went transparent WHEN IDLE (blend==0) — same path — reinforcing that the trigger
is the blend==0/single-pose DRAW, count-scaled, model-specific, and geometry (missing/collapsed
triangles). World + monsters render fine (not scene-wide).

STATE: clean single-pose-fix binary redeployed (md5 3c008c86 at revert; note a later identical
rebuild). Grenade-launcher fix intact + user-confirmed. Nailgun/torches/held-weapon/boxes NOT
fixed. Boxes = BSP brush models (separate path). autoexec emptied (original ~15B lost earlier).

PRIORITIZED NEXT STEPS (for morning / user-as-oracle):
1. Answer the pending question: which floor guns are broken vs clean (esp. Super Nailgun,
   g_nail2) — maps the tri-count threshold within single-frame models, confirms it's systematic.
2. Reliable non-visual tie-breaker: instrument Mesa's emit_gl_shader_state (external/mesa
   v3dx_draw.c ~640-871) to log the BIN coordinate-shader VPM in/out segment sizes + VCM cache
   size + attribute maximum_index for g_nail vs g_rock vs an animated model; capture to UART
   (reliable to read). A mismatch specific to the single-pose draw would pinpoint it.
3. Candidate to test (single-pose-path-specific, fast quakespasm build): drop the numposes==1
   duplicate pose block (gl_mesh.c vboposes=(numposes==1)?1:numposes) — unused since the
   single-pose fix disables pose2; tests whether the dup block corrupts larger single-pose draws.
4. Confirm whether the blend==0 vs blend!=0 distinction is real: does an IDLE (paused-animation)
   high-tri monster collapse while the same monster walking is fine? (Hard to frame in demos.)
