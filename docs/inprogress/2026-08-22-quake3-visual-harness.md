# quake3 (quake3e) Pi4-vs-host visual-regression harness — host reference (2026-08-22)

Extends the Quake1 visual-regression harness
(`docs/inprogress/2026-06-15-quake-visual-regression-harness.md`) to **quake3**
(engine: `external/quake3e`, ec-/quake3e). HOST-ONLY work here: this produces the
deterministic **host reference** capture that a future Pi capture pairs against
frame-for-frame. The comparator `scripts/quake-visual-compare.py` is game-agnostic
and is reused as-is (validated below).

## Capture mechanism — `video`, NOT `cl_avidemo`

The task brief assumed quake3e exposes a per-frame-TGA `cl_avidemo <fps>` (that is
**vanilla ioquake3**). ec-/quake3e **dropped it** — `grep cl_avidemo` finds only
`cl_aviFrameRate` / `cl_forceavidemo`. quake3e's built-in per-frame capture is the
**`video` command** (AVI writer, `code/client/cl_avi.c`):

- While an AVI is recording, `CL_Frame` (`cl_main.c:3028-3046`) **forces a fixed
  timestep**: `msec = 1000/cl_aviFrameRate`, capturing exactly one frame per
  `Com_Frame`. This is the q3 equivalent of Q1's `host_framerate <dt>` — the demo
  advances a constant amount of demo-time per captured frame regardless of
  wall-clock, so **frame N is the same demo moment on every machine**. No
  `com_fixedtime`/`timescale` needed (and `fixedtime` is CVAR_CHEAT anyway).
- `cl_aviMotionJpeg 0` → **uncompressed raw BGR AVI** (lossless; MJPEG would wreck
  the `blacktex%` metric).
- `cl_forceavidemo 0` (default) → frames captured only while `cls.state ==
  CA_ACTIVE`, so both machines begin at demo snapshot 0 and the variable-length
  load frames (the Pi loads slower over NFS) are **excluded** — this is what keeps
  the frame indices aligned for pairing.

