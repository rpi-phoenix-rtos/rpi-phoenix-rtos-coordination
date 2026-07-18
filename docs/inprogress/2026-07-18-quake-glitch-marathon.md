# Quake glitch marathon (2026-07-18+, autonomous multi-day)

Mandate: fix the Quake rendering glitches FULLY; also NFS issues + any important
issues found. Open-minded — rewrite/refactor/experiment (git branches/stashes),
write small diagnostic GL programs. Commit what works, document, update memory.
User away 3-4 days; no questions.

## Rollback anchors
- Pre-everything known-good (flicker-fixed, pre-upstream): `manifests/2026-07-16-flicker-vcmbox-fixed-knowngood.md`
- Upstream-merged + validated: `manifests/2026-07-17-upstream-merged-validated.md`

## Known issues (living list)
1. **Model rendering glitches** — per-boot-varying black/skewed triangles on dynamic
   models (monsters/weapons/torches). Intermittent; does NOT reliably reproduce in the
   deterministic demo. PRIMARY target.
2. **Capture harness** — TGA screenshot capture read noise (render-to-scanout: glReadPixels
   reads the FBO's unwritten CPU mapping). FIX IN PROGRESS (read fb PA of displayed buffer).
3. **Triple-buffer flip BUG (found 2026-07-18)** — winsys `v3d_phoenix_flip` used a 2-position
   pan `(buf!=0)?phys_h:0` (double-buffer-era) for 3 buffers → buffer 2 mis-displayed as
   buffer 1; buffer 2's renders never scanned out (every-3rd-frame stale). FIXED: pan =
   buf*phys_h. (May also improve smoothness / be a glitch contributor.)
4. **NFS exec -5** — intermittent EIO exec'ing rpi4-quake (17MB) over NFS. Retry works.
5. **Recurring build kills** — the mesa-codegen "materializing" phase gets killed
   intermittently (not OOM — 21Gi free). Re-run gets past it.

## Method / tooling
- Visual-regression harness: deterministic demo (host_framerate 0.05, r_particles 0) →
  Pi TGA capture via TCP sink (:5599) → host llvmpipe reference → quake-visual-compare.py
  (SSIM / blacktex% / HUD). Primary probe for non-determinism = Pi-boot-A vs Pi-boot-B at
  identical frame indices.
- HDMI capture (flicker-capture-analyze) = the DISPLAYED (correct) output, for eyeballing.

## Log
- 2026-07-18: found+fixed the triple-buffer flip bug (winsys). Reworked capture readback to
  read the ACTUAL displayed region (winsys tracks the pan offset; buf=-1). Building
  (flipfix) on the upstream-integrated tree. Next: validate capture produces correct frames,
  then multi-boot glitch hunt.

- 2026-07-18 (cont): capture mmap-the-scanout-PA approach FAILED 5 ways (always noise, even
  reading the tracked displayed region + after the flip fix). Root: reading the fb PA via
  mmap doesn't yield the rendered pixels (reason unclear; MAP_SHARED reads *some* real fb but
  never the clean frame). ABANDONED it. NEW GL-native capture: blit the just-rendered scanout
  FBO -> a normal DRAM FBO on the GPU (reads scanout via GPU-VA correctly), then glReadPixels
  the normal FBO (real CPU-backed BO). qsv3d_capture_gl(). Should sidestep the alias entirely.
- 2026-07-18: NFS exec -5 flake ROOT-CAUSED + fixed. nfs_ops read/open/lookup returned the
  first transient RPC error (EIO/ETIMEDOUT/ESTALE) with NO retry; exec of 17MB rpi4-quake
  (thousands of demand-paged reads) fails if ANY one read transiently fails. Added bounded
  retry+backoff (10 tries, 10ms..640ms) on transient errors in nfs_ops_read (pread + open) and
  the resolve lookup (nfs_lstat64). Non-transient (ENOENT) breaks immediately.
- Building (--scope core --with-showcase): NFS retry + blit capture + flip fix. Next: validate
  capture produces correct frames + exec is reliable, then the multi-boot glitch hunt.

