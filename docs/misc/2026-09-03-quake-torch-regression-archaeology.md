# Quake torch regression archaeology — why it keeps coming back

Date: 2026-09-03
Scope: analysis only. No build, no Pi cycle, no writes under `external/`.
Trigger: owner report — *"in the Quake1 screenshots — including THE most recent one
(vkq-baseline-final) there is the classical Q1 bug — no torches are rendered in the map
start. I feel like we've solved this bug 5 times already in different build setups and it
keeps returning!"*

**Short answer.** It is not one bug returning five times. It is **two unrelated bugs plus
one delivery failure**, and the reason the *last* one "keeps returning" is that it was
never actually fixed — it was repeatedly **declared** fixed against evidence that does not
show a torch. This document proves that claim at the pixel level: the single HDMI grab
that closed the bug on 2026-08-22 shows a **lavaball projectile**, not a wall torch.

---

## 0. The instrument this analysis added (and which the project never had)

Every torch verdict in project history was an agent eyeballing a dark HDMI grab with no
reference to compare against. This session produced the missing reference by running the
already-built host quakespasm headless on `map start` from the spawn point:

```
external/quakespasm/Quake/quakespasm -basedir <scratch> -width 1920 -height 1080
   autoexec.cfg: host_framerate 0.05 / r_particles 0 / map start / <40x wait> / screenshot
   env: SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1
```

→ `docs/misc/torch-archaeology/host-reference-start-map.png` (1920x1080).

Ground truth for `start.bsp` (entity lump extracted from
`/srv/phoenix-rpi4-nfs/usr/share/quake/id1/pak0.pak`, md5 `5906e5998f…`, **identical across the 6 staged copies checked** — game data is *not* a variable here):

| classname | count | model | entity kind |
| --- | --- | --- | --- |
| `light_torch_small_walltorch` | 31 | `progs/flame.mdl` (75 verts, 1 frame-group) | `makestatic()` |
| `light_flame_large_yellow` | 10 | `progs/flame2.mdl` (55 verts, 2 frames) | `makestatic()` |
| `misc_fireball` | 3 | spawns `progs/lavaball.mdl` | regular packet entity, **moves** |

The two torches the owner means are `light_torch_small_walltorch` at `(394 762 84)` and
`(698 762 84)` — the pair flanking the "QUAKE" archway, directly ahead of
`info_player_start (544 288 32, angle 90)`. In the host reference they sit at 1920x1080
pixel `(735, 470)` and `(1188, 478)`.

`id1/config.cfg` was checked as a possible hand-staged carrier of the fix: it contains only
`vid_width/vid_height/vid_fullscreen`. Ruled out.

---

## 1. Timeline of every occurrence

