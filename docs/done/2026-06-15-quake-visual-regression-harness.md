# Quake Pi4-vs-host visual regression harness — design + status (2026-06-15)

> **STATUS (2026-06-26): still ACTIVE.** The deterministic Pi-side capture is DONE +
> validated (120 shots); the host llvmpipe reference run + the SSIM/black-texture/text
> comparison script remain PLANNED (unrun). The NFS large-write stall documented below is
> still open — the stress suite confirmed it does NOT trigger at a 4 MB write but the deeper
> nfs-fs VFS-write bug stands (see `docs/done/2026-06-26-stress-test-results.md`). No change since
> 2026-06-15; the TCP-sink transport workaround is the path forward when this resumes.

Goal: systematic, automated, **unattended** comparison of Quake's visual output
on Pi4 (V3D) vs a host reference (llvmpipe software GL), to localise the small
visual bugs (objects/parts rendering black instead of textured; broken text).
Not pixel-perfect — the deliverable is clear, per-frame **evaluation criteria**
for what's still wrong.

## Approach

**Determinism via fixed timestep.** `host_framerate <dt>` (host.c, pre-existing)
forces a constant frametime per rendered frame regardless of wall-clock, so a
demo advances deterministically. With `r_particles 0` (cross-arch `rand()` —
libphoenix vs glibc — would otherwise desync particles), **frame N shows the
same demo moment on every machine**. The same demo (demo1...) on both → directly
comparable frame sets.

**Capture mode (DONE, committed external/quakespasm in gl_screen.c).** New cvars:
`scr_capture` (dump every Nth in-game demo frame), `scr_capture_max` (auto-quit
after N), `scr_capture_dir` (output dir). `SCR_CaptureTick` (SCR_UpdateScreen,
pre-swap) dumps the composed frame to `cap_NNNN.tga` (self-contained 24-bit TGA
writer — no libpng, no com_gamedir dep). Driven by an `id1/autoexec.cfg`:
```
host_framerate 0.05
r_particles 0
scr_capture_dir "/nfstest/id1"   # Pi: straight onto the NFS export
scr_capture 5
scr_capture_max 120
```
**Validated on Pi**: 120 shots, demo timestamps exactly 0.25 s apart (5 frames ×
0.05), clean auto-quit — fully deterministic.

## Transport: Pi → host

Pi writes `cap_NNNN.tga` to `/nfstest/id1` = `/srv/phoenix-rpi4-nfs/id1` on the
host (the host reads them directly — no separate copy). The host capture run
writes to a separate local dir to avoid collision.