Host then splits the AVI into `cap_%04d.tga` with **ffmpeg** (`bgr24` → RGB TGA,
0-indexed to match Q1's `cap_0000` naming). The Pi build already compiles the same
path (`cl_avi` in the CLIENT TU list; `RE_TakeVideoFrame`/`RB_TakeVideoFrameCmd` in
`tr_init.c`, part of the REND1 list) — **no engine patch, symmetric host/Pi.** The
FBO/"minimized" gate at `tr_init.c:1124` guards only the interactive `screenshot`
command, **not** the video path, so headless offscreen capture is unaffected.

## Determinism cvars (the q3 analogue of Q1's `r_particles 0`)

Cross-arch `rand()` (libphoenix vs glibc) desyncs client-side effects. The demo
stream itself (entity positions/events replayed from the `.dm_68`) is fixed; only
the cgame-spawned effects use `rand()`. Disabled in `autocap.cfg`:

```
set cg_marks 0        # impact/explosion decals (random spread+orientation)
set cg_gibs 0         # gibs (random velocities)
set cg_brassTime 0    # ejected shell casings (random)
set cg_drawFPS 0      # wall-clock-dependent HUD text
set cl_aviMotionJpeg 0
set cl_aviFrameRate 25
set cl_forceavidemo 0
```

## The demo: a committed recorded demo, NOT the shipped `.dm3`

The 1999 demo-release demos in `demoq3/pak0.pk3` (`demos/demo001.dm3`,
`demo002.dm3`) use an **old protocol quake3e refuses**: `DEMOEXT` is `dm_<proto>`
and `demo_protocols[]` is `{66,67,68,71,...}`; `CL_PlayDemo_f` cannot resolve
`.dm3` (`"Protocol not supported for demos"`). So per fallback option #3 of the
brief, the reference camera path is a **protocol-68 demo recorded once on host**:

- **Map:** `q3dm7` (the map used in the Pi V3D lightmap work — good textured
  gothic architecture + lightmaps + HUD).
- **Recording:** `devmap q3dm7` + 2 bots (sarge, major), player switched to
  `team spectator` + `follow sarge` → the camera **follows a moving bot**, giving
  real camera motion through varied scenes (not a static viewpoint).
- Recorded file committed at **`tools/quake3-port/demos/cap.dm_68`** (11 KB,
  md5 `850e1f4751108dc08d60e0219d9816b8`). The recording's own bot-AI randomness
  is irrelevant — the **file is fixed**, and both host and Pi replay the identical
  bytes, so playback is deterministic.
- To regenerate (changes the reference — must re-stage to the Pi): the `rec.cfg`
  sequence is in git history of this work; `record cap` on `q3dm7` with 2 bots in
  spectator-follow.

## Result — host capture WORKS

`scripts/quake3-host-capture.sh` (default 1024×768, `+wait 800`):

- **199 frames**, `cap_0000.tga`..`cap_0198.tga` in `/tmp/quake3-host/cap/`.
- Frames are **non-black and recognizable**: q3dm7 gothic stone/metal
  architecture, correct textures and **correct colors** (orange sky, brown stone,
  the flame weapon-model bottom-right, white HUD) — **no BGR↔RGB swap**. HUD shows
  `following Sarge`, health/ammo, the Sarge face icon.
- **Determinism proven**: two independent host runs → `quake-visual-compare.py`
  reports **SSIM mean 1.000 / min 1.000, blacktex% 0.000, MAE 0.0** across all 199
  pairs (bit-identical). Confirms the fixed-timestep video path is fully
  deterministic and that the game-agnostic comparator pairs q3 frames correctly.
- **Landmarks** for the future Pi run to sanity-check index alignment: `cap_0010`
  = overhead view of a stepped courtyard with a Gauntlet pickup; `cap_0100` = wide
  hall with a central staircase and the orange sky; `cap_0198` = demo end (screen
  ~45% dark, disconnect fade).

## Renderer must match: opengl1 both sides

The host binary is built `USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=opengl` →
fixed-function **opengl1** folded in, the **same renderer** `tools/quake3-port`
builds for the Pi (`RENDERER_DEFAULT=opengl`). opengl2 would produce different
images and break the comparison.

## How to regenerate the host reference

```
scripts/quake3-host-capture.sh
# env knobs: QUAKE3_HOST_DIR (/tmp/quake3-host), QUAKE3_WIDTH/HEIGHT (1024/768),
#            QUAKE3_FPS (25), QUAKE3_WAIT (800 game-frames), QUAKE3_PAK_SRC
# -> host frames in $QUAKE3_HOST_DIR/cap/cap_*.tga
```

## EXACTLY what the Pi side needs to reproduce the same frames

The Pi engine binary already links the capture path — nothing to rebuild. Steps:

1. **Stage the demo** into the export so ram-stage picks it up:
   `cp tools/quake3-port/demos/cap.dm_68
      /srv/phoenix-rpi4-nfs/usr/share/quake3/demoq3/demos/cap.dm_68`
   (see the netboot export-drift memory: the NFS root is hand-maintained).
2. **autocap.cfg** identical to the host one above (same six determinism cvars +
   same `cl_aviFrameRate`), placed in the staged `demoq3/`.
3. **Launch** with the same demo + video sequence and a **writable tmpfs
   homepath** (the AVI is written to `fs_homepath/demoq3/videos/`):
   ```
   quake3e +set fs_game demoq3 +set fs_homepath /tmp/quake3 \
           +set r_customwidth 1024 +set r_customheight 768 +set r_mode -1 \
           +set s_initsound 0 +exec autocap.cfg \
           +demo cap +video +wait 800 +stopvideo +quit
   ```
   The `/usr/bin/quake3` launcher already sets `fs_basepath /tmp/quake3` +
   `fs_game demoq3` and forwards extra argv, so
   `quake3 +set fs_homepath /tmp/quake3 +exec autocap.cfg +demo cap +video +wait 800 +stopvideo +quit`
   works if `cap.dm_68` and `autocap.cfg` are in the RAM-staged tree; otherwise
   invoke the engine directly with the args above.
4. **Pull the AVI to the host** (NFS/SD **reads** are fine — only large *writes*
   hit the nfs-fs stall) and run the **same** ffmpeg split into a Pi `cap/` dir:
   `ffmpeg -y -i video0000.avi -start_number 0 pi_cap/cap_%04d.tga`.
5. **Compare:**
   `.venv-quakecmp/bin/python scripts/quake-visual-compare.py
      --pi pi_cap --host /tmp/quake3-host/cap --out artifacts/quake3-compare`.

### Pi transport caveat (size) — DO size this before running the Pi

Uncompressed 1024×768 = **~2.36 MB/frame → ~469 MB for 199 frames**. That must go
to a **writable tmpfs (`/tmp`)**, never a direct NFS write (the AVI would trip the
open `nfs-fs` VFS large-write stall documented in the Q1 harness doc). 469 MB of
tmpfs on a 4 GB Pi is feasible but tight. Two levers, applied **identically on
host** (re-run the host script with the same values so indices/resolution pair):

- Drop resolution to **640×480** (`QUAKE3_WIDTH=640 QUAKE3_HEIGHT=480`, and the
  matching `r_customwidth/height` on the Pi) → ~0.9 MB/frame (~180 MB) — the safe
  default for the first Pi run.
- Shorten with a smaller `+wait` (fewer frames).

If AVI-in-RAM proves infeasible on the Pi, the **fallback** is the Q1-style
per-frame-TGA + TCP-sink transport, which for quake3e would require porting the Q1
`scr_capture` hook into the engine (an engine patch under `external/quake3e`) since
quake3e has no per-frame-TGA built-in. Prefer the AVI-in-tmpfs path first.

## Status

- [x] Host quake3e opengl1 build (native, headless SDL-offscreen + llvmpipe).
- [x] Deterministic capture mechanism identified + validated (`video` fixed
      timestep; 199 frames, host-vs-host SSIM 1.000).
- [x] Reference demo recorded + committed (`tools/quake3-port/demos/cap.dm_68`).
- [x] `scripts/quake3-host-capture.sh` (fail-loud, parameterized).
- [ ] Pi-side capture run (needs HW; recipe above) → first q3 Pi-vs-host bug report.
