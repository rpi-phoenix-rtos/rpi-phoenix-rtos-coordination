# Quake Pi4-vs-host visual regression harness — design + status (2026-06-15)

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
- [ ] **Capture a water/start-map scene** to localise the "objects black instead of textured" bug.
- [ ] (separate NFS-stability track) root-cause + fix the nfs-fs VFS large-write hang.

## End-to-end run (once TCP sink lands)
1. `scripts/quake-host-capture.sh` -> host `cap_*.tga` in /tmp/quake-host/id1
2. Pi netboot with capture autoexec (scr_capture_host=<host>) -> frames stream to the host listener dir
3. `.venv-quakecmp/bin/python scripts/quake-visual-compare.py --pi <pidir> --host /tmp/quake-host/id1`
   -> CSV + montages localising the black-object / text bugs (the evaluation criteria).