**BLOCKER (open, DISENTANGLED 2026-06-15): the bug is the nfs-fs VFS-write
bridge, NOT libnfs or the network.** Isolation test (nfs-smoke BIGWRITE: 512 KB
via direct libnfs `nfs_pwrite` ×16 on one fh) → **works, 5.68 MB/s**. The same
data via the nfs-fs VFS path (Pi `fwrite` → kernel → `nfs_ops_write`) → **stalls
after the first 4 KB write**. Instrumenting `nfs_ops_write`: the 1st write
(off=0, 4 KB) returns rc=4096 OK; the 2nd `nfs_pwrite` (off=4096) **never
returns** (hangs inside libnfs's reconnect loop → the `getservbyport`×25739
flood from the reserved-port re-bind, socket.c:1515). nfs-fs is **single-
threaded** here (`nfs_loopThread`; the 2nd thread is a one-shot mount/splice
helper) so it is NOT a concurrency race. The fh IS cached (`nfs_ops_open`:222,
owned=0). So: libnfs leaves the connection in a state after the 1st nfs-fs write
where the next op reconnect-hangs — but the identical libnfs call sequence via
nfs-smoke is fine. Root is a subtle libnfs/nfs-fs write-state interaction
specific to the nfs-fs-opened fh (vs nfs-smoke's `nfs_creat` fh) — deep, 3
sessions in, not yet cracked. Real NFS-write-stability gap (also affects saves).

**Transport decision: TCP sink** (don't keep blocking on the nfs-fs-write bug).
The Pi capture opens a TCP socket to a host listener and streams the TGA bytes
(length-prefixed per frame); the host listener writes `cap_NNNN.tga`. Uses the
proven-fast lwip TCP (reads do 8.5 MB/s), bypasses the buggy VFS-write path
entirely. The nfs-fs-write bug stays documented as a separate NFS-stability item.

## Host reference run (planned)

Host can build quakespasm (SDL2 2.32.10 + llvmpipe `swrast_dri.so` present).
Headless options: `Xvfb` (needs `sudo apt install xvfb`) + `LIBGL_ALWAYS_SOFTWARE=1`,
or SDL `offscreen`/EGL surfaceless. Build the same capture-enabled tree, run the
same demo + autoexec (no `scr_capture_dir` → local id1), collect `cap_*.tga`.

## Comparison script (planned, host Python via uv venv)

Per frame pair `(pi_i, host_i)`: load TGAs → numpy; metrics:
- **SSIM** + mean-abs-error (overall similarity).
- **Black-texture signature**: pixels near-black on Pi but textured on host →
  count + bounding boxes (the "object rendered black" bug). The headline metric.
- **HUD/text region** cropped + compared separately (the text-rendering bug).
Output: per-frame CSV + a montage (side-by-side + diff heatmap) of the worst
pairs. Evaluation criterion = #black-texture pixels and text-region SSIM trend.

## Status / next

- [x] Deterministic capture mode (committed, Pi-validated: 120 shots, exact 0.25 s spacing).
- [x] Transport root-cause disentangled: nfs-fs VFS-write bridge bug (libnfs + net are fine).
- [x] **Host reference run** — `scripts/quake-host-capture.sh`: builds quakespasm natively
      + runs HEADLESS via **SDL offscreen + llvmpipe** (no X/Xvfb needed!), deterministic.
      Validated: 20 frames, timestamps matching the Pi (cap_0010 @ demo t=3.98), correct render.
- [x] **Comparison script** — `scripts/quake-visual-compare.py` (`.venv-quakecmp`): per-frame
      SSIM, MAE, blacktex% (textured-on-host/black-on-Pi = the "black object" bug), HUD-strip
      SSIM; CSV + [Pi|host|diff] montages. Validated host-vs-host (SSIM 1.000, blacktex 0.000).
- [x] **TCP sink (DONE, committed `gl_screen.c` + `scripts/quake-capture-sink.py`)**: Pi capture
      streams `[u32 idx][u32 tgalen][TGA]` over lwip TCP to a host listener (bypasses the nfs-fs
      write bug). Quake's socket()/connect()/send() link + work on Phoenix. HW-proven: 40 frames
      streamed, cap_0000 @ t=1.48 matching host.
- [x] **FIRST PI-vs-HOST BUG REPORT (2026-06-15)** — 40 frame pairs, full pipeline ran end-to-end:
      ```
      SSIM mean=0.848 min=0.123   blacktex% mean=0.266 max=0.568
      ```
      **Headline finding: the 3D WORLD matches the host** (diff panel ~black over all geometry;
      steady-state SSIM ~0.958) — textures, lightmaps, geometry, depth all correct. **The dominant
      difference is TEXT/HUD rendering** (diff panel bright-red exactly on the top centerprint + the
      bottom sbar; hud_ssim ~0.91). Characterised quantitatively (cap_0011 top text strip):
      host text = sharp NEAREST peaks (max lum 171, 347 px>120); Pi text = LINEAR-spread (max 155,
      565 px>120, same foreground brightness ~106-112). **= V3D applies LINEAR filtering to the
      charset where quakespasm requests `GL_NEAREST`** (gl_draw.c:362 TEXPREF_NEAREST →
      gl_texmgr.c:95 GL_NEAREST). The half-texel linear peak-shift is what `blacktex%` flags. World
      textures filter correctly (linear, as intended) so the sampler path works in general — the
      NEAREST case specifically is not honored. **This IS the user's "text rendering broken".**
      The "objects black instead of textured" report is NOT strongly present in this corridor demo
      (blacktex here is mostly text halos) — needs a water/start-map capture to reproduce.
      Evaluation criteria established: per-frame SSIM (world), hud_ssim (text), blacktex% (black bug).
- [ ] **FIX: V3D NEAREST sampler not honored** (the text bug) — investigate why the charset's
      per-texture GL_NEAREST doesn't reach the V3D sampler descriptor (sampler encoding in
      `v3dx_state.c:601` is correct upstream; world linear works → suspect FF/st sampler-atom or
      a shared/default sampler in the GL-frontend port). Re-run harness to verify (target hud_ssim ~1).
- [x] **Wide capture (100 frames / all 3 demos, ~100 s, default filtering) to hunt black objects (2026-06-15).**
      WORLD-region blacktex mean=0.017% max=0.92% — **the world renders correctly across every demo
      scene** (montages: cap_0050 textured stone hall + shotgun + ammo matches host pixel-for-pixel;
      the two >0.5% outliers are cap_0000 demo-start transition + cap_0014 a rocket-explosion sprite
      that's a frame off in position — both render fine, neither is a black object). **The user's
      "objects black instead of textured" bug DOES NOT reproduce in deterministic demo playback.**
      → strongly implicates **particles** (we force `r_particles 0` for determinism; particle quads/
      points may render black on V3D = the "small random visual bugs") OR interactive-only scenes/
      angles. NEXT: capture with `r_particles 1` and eyeball the Pi frames for black specks (no host
      pairing needed — particles are rand()-placed; just need "colored vs black").
- [x] **`r_particles 1` test (2026-06-15)** — captured 60 frames with particles ON; diffed vs the
      particles-OFF run at the same demo moments (both EVERY=5 from t=1.48). Particles **render
      correctly**: cap_30 (t=8.98) shows a dark-red blood/gib cloud near a monster (correct — blood
      is dark red; mean lum 53 because blood IS dim, not because it's a black-texture failure). So
      **particles are not the black-object bug either.**
- [x] **VERDICT: the user's reported bugs appear ALREADY RESOLVED.** Their screenshot was timestamped
      2026-06-14 20:32, which pre-dates that day's gray-world back-face-culling fix (coord e3c43ca)
      and the `r_oldwater` water fix (ff17470). The harness now confirms across all reproducible
      (demo) scenes that the world, water, entities, weapons, ammo, explosions, and particles **all
      render correctly + match the host** — the "objects black instead of textured" (= the old
      culling/gray-world bug) and "water looks strange" (= the old warpimage RGB-noise) symptoms do
      not reproduce in the current build. The one genuine residual is the 2D-text mag-filter softness.
- [x] **TEXT "BUG" ROOT-CAUSED + FIXED — it was stale config, NOT a V3D driver bug (2026-06-15).**
      Decisive zero-cost test on the existing captures: measured the text bounding box — Pi=445×47px
      vs host=276×30px = the Pi rendered 2D **1.6× larger**. 1024/1.6=640: the NFS export's
      `config.cfg` (written by a prior session, Jun 13) had **`scr_conscale "1.6"`**. gl_screen.c:391
      (included in the Pi build) computes `vid.conwidth = vid.width/scr_conscale = 640`, so 2D text
      draws at `glwidth/conwidth = 1.6×`. Non-integer 1.6× scaling of the 8×8 bitmap font is the
      "blurry/broken text". The host has no config.cfg → default `scr_conscale 1` (crisp 8px). **The
      published/fresh build uses the default too → already correct; this was purely stale local
      config, no code defect.** FIX: set the export config.cfg back to `scr_conscale "1"`.
      VALIDATED via re-capture: Pi text 445×47 → **278×31px, matching host 276×30**; full harness
      comparison SSIM mean 0.848→**0.875**, blacktex% 0.266→**0.189**, steady-state frames now
      **SSIM 0.98 / MAE 0.7** (near-perfect host match). My earlier "V3D mag-NEAREST driver bug"
      hypothesis is RETRACTED. **User guidance: use an INTEGER `scr_conscale` (1 or 2) on V3D — a
      non-integer value blurs the bitmap font (true on any GL, but visible here).** A faint residual
      softness remains at 1:1 (minor, deferred) but the text now matches the host size + layout.
- [x] **CONCLUSION: all reported Quake visual issues are now resolved or explained.** World/water/
      entities/particles render correctly (matches host); "black objects"+"strange water" were the
      pre-2026-06-14 cull/warpimage bugs (fixed); "broken text" was stale scr_conscale 1.6 (fixed).
      The harness + evaluation criteria (SSIM/hud_ssim/blacktex%) are the durable deliverable.
- [ ] (separate NFS-stability track) root-cause + fix the nfs-fs VFS large-write hang.

## End-to-end run (once TCP sink lands)
1. `scripts/quake-host-capture.sh` -> host `cap_*.tga` in /tmp/quake-host/id1
2. Pi netboot with capture autoexec (scr_capture_host=<host>) -> frames stream to the host listener dir
3. `.venv-quakecmp/bin/python scripts/quake-visual-compare.py --pi <pidir> --host /tmp/quake-host/id1`
   -> CSV + montages localising the black-object / text bugs (the evaluation criteria).