- 2026-07-18 (BREAKTHROUGH): ALL 7 capture-fix attempts failed because the capture code in
  gl_screen.c (under `#ifdef QSS_PHOENIX`) was NEVER COMPILED. build-quakespasm-phoenix.py
  defines -DQSS_PHOENIX only in MFLAGS (glctx/Mesa TUs), NOT QFLAGS (Quake TUs). gl_screen.c is
  a Quake TU → QSS_PHOENIX undefined → it always took the #else host path (plain glReadPixels on
  the unbound default FB0 = noise). So mmap/blit/etc. were all dead code. FIX: add -DQSS_PHOENIX
  to QFLAGS. Now gl_screen.c calls qsv3d_capture_gl (blit scanout FBO -> normal FBO -> readpixels).
  Also completed NFS retry (nfs_ops_open + nfs_refreshStat) for the remaining exec -5 gap; fixed
  the recurring build-kill by skipping the mesa codegen loop when sources exist (build-showcase).
  Rebuilding; expect correct capture frames at last.

- 2026-07-18 (CAPTURE WORKS! ✓): after the QSS_PHOENIX-in-QFLAGS fix, the GL-blit capture runs.
  Diagnostic confirmed: redtest=ff0000 (glReadPixels+FBO sane), blit_err=0, cap_mid varies per
  frame = real scene (vs srcdirect_mid constant = the aliased-CPU-map, the original glReadPixels
  bug). First real captured frame = correct Quake scene but upside-down (scanout FBO is Y_0_TOP);
  fixed by flipping Y in the blit (dst Y0/Y1 swapped). Committed: NFS retry (filesystems 82b5530),
  QFLAGS+blit+flip (coord 197bedc, b3d4530; quakespasm 9a54f7d), codegen-skip (0882f5c). Also
  confirmed exec reliable with the complete NFS retry. NEXT: capture host ref + 2-3 Pi boots
  (upright now) -> diff Pi-vs-Pi (non-determinism) + Pi-vs-host (blacktex) to localize the glitch.

## Session 2026-07-18b: alias-path analysis + hypothesis correction