| # | Date | Engine | Symptom | Diagnosed cause | Fix — where it landed | Validation | What later removed / bypassed it |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 2026-06-23 → 06-26 | GLQuake (quakespasm + gallium GL) | **#28** wall-torch flame renders as **black triangles** | never established; suspected GLSL alias lighting / `GL_BYTE` normal decode (`docs/done/2026-06-23-overnight-progress.md:276-280`) | two band-aids in `external/quakespasm`: `90da546` (unconditional pose-snap `pose1=pose2`) + `5e3ec37` (floor alias lit-term 0.7) | one demo HDMI grab, no filename, self-caveated *"not the user's exact scene"* (`docs/done/2026-06-26-overnight-results.md:14-16`) | `5e3ec37` reverted inside `90da546`; `90da546`'s default flipped back by `8fdede9` (07-18). `docs/done/2026-07-18-quake-glitch-marathon.md:191`: torches are on `r_nolerp_list` → **byte-identical before/after the lerp change; UNTOUCHED**. #28 was never actually fixed by #28's fix. |
| 2 | 2026-07-23 | GLQuake | **#67** alias models (incl. `flame`) collapse to a fan/wedge in **model space** | `numframes==1` ⇒ pose1 and pose2 bound to the same (BO, offset) → "attribute aliasing" | `external/quakespasm 0a900c7` de-alias (duplicate pose block) | *"Torch flames … render as clean, proper flames everywhere. This is the decisive proof"* (`docs/done/2026-07-23-quake-single-frame-alias-glitch.md:105-126`) | **Retracted next day** (`:131-159`) — the clean run was luck under a non-deterministic bug. Worse: the duplicate block is what later pushed big single-pose VBOs over 4 KiB and *caused* occurrence 5. |
| 3 | 2026-07-25 | GLQuake (V3D winsys) | same mangle, now framed as per-cold-boot non-deterministic | fire-and-forget `SLCACTL` slice invalidate raced by the binner's coordinate-shader vertex fetch | coord `214be9a` ("fix-A", waited L2T flush) in `tools/v3d-driver-port/v3d_phoenix_winsys.c` | cross-boot crop-diff determinism over 5–10 boots; `artifacts/qglitch-67/ab-0090.png` | **Contradicted 1 day later**: 10-boot campaign found *"Torches: STILL glitch"* (`docs/done/2026-07-26-nfs-boot-and-quake-10boot-campaign.md:56-63`, `artifacts/quake-10boot-campaign/torch-glitch-boot1-vs-boot5.png`) |
| 4 | 2026-07-26 | GLQuake (V3D winsys) | wall torches as mangled red/black spikes | ordering: Phoenix's `l2t_flush_wait` removed Linux's implicit settle window | coord `457a650` — issue `SLCACTL` first after the submit `dsb` | cross-boot variance 3–5.5% → **0.0%**; prose *"wall torches render as correct flames"*, **no grab filename** | **Retracted 07-27** (`docs/done/2026-07-27-67-REAL-fix-single-pose.md:3-13`): *"A consistently-broken render scores 0.0% cross-boot and falsely passes."* fix-A kept for an unrelated reason (CT1 wedge margin, coord `3567f2f`). |
| 5 | 2026-07-27 → 07-28 | GLQuake | deterministic black-triangle spikes on pickups + `r_nolerp` models (torches) | (a) V3D mishandles the alias draw at `blend==0` while both 8-bit position attrs are bound; (b) a single-pose alias VBO crossing a 4 KiB page is mis-fetched | `external/quakespasm 4ef0a42` (don't bind Pose2 at `blend==0`) + `3d742a3` (`vboposes = numposes`, undoing occurrence 2's dup block) | model gallery **61/61 coherent** — the first faithful instrument in the saga | Genuine, and **still in effect today** (verified in the shipped patch, §3). But note `flame` was already CLEAN in the pre-fix gallery calibration (`docs/done/2026-07-28-model-gallery-test-plan.md:129-130`), so `3d742a3` is **mis-credited** as the torch closer; `4ef0a42` is the better-supported one. In-game verify was blocked by NFS flakiness (`:101-107`). |
| 6 | 2026-08-03/04 | **vkQuake** | **INVISIBLE** archway torches (a different symptom class entirely) | the no-WSI `/dev/fb0` scanout keys on colour-buffer **alpha**; `flame.mdl` is almost all fullbright texels, where the nobright diffuse alpha ≈ 0 → scanout dropped them | `external/vkquake d3e329c` — opaque alias draws set `ubo->flags |= 0x10`; `Shaders/alias_common.inc` forces `result.a = 1.0` | *"HW-verified 7/7 map frames render both archway torches as orange flames"* (`manifests/2026-08-03-vkquake-torch-alpha-fix.md:6`) | **The artifacts are gone and vkQuake has no engine-side capture harness** (§4), so the "7/7 captured map frames" were HDMI tick grabs — the same instrument that produced occurrence 7's false positive. Unverifiable today. |
| 7 | 2026-08-22 | vkQuake | torches gone again; scene darker (owner-reported, `artifacts/hdmi/20260822-143739-vkq-semafix-final.png`) | **stale binary** — the tested ELF was a P7 semaphore relink of pre-`d3e329c` objects (`docs/done/2026-08-22-gl-gpu-regressions-owner-reported.md:60-64`) | clean rebuild (`rm /tmp/vkqobj`, all 83 TUs) → relink → stage `/srv/phoenix-rpi4-nfs/bin/vkquake` | *"grab `20260822-200435-vkq-torchfix-tick.png` shows a flame rendering **and animating** on the right wall"* (`:66-71`) | **PROVEN FALSE THIS SESSION** — see §2. The "flame rendering and animating on the right wall" is a `misc_fireball` **lavaball projectile**, not a torch. The archway torches are absent in that grab. Same doc, same day, round 2 left `flame2.mdl` fire-pit flames explicitly **OPEN** (`docs/done/2026-08-22-owner-test-round2-issues.md:27-43`, `:153`). |
| 8 | 2026-09-03 (now) | vkQuake only | archway torches absent in `20260903-061703-vkq-baseline-final.png` | — | — | — | **No regression occurred between occurrence 7 and now.** The two frames are indistinguishable in the torch ROI (§2). Occurrence 7's "fix" simply never worked. |