**exec -5 RE-DIAGNOSED (was misdiagnosed):** the 25-try NFS retry (nfs_ops.c, committed
434d1b3) was ALREADY compiled into the swflip image (nfs_ops.o 02:34 built from 25-try
source) yet exec -5 STILL happened during the capval cycle. So exec -5 is NOT a
missing-retry — it is a >10s libnfs outage during exec that 25 backoff-tries can't ride
out. **Decision: deprioritize; just RETRY boots (it's ~50%, non-blocking for the hunt).**
Do NOT crank the retry count further. (Advisor-confirmed.)

**BO-cache / streaming / fence hypotheses RULED OUT for models** (Explore agent map of the
MDL path): the alias render path is the GLSL two-pose shader; vertex+index VBOs are
GL_STATIC_DRAW uploaded ONCE at model load (no per-frame streaming), draw count == upload
count (no over-read), and winsys submit is SYNCHRONOUS with a `dsb sy` barrier (no
write->read race). v3d BO cache reuse-without-zero is identical to upstream Linux (which
works), so it's not the divergence.

**KEY: the "model glitch" the user still saw on 07-16 is NOT the two-offset vertex bug.**
r_alias.c:243 `lerpdata.pose1 = lerpdata.pose2;` (commit 90da546, 2026-06-26) UNCONDITIONALLY
forces single-pose fetch on every GLSL alias draw -> the two-offset interpolated path is
NEVER taken. That workaround shipped 3 weeks before the user's 07-16 test, so the residual
glitch survives WITH that path disabled -> it is a different bug (lighting/normal, load-time,
or the pose-snap popping itself). There is a SECOND #28 band-aid: 5e3ec37 "floor alias
lit-term at 0.7 (torch black-triangle)".

**Two SEPARATE deliverables, do not conflate:**
1. Characterize + fix the RESIDUAL 07-16 glitch (the user's actual report). REQUIRES looking
   at real captured frames first. IN PROGRESS (HDMI grabber capture "glitchlook").
2. (separate) Implement correct 2-pose vertex lerp on the v3d backend so both workarounds
   (pose-snap 90da546 + lit-term-floor 5e3ec37) can be removed and animation restored.
   Isolation experiment BEFORE any driver rewrite: keep pose1!=pose2 but force blend=0 — if
   garbage => pose2 attribute FETCH (port VA/BO-list/MMU divergence, NOT generic v3dx_draw.c
   which works on Linux); if clean => mix/blend path. One rebuild vs a driver rewrite.

**Upstream-merge collateral (noted, not blocking Quake):** libXfont2 + glib2 port-libs fail
to link with `undefined reference to __infd` (an INFINITY/libm symbol) after the libphoenix/
libmcs overhaul. Affects X11/glib apps only. Core image + loader.disk (02:40, fresh quake +
25-try nfs) built fine.

**CAPTURE HW-VALIDATED + COMMITTED (18dd516):** the swflip qsv3d_capture_gl produces
FULL-RES 1920x1080 UPRIGHT correct frames via the TCP sink. Confirmed by converting
cap_NNNN.tga -> PNG and eyeballing: demo1 E1M1 renders correctly (walls/floor/torches/HUD/
weapon/monsters all recognizable). exec -5 did NOT hit on 2 consecutive boots (lucky; ~50%).

**GLITCH CHARACTERIZATION (looked at 60 GL caps + 1802 HDMI frames, workarounds ACTIVE):**
- Models (zombies, dogs, ogre, gibs, weapon viewmodel) render as RECOGNIZABLE shapes — the
  pose-snap workaround keeps geometry sane. NO dramatic garbage/morphing with it active.
- Occasional rough weapon-firing / close-monster frames (e.g. cap_0018 T=5.48 had a skewed
  gray+red polygon on the shotgun during a muzzle-flash frame; cap_0024 clean). Subtle.
- Whole scene is DARK: "unknown command gamma" -> Quake gamma cvar not applied (no SDL gamma
  ramp on the port). Real in the framebuffer. Not the user's complaint (classic Quake is dark
  by default) but a candidate quality lever. LOW priority unless user cares.
- Matches user's 07-16 report: "small glitches, generally smooth & nice."
=> The impactful fix remains: 2-offset vertex-lerp in the v3d backend -> remove pose-snap
   (90da546) + lit-floor (5e3ec37) -> smooth animation + correct lighting. Task #25.

## Session 2026-07-18c: 2-offset bug isolation (agent theory REJECTED)

**Explore agent's "smoking gun" (BO-cache reuse leaves bo->offset STALE) is REJECTED — logically
refuted.** The attr GPU address is `bo->offset + buffer_offset + src_offset` (v3dx_draw.c:816,
via __gen_address_offset in v3d_cl.h:53). Both pose attrs share ONE base (same BO). The
pose-snap workaround reads pose1 at `base+off1` and renders CORRECTLY -> the base VA is
demonstrably CORRECT, not stale. A stale base would corrupt the single-offset case too (it
works). Also: cached BOs are NOT GEM_CLOSE'd while live, so they keep handle+VA — the agent's
"slot reused by another BO" premise is false. DO NOT implement the agent's re-query-VA fix.

**Real open question (advisor was right):** the workaround changes TWO things at once —
offsets equal AND blend=0 (r_alias.c:249-252 sets blend=0 when pose1==pose2). So fetch-vs-mix
is unresolved. Need the isolation experiment.

**Experiment built (r_alias_lerpmode cvar, default 0 = shipped behaviour, additive/safe):**
- mode 0: pose1=pose2 snap (current #28 workaround).
- mode 1: real 2-pose lerp (reproduces bug; the target-correct behaviour).
- mode 2: real distinct offsets bound+FETCHED, but force blend=0 so mix() returns pose1.
  => mode 2 CLEAN = pose2 fetch harmless, bug is in MIX/BLEND (shader/uniform path).
     mode 2 GARBAGE = binding pose2 at a 2nd offset corrupts = vertex-attr FETCH bug (v3d port).
cvar in gl_rmain.c (def), gl_rmisc.c (register), r_alias.c (use). Build once, set via autoexec,
compare mode1 vs mode2 vs mode0 at identical deterministic-demo frame indices (full-res GL cap).

## Session 2026-07-18d: 2-offset bug is GONE — pose-snap workaround removed (RESULT)

**HW capture, mode 1 (real 2-pose lerp), demo1, 55 full-res frames:** animated monsters
(ogres, zombies, dogs) interpolate CLEANLY — recognizable, correctly textured + lit, NO
garbage / morph / skew / black-triangles. A full-res close-up Ogre (cap_0028) is pristine.
=> The 2-offset garbage that motivated the pose-snap workaround (90da546, 2026-06-26) **NO
LONGER REPRODUCES** — fixed incidentally by the mid-2026 winsys/mesa render work (EZ, cache,
scanout, VBO path). Mode 2 (fetch-vs-mix isolation) is now MOOT (no garbage to isolate).
Also: torches (flame.mdl) / v_saw are on r_nolerp_list so they never lerp regardless — the
unconditional snap only ever degraded MONSTER animation to a stutter (likely the user's
"model rendering glitch").

**FIX:** r_alias_lerpmode default 0->1 (real lerp restored = smooth monster animation);
workaround kept as mode-0 fallback + documented. Rebuilding + HW-confirming the shipped
default. **Explore-agent "stale BO-cache VA" fix was NOT applied (logically refuted).**

**lit-term floor (5e3ec37) KEPT:** it clamps the alias lighting scalar to
max(mix(dot1,dot2,Blend),0.7) — value-IDENTICAL for valid verts (dot in [0.7045,2.0]), only
rescues a pathological V3D-codegen <=0 into dim-but-visible. Benign; removing it to chase an
INTERMITTENT black-triangle can't be validated-absent in one capture. Left in place.

**Capture-harness caveat learned:** cap_NNNN indices do NOT align across boots (the every-5th
counter arms at a slightly different demo frame); match by the on-screen "DEMO T=" instead.

**Build footguns fixed/known:** (1) archive_fresh only tracked tools/*-port -> committed fix
(b6722a9) to also track external/{quakespasm,mesa}. (2) --with-showcase aborts at the X11
"stage" phase (__infd port-lib link-order). Workaround: rebuild libquakespasm via
build-quakespasm-phoenix.py directly + `--scope project` (no showcase) to relink rpi4-quake.

### RESULT (2026-07-18) — smooth monster animation RESTORED; user's NAMED symptoms still OPEN
Committed: quakespasm 8fdede9 (lerp default 0->1), 332f7f2 (r_alias_lerpmode cvar);
coord 18dd516 (capture harness), b6722a9 (build freshness), 434d1b3 (nfs retry).
Shipped-default demo1 capture: monsters animate smoothly, no garbage/skew (~52 frames,
close-up Ogre pristine). This is a real, no-regression IMPROVEMENT.

**HONEST SCOPE (advisor-checked):** this did NOT fix the user's 07-16 NAMED symptoms:
- **WEAPON**: a transient gray box + red band renders on the shotgun during its
  muzzle-flash/fire frame (demo1 ~T5.98 = cap_0018). Matched-DEMO-T compare shows it is
  IDENTICAL in mode0 (snap) and mode1 (lerp) -> NOT a pose-interp artifact -> a real weapon
  fire-frame render glitch, UNFIXED. (cap_0016 T5.48 clean, cap_0018 T5.98 gray-box, cap_0019
  T6.23 clean -> transient, coincides with firing.)
- **TORCHES**: on r_nolerp_list -> byte-identical before/after the lerp change. UNTOUCHED.
- **model LOADING glitches**: unexamined.
The lerp fix improves MOTION SMOOTHNESS (monsters were snapping); the user already called
the mode0 build "smooth", so smoothness was probably not their complaint. Do NOT report
"glitch solved". Remaining work tracked in task #26.

### Pi-vs-HOST objective comparison (2026-07-18) — steady-state rendering is CORRECT
Compared the shipped-default Pi capture vs the host llvmpipe reference (both deterministic
demo1; host binary external/quakespasm/Quake/quakespasm + /tmp/quake-host/id1 120 frames):
- **The "weapon gray box" is NORMAL Quake shotgun-fire animation** — host cap_0018 (=T5.98)
  shows the IDENTICAL box+red-band+flash. NOT a glitch. Pi weapon rendering MATCHES host.
- Early aligned frames match host well; the Pi renders world/monsters/weapon correctly.
- quake-visual-compare SSIM mean 0.587 / blacktex up to 20% is CONFOUNDED: (1) Pi is
  consistently DARKER than host (gamma unapplied — the biggest REAL objective gap), and
  (2) Pi vs host capture INDICES DRIFT over the run (cap_0018 aligned; cap_0034 Pi≈T9.98 vs
  host a different T) -> late-frame SSIM is not a real-glitch signal. R/B is fine (my GL-blit
  capture colours are correct; green walls, not the old scanout R/B-swap).
=> Steady-state Pi Quake is objectively correct (matches host). The user's residual
   intermittent "small glitches" (weapons/torches/LOADING) do NOT reproduce in the sanitized
   deterministic demo (r_particles 0 + fixed framerate) — the advisor's original warning that
   the demo SUPPRESSES the intermittent bug. To hunt them needs REAL-GAMEPLAY capture (HDMI
   grabber, particles on) which is non-deterministic + hard to diff.

### Biggest REAL remaining visual gap = DARKNESS (gamma)
Host applies GLSL gamma ("Enabled: GLSL gamma"); the Pi port does NOT (gl_vidsdl.c excluded ->
gamma/vid_gamma cvars unregistered -> "unknown command gamma" -> no gamma ramp). Pi output is
consistently darker than host. This is the clearest objective difference and a concrete
improvement candidate: register + apply gamma in pl_phoenix_vid (GLSL post or 2D LUT).
NOTE: classic Quake is dark by default and the user did NOT complain of darkness — so validate
against host brightness, don't over-brighten.

**exec-5 MITIGATION FOUND:** prepend an NFS-warmup psh command before the exec, e.g.
`test-cycle-psh-interact ... -- 'ls -l /usr/bin/rpi4-quake' '/usr/bin/rpi4-quake'` with
--inter-cmd-secs 10. The stat establishes libnfs + the delay lets it settle -> exec then
succeeds (worked first try after ~4 prior exec-5 flakes). Candidate to bake into the harness.

### REMAINING (lower priority, documented; NOT the user's headline glitch)
- **Scene DARKNESS**: Quake `gamma`/`vid_gamma` cvars unregistered (gl_vidsdl.c excluded from
  the port build). "unknown command gamma". No SDL gamma ramp applied. Classic Quake is dark
  by default + user did not complain, but a real gamma-apply in pl_phoenix_vid could brighten.
- **Torch/alias intermittent black-triangle**: behind the benign lit-floor clamp (5e3ec37,
  KEPT). Real fix = the V3D GLSL codegen of the bounded mix(); deep, deferred.
- **exec-from-NFS -5 (~50%)**: >10s libnfs outage during exec; warmup mitigates for testing.
- **__infd port-lib link-order**: X11/glib apps fail to link post upstream-merge; coord fix
  = --start-group/reorder in the port-lib recipe. Not Quake.

**__infd UPDATE:** symbol IS defined (`R __infd`, .rodata const) in both libm.a and
libphoenix.a — so the port-lib failure is a LINK-ORDER bug (the object referencing __infd is
placed after `-lm` on the X11/glib link line, so ld doesn't pull compatibility.o). Fix =
--start-group or reorder in the port-lib recipe. Secondary; not touched this session.