Adjacent, **not** torch occurrences (kept so the count stays honest): the 2026-06-16 `GL_QUADS`
particle square (torch text there is *validation content*), and the 2026-07-15 single-buffer
scanout flicker.

---

## 2. The proof: 2026-08-22's "FIXED + HW-VERIFIED" grab shows a lavaball

`docs/misc/torch-archaeology/aug22-verification-was-a-lavaball.png` — three rows, same crop,
same brightness boost, all at 1920x1080 (today's 4K grab downscaled):

1. **host reference** — two orange archway torch flames, plus a small red lavaball far right.
2. **`20260822-143739-vkq-semafix-final.png`** (the owner's failing grab) — no torches, **and no lavaball**.
3. **`20260822-200435-vkq-torchfix-tick.png`** (the grab that closed the bug) — **still no torches**, but the **lavaball is present**, with its particle trail.

The `misc_fireball` at `(864 992 -168)` in the right-hand lava pit launches a
`progs/lavaball.mdl` on a periodic timer. It is therefore present in some frames and absent
in others, and it *moves* — which is exactly what "a flame rendering **and animating**"
described. `docs/misc/torch-archaeology/lavaball-moves-across-frames.png` shows it on a
ballistic arc with a smoke trail across four consecutive 2026-09-03 tick frames: frame 1 top
of arc, frame 2 gone, frame 3 trail only, frame 4 mid-arc. A wall torch does not move.

The before/after pair the doc cited as *"same camera angle ⇒ content difference not camera"*
was real — but the content difference was **the lavaball's duty cycle**, not the fix.

`docs/misc/torch-archaeology/right-torch-roi-gamma-boost.png` (gamma 0.35, 6x zoom, right
torch ROI, reference vs today) settles the follow-up question: today's frame shows **bare wall
texture**, not a black flame silhouette. So the symptom is *pixels never reaching the
framebuffer*, not *pixels reaching it unlit*. That rules out a fullbright-texture-sampling
failure and is bit-for-bit the **pre-`d3e329c` symptom**.

---

## 3. Present-day status, per engine

### quakespasm (GLQuake) — fix present, and **visually confirmed working today**

- Ships as `/usr/bin/quakespasm`, framework port only. `ports.yaml:200-201` (`if: true`) → `sources/phoenix-rtos-ports/quakespasm/port.def.sh` (pinned tarball `6baceeac…` + generated `patches/0001-quakespasm-phoenix-v3d-single-elf.patch`) → `port.def.sh:196-197` install to `/usr/bin`.
- The occurrence-5 fixes are in the generated patch: `Quake/r_alias.c` @@-225/@@-242 (no Pose2 bind at `blend==0`; in-patch comment at line 727 names #67 explicitly) and `Quake/gl_mesh.c` (`vboposes = hdr->numposes`).
- `scripts/game-port-patch.sh --check` → `quakespasm OK (12 files, 522+/17-)`; the applied-patch content-hash marker equals the patch sha256.
- **Visual confirmation:** `docs/misc/torch-archaeology/quakespasm-sep03-torches-present.png` — six 2026-09-03 `final-qs` frames, wall-torch flames clearly rendering in four of them. A 2026-09-02 `d9quake` frame (`20260902-202047-d9quake-tick.png`) shows a textbook torch too.
- **This contradicts the "both engines" premise.** The recurring torch absence is vkQuake-only. (The owner's wording — "the Quake1 screenshots, including vkq-baseline-final" — is consistent with all the frames in question being vkQuake.)

### vkQuake — fix present in **all three** representations, symptom still present

| representation | contains `d3e329c`? | evidence |
| --- | --- | --- |
| fork `external/vkquake` HEAD (`d3e329c`, on `master`) | yes — it *is* the tip | `git show d3e329c` |
| generated patch `sources/phoenix-rtos-ports/vkquake/patches/0001-…patch` | yes, both halves | `Quake/r_alias.c` +7 (`ubo->flags |= 0x10`), `Shaders/alias_common.inc` +7 (`result.a = 1.0`); `--check` → `vkquake OK (9 files, 202+/24-)` |
| pre-compiled SPIR-V blob `glue/vkquake_shaders.c` (the shader source in the patch is **never compiled**) | yes | `spirv-dis alias_frag_spv`: `%uint_16 = OpConstant %uint 16` → `%16652 = OpBitwiseAnd %uint %14185 %uint_16` → `%21521 = OpCompositeInsert %v4float %float_1 %20612 3` (alpha ← 1.0). Same in `alias_alphatest_frag`, `alias_oit_frag`, `alias_alphatest_oit_frag`. |
| `__phoenix__` guard actually defined? | yes | `aarch64-phoenix-gcc -dM -E` → `#define __phoenix__ 1` |
| shipped ELF | current | `/srv/phoenix-rpi4-nfs-gcc16/usr/bin/vkquake`, 12,799,520 B, 2026-09-03 09:28:58 == `.buildroot/…/prog.stripped/vkquake` |

And the shader modules are **the same generation as the ones that ran on the day the fix was
declared working**: the 2026-08-04 00:00 UART log `rpi4b-uart-20260804-000020-vkq-alpha1.log` reports
`shmod alias_frag size=2036 / alias_alphatest_frag 2120 / alias_oit_frag 2716 /
alias_alphatest_oit_frag 2800` — byte-for-byte the current blob's array sizes.

**So: nothing was lost.** The fix ships in the source, in the patch, in the compiled shader,
and in the binary — and the torches are still gone. Combined with §2 (the only surviving
"verified" artifact is a lavaball) the parsimonious reading is that **`d3e329c` never fixed
the vkQuake torches on hardware**, and the "regression" the owner keeps seeing is the
original 2026-08-03 bug, continuously present since then, closed twice on bad evidence.

Two smaller live hazards found en route (real, but *not* the cause here):

- The active NFS export is `/srv/phoenix-rpi4-nfs-gcc16` (the only `fsid=0` in `/var/lib/nfs/etab`). The **inactive** `/srv/phoenix-rpi4-nfs` still holds `/bin/quakespasm` (2026-08-10) and `/bin/vkquake` (2026-08-22), and `rootfs-overlay/etc/rc.psh:10` puts `/bin` **before** `/usr/bin` on `PATH`. Verifying on that tree would run August binaries.
- Dead `_user` artifacts persist: `/srv/phoenix-rpi4-nfs/usr/bin/rpi4-{quake,vkquake}` (2026-08-26) and the whole `.buildroot-gcc16` tree (2026-08-23), for components deleted from source on 2026-09-03 (D10). No build produces them any more.

---

## 4. Mechanism verdict

The recurrence is **not** caused by dual build paths or fork↔patch drift. Both were checked
and both are clean for these two ports' **engine sources** today (the hand-maintained `glue/`
shims *are* drifting — see §5.2 — but the drifting file is not torch-related): the `tools/quakespasm-port` /
`tools/vkquake-port` scripts are invoked by nothing (`build-showcase-apps.sh:381-385` records
their retirement on 2026-09-03), `phase_stage()` stages no engine binary, and
`game-port-patch.sh --check` reports both games byte-current against their forks. The
"#67 is fork-only surplus" lead from `docs/misc/2026-09-02-game-build-unification-plan.md:26-28`
was already resolved as false by `docs/misc/2026-09-02-game-source-of-truth-audit.md:306`.

**The dominant mechanism is verification failure — specifically, closing a visual bug against
an instrument that cannot see it.** Every torch verdict in this project's history was produced
by one of four unfaithful instruments:

1. **An agent eyeballing a dark HDMI grab with no reference render.** This is how occurrence 7 closed on a lavaball, and — since vkQuake has **no** engine-side capture harness (`scr_capture` exists only in the quakespasm patch, `Quake/gl_screen.c` +195; the vkquake patch has no capture code) — it is the only instrument occurrence 6 could have used either.
2. **Cross-boot determinism.** Occurrences 3 and 4. Explicitly discredited afterwards: *"A consistently-broken render scores 0.0% cross-boot and falsely passes."*
3. **A demo/scene that never frames the thing under test.** Occurrence 1 ("not the user's exact scene"); the surviving `conf4-boot3-demo1-torch.png` from occurrence 5 contains no torch at all.
4. **A harness that itself perturbs the render** (`docs/done/2026-07-27-67-REAL-fix-single-pose.md:252-262`).

Two amplifiers turn a false positive into a *recurring* bug:

- **Symptom-name collision.** "Torches" covered two unrelated bugs — GLQuake **mangled geometry** (alias vertex-attribute/VBO-page, `4ef0a42`/`3d742a3`) and vkQuake **invisible** models (fb0 scanout alpha, `d3e329c`). `docs/done/2026-08-22-gl-gpu-regressions-owner-reported.md:93` conflates them outright (*"the long-fought #67 torch/alpha bug resurfaced"*). Each new report gets re-diagnosed against the other family's history, and the GL fixes' genuine success reads as evidence the vkQuake one is fixed too.
- **Closure records that outran their evidence, then were deleted.** `docs/KNOWN-ISSUES.md` commit `8ef82a292` (2026-08-05) replaced an entry containing the literal strings *"CORRECTION 2026-07-27: the above 'RESOLVED' claims were FALSE POSITIVES"* and *"REOPENED"* with a single confident RESOLVED entry claiming torches render correctly; commit `fe4dcfdc6` (2026-08-27) deleted the entry entirely. There is no torch/#28/#67 entry in `KNOWN-ISSUES.md` today. Nothing in the tree carries the caveat forward, so each session starts from "it was fixed".

**The delivery-loss family is real and well documented, but it is the secondary leg** — and it
is the *only* one that ever produced a genuine code-side loss: occurrence 7's stale
`/tmp/vkqobj` objects. Its siblings (`archive_fresh` watching only `tools/`; a cached `_user`
program; `-DQSS_PHOENIX` missing from `QFLAGS` so seven consecutive fixes were dead code;
24 dual-personality build paths) are all documented in
`docs/misc/2026-09-03-port-build-deduplication-audit.md` and none of them applies to these two
engines today.

**Ambiguity, stated plainly.** I cannot prove occurrence 6 (2026-08-03/04) never worked — its
capture artifacts are gone. What is proven is: (a) occurrence 7's closing evidence shows a
lavaball; (b) the fix ships intact in every representation today; (c) the shader bytes are
identical to the 2026-08-04 run; (d) the 2026-08-22 and 2026-09-03 frames are
indistinguishable in the torch ROI. Two readings survive: *the fix never worked and both
closures were misreads*, or *the fix worked and something below vkQuake (V3D/Mesa/kernel)
regressed between 2026-08-04 and 2026-08-22 and was then masked by a false closure*. The first
is more parsimonious; the second is not excluded and would be settled by the gate in §5.1.

---

## 5. Permanent fix

The goal is that a torch-less frame **cannot be signed off**, and that a fix present in source
**cannot** be absent from the binary. Four concrete changes.

### 5.1 A torch-presence gate that fails the cycle (the one that matters)

New `scripts/quake-torch-gate.py` + a call from the post-cycle health table in
`scripts/summarize-rpi4b-uart-log.py` / `scripts/uart-summary.sh`.

- **Reference:** commit `docs/misc/torch-archaeology/host-reference-start-map.png` plus a small
  `docs/misc/torch-archaeology/start-map-torch-rois.json` holding the two archway ROIs
  (1920x1080: `[705,435,765,510]` and `[1158,440,1218,515]`; scale by frame width) and, for
  contrast, the lavaball's ROI marked `ignore: true` **with a comment saying why** — that
  single annotation is what stops the 2026-08-22 mistake from recurring.
- **Assert:** for each torch ROI, ≥ N pixels with `R > 180 && R > G+40 && G > B+20`
  (the flame's fullbright orange/white core; the surrounding wall never reaches it — verified
  on the reference and on `right-torch-roi-gamma-boost.png`). Score over all `*-tick.png`
  frames of the cycle; require ≥ 2 frames passing, since the flame animates and a frame may
  tear.
- **Wire it in:** any label matched by `vkq*`/`*quake*` with `+map start` in the launch line
  gets a `[✓]/[✗] start-map archway torches (L/R)` row, and `[✗]` makes the cycle **fail**,
  the same way a missing `lwip started` row already reads as failure.
- Because it scores the same HDMI PNGs the project already captures, it is retroactive: run it
  over `artifacts/hdmi/2026*-vkq-*` and it reproduces this document's verdict without a Pi.
- **Regeneration recipe** for the reference (host binary already built, no `make` needed) is in
  §0 — record it in the script header so the reference is reproducible, not a magic blob.
- Note the constraint found this session: vkQuake has **no** engine-side capture, and
  quakespasm's `scr_capture` gates on `cls.demoplayback`
  (`docs/done/2026-07-18-quake-glitch-marathon.md:302-304`), so a start-map *engine* capture
  needs either that gate relaxed or a recorded `start` demo. **The HDMI-ROI gate needs neither**
  — prefer it, and treat engine-side capture as a later refinement.

### 5.2 Close the third-representation hole in the vkQuake shader blob

`glue/vkquake_shaders.c` is a **generated** artifact regenerated only when someone remembers
to run `tools/vkquake-port/gen-vkquake-shaders.py`. `game-port-patch.sh --check` compares
fork↔patch and is blind to it: a future `Shaders/*.inc` fix will land in the patch, `--check`
will say `OK`, and the binary will still run the old SPIR-V. Worse, the generator **silently
falls back to a 5-word placeholder module** if no glslang is on `PATH`.

- Have `gen-vkquake-shaders.py` write a header line `/* generated from Shaders/ @ sha256:<h> (mode: REAL|PLACEHOLDER) */`, hashing the concatenated sorted `Shaders/*.{vert,frag,comp,inc,h}`.
- Extend `scripts/game-port-patch.sh --check` with a `vkquake-shaders` row that recomputes that hash from `external/vkquake/Shaders` and reports `STALE` on mismatch.
- Make `port.def.sh` **`b_die`** if the blob's mode is `PLACEHOLDER` or if any `*_spv` array is ≤ 20 bytes. A placeholder shader must never reach a ship build.

The same blind spot exists one level over, and it is **live today**: the `glue/` shims are
hand-maintained in `sources/phoenix-rtos-ports/<port>/glue/` with a second copy under
`tools/<port>-port/`, and `--check` compares neither. Four files currently differ, and in
`vkquake/glue/pl_phoenix_sdlcompat.c` the **`tools/` copy is the newer one**: it dropped a
local `double copysign(double,double)` on 2026-09-02 because libphoenix/libm now export it,
while the shipping copy still defines it — the link only survives because `port.def.sh:251`
passes `-Wl,--allow-multiple-definition`, so the shipped `vkquake` binds our stale local
`copysign`. Not torch-related, but it is a live instance of exactly the family the owner
suspects. Extend `--check` with a `glue` row that diffs each shipping shim against its
`tools/` counterpart (or delete the `tools/` copies now that nothing invokes them).

### 5.3 Close the regen trap that would delete the torch fix

`external/vkquake`'s local branch `phoenix-rpi4-port` is at `51ddfc4` — **exactly one commit
behind `master`, and the missing commit is `d3e329c` itself**. `game-port-patch.sh` diffs
`HEAD`, while both `port.def.sh` headers claim the patch comes from branch
`phoenix-rpi4-port`. A `git checkout phoenix-rpi4-port` followed by `--regen vkquake` would
silently delete the torch fix from the shipped patch, and `--check` would then report `OK`.

- Fast-forward that local branch to `d3e329c` (owner action — this file does not touch `external/`), **and** make `game-port-patch.sh` diff an explicit ref recorded in `port.def.sh` rather than whatever `HEAD` happens to be, so the two can never disagree again.
- Also surface the currently-red `--check`: `yquake2 STALE` (out of scope here, but any build gate wired to `--check` is failing today).

### 5.4 Process rule

> A "FIXED" claim for a visual bug must cite an artifact that a **checker** can re-score, and
> name the ROI it scored. Prose ("torches render as correct flames") and a metric that a
> consistently-broken render also passes are not evidence.

Add it to `docs/knowledge/testing-automation.md` next to the probe-parity rule, and restore a
`docs/KNOWN-ISSUES.md` entry for the vkQuake torch bug that says **open**, with a pointer to
this document — the previous entry's deletion is a documented part of the recurrence.

---

## 6. Corrections to existing docs (stale claims found, not fixed here)

- `docs/pi4-hardware-support-matrix.md:54`, `:94`, `:112` state the vkQuake torch/alpha-scanout bug is **FIXED** and `torch flame #28` is **done**. §2/§3 contradict the vkQuake half.
- `docs/done/2026-08-22-gl-gpu-regressions-owner-reported.md:60-71` ("Issue 2 — RESOLVED (stale binary) + HW-VERIFIED") is **disproved**: its evidence grab shows a lavaball.
- `docs/done/2026-08-22-owner-test-round2-issues.md:27-43` (fire-pit `flame2.mdl` flames missing) is still open and is very likely the **same** bug as the walltorch case — both are fullbright-heavy `makestatic` alias models.
- `external/quakespasm-det` is **not** a Quake fork; it is a second clone of this coordination repo (root commit and `origin` match). Any doc treating it as an engine variant is wrong.
- Neither fork nor patch contains any efrag / static-entity change (`MAX_EFRAGS`, `MAX_STATIC_ENTITIES`, `R_AddEfrags` = 0 hits); stock upstream values are used. The static-entity path is *not* a candidate.

## 7. Next diagnostic step (for whoever fixes it the sixth time)

Do **not** blind-reapply anything. The symptom is bit-for-bit pre-`d3e329c` (pixels absent,
not black — §2), while the flag-0x10 shader path is provably compiled in. So instrument the
**runtime**: print `ubo->flags`, `has_alpha`, `alphatest` and the bound pipeline for every
`flame.mdl` / `flame2.mdl` draw in `GL_DrawAliasFrame`, one line per model per frame, and boot
`+map start`. Three outcomes, three different bugs:

- flag 0x10 **not** set → the `!has_alpha && !alphatest && showtris == 0` condition fails at runtime; fix the condition, not the shader.
- flag 0x10 set but no draw logged → the flame entity never reaches the alias path (visedict/cull/`makestatic` handling); a wholly new bug, and the one the "invisible" symptom class actually points at.
- flag 0x10 set and drawn → the loss is below vkQuake (V3D/Mesa/fb0 scanout) and something regressed there between 2026-08-04 and 2026-08-22.

Land the §5.1 gate **first**, so whatever comes out of that cannot be closed on a lavaball.
