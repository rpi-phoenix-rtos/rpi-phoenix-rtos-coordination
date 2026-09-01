# Quake2 (yQuake2 ref_gl1) Pi4-vs-host visual regression harness (2026-08-22)

Extends the Quake1 harness (`docs/inprogress/2026-06-15-quake-visual-regression-harness.md`)
to **yQuake2** (`external/yquake2`, `ref_gl1` renderer, the Phoenix single-ELF port
in `tools/yquake2-port/`). Same idea: deterministic fixed-timestep demo playback so
frame N is the same demo moment on host (llvmpipe software GL) and Pi (V3D); dump
paired `cap_NNNN.tga`; compare with the game-agnostic `scripts/quake-visual-compare.py`.

## STATUS: host reference DONE + self-verified. Pi capture PENDING (needs a Pi cycle).

- [x] Capture hook added + committed to `external/yquake2` (commit `ea5d7ae`, on top of
      pinned `YQ2_SHA e27fdcce`).
- [x] `scripts/quake2-host-capture.sh` — builds native yQuake2 headless, runs the demo,
      produces 120 deterministic `cap_*.tga`. **Verified**: recognizable scene, camera
      advances, byte-identical across two runs.
- [x] Pi build inherits the hook automatically (same `external/yquake2/src`, compiled by
      `tools/yquake2-port/build-yquake2-phoenix.py`, now with `-DYQ2CAP_PHOENIX`).
- [ ] Pi capture run + `quake-visual-compare.py` pairing (future Pi cycle — HOST-only here).

## Determinism model (yQuake2 differs from Q1)

Q1 used `host_framerate <dt>`. yQuake2 has no such cvar; the equivalent is a **pair**:

| cvar | value | effect |
|------|-------|--------|
| `timedemo` | `1` | mainloop renders one frame per iteration, **no wall-clock throttle** (`common/frame.c` skips the `renderdelta < 1/rfps` cap). Decouples from machine speed. |
| `fixedtime` | `50000` | `common/frame.c:524` forces per-frame `usec = fixedtime` → `cl.time` advances a **fixed 50 ms/frame** regardless of how long the frame actually took. This is the real determinism knob (yQuake2's `host_framerate` analog). |
| `cl_particles` | `0` | particles are `rand()`-placed; libphoenix vs glibc RNG would desync them across machines. Off = deterministic. |
| `con_notifytime` | `0` | console-notify text (incl. the hook's own `CAPTURE:` lines) expires immediately so it does not linger across ~60 frames as on-screen text. |

`timedemo 1` + `fixedtime 50000` ⇒ each rendered frame = exactly 50 ms of demo time,
identical on every machine. **Self-check passed**: two independent host runs produced
**byte-identical** `cap_*.tga` (llvmpipe is deterministic; the cvars kill the RNG paths).

## Capture hook (committed `external/yquake2`, commit ea5d7ae)

Mirrors the Q1 `SCR_CaptureTick`/`SCR_CaptureFrame` design but lives in the **renderer**
(`src/client/refresh/gl1/gl1_sdl.c`, `YQ2_CaptureTick()` called from `RI_EndFrame` after
`R_ApplyGLBuffer`, before `SDL_GL_SwapWindow` → captures the full composed frame incl. HUD):

- Cvars (via `ri.Cvar_Get`, so both the client autoexec and `+set` reach them):
  `scr_capture` (dump every Nth in-game frame), `scr_capture_max` (auto-`exit(0)` after N),
  `scr_capture_dir` (output dir, default `.`).
- Self-contained 18-byte uncompressed 24-bit **BGR bottom-up** TGA writer — byte-identical
  format on host and Pi, no libpng/screenshot-path dependency. `glReadPixels(GL_RGB)` is
  bottom-up which matches TGA descriptor 0, so no row flip; RGB→BGR swap in place.
- **In-game-frame gate**: the renderer can't see the client's `cls.demoplayback`, so
  `RI_RenderFrame` (gl1_main.c) latches a flag `yq2cap_scene_rendered` that the hook checks
  and clears. Only frames where the 3D world actually rendered are counted → **index 0 ==
  first demo world frame on every machine** (load/menu 2D-only frames are skipped). This is
  the single most important correctness property for cross-machine pairing.
- **Pi readback (`-DYQ2CAP_PHOENIX`)**: Phoenix/V3D renders into a scanout-backed FBO
  (`sdl_phoenix_glctx.c`), not FB0, so a plain `glReadPixels` there returns noise (same
  issue Q1 hit). Under the define the hook reads back via `phxgl_capture_gl()` (a GPU blit
  to a normal FBO, then readback), exactly as Q1's `QSS_PHOENIX` path. The native host build
  (yQuake2 Makefile, renders into FB0) does **not** define it and uses plain `glReadPixels`.
  **This path is a direct port of proven Q1 code but is UNVERIFIED on Pi (host-only task).**

## Demo used

`demos/q2demo1.dm2`, shipped **inside** `baseq2/pak0.pak` (the retail/demo Quake II pak;
the pak also defines `alias d1 "demomap q2demo1.dm2"`). It records map **demo2** (a Base
Unit area). Launch straight into it with `+demomap q2demo1.dm2` (no attract-mode loop, so
no different-map frames sneak in). The demo ends with a scripted **player death** at ~frame
114; frames 115–119 are the near-static death-cam (still part of the single deterministic
demo — no loop into another map). Full playthrough at 50 ms/frame × (120 shots × every-5) =
600 rendered frames = 30 s of demo, which spans the whole q2demo1.

## Host capture — how to regenerate

```
bash scripts/quake2-host-capture.sh          # -> 120 cap_*.tga in /tmp/quake2-host/
# knobs: NSHOTS EVERY FIXEDTIME WIDTH HEIGHT DEMO QUAKE2_HOST_DIR PAK0_SRC
```

It builds a **clean detached git worktree** of `external/yquake2` at HEAD in
`/tmp/yq2-host-src` (the main working tree carries the Phoenix port patch uncommitted, which
would break a native `.so`-dlopen build — vid.c renderer gate, dropped forwarders). Builds
`WITH_SDL3=no WITH_CURL=no WITH_OPENAL=no` (client + game + ref_gl1) and runs headless via
`SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy` — no X/Xvfb needed.

### Host result — VERDICT: render is CORRECT

120 frames, `1024×768`, non-black (mean lum ~15–41 across scenes), avg inter-frame MAE 18.5
(camera clearly advancing), byte-identical across reruns. Eyeballed cap_0000 (warehouse:
crates, blaster viewmodel, an enforcer, HUD health 100 — textbook Q2), cap_0060 (corridor,
grenade viewmodel, dead body, health 52), cap_0119 (death-cam). Textures, lightmaps,
geometry, HUD all render correctly.

**Known cosmetic**: frames 0–2 carry a **host-only** SDL startup warning ("Setting Relative
Mousemode failed… update to SDL 2.0.3") in the top ~22 px console-notify strip (fades by
frame 3; won't appear on the Pi). When comparing, skip frames 0–2 or mask the top 22 px.

## Pi-side reproduction recipe (future Pi cycle)

The Pi build already inherits the hook (verified by reading
`tools/yquake2-port/build-yquake2-phoenix.py`: it compiles `external/yquake2/src` — including
the committed `gl1_sdl.c`/`gl1_main.c` hook — and now passes `-DYQ2CAP_PHOENIX` on the GL1
TUs so the scanout-FBO `phxgl_capture_gl` readback path is active). Rebuild the port ELF and
deploy it + `baseq2/pak0.pak` to the NFS/SD root, then launch with the SAME determinism cvars.
Two ways to drive it:

1. **Command line** (mirrors the host script):
   ```
   quake2 +set vid_renderer gl1 +set s_initsound 0 \
     +set r_mode -1 +set r_customwidth 1024 +set r_customheight 768 \
     +set cl_particles 0 +set con_notifytime 0 +set fixedtime 50000 +set timedemo 1 \
     +set scr_capture 5 +set scr_capture_max 120 +set scr_capture_dir /nfstest/q2cap \
     +demomap q2demo1.dm2
   ```
2. **`baseq2/autoexec.cfg`** (if launching via psh without args):
   ```
   set r_mode -1
   set r_customwidth 1024
   set r_customheight 768
   set cl_particles 0
   set con_notifytime 0
   set fixedtime 50000
   set timedemo 1
   set scr_capture 5
   set scr_capture_max 120
   set scr_capture_dir "/nfstest/q2cap"
   demomap q2demo1.dm2
   ```
**MANDATORY: matched resolution.** `quake-visual-compare.py` pairs by index and needs
identical WxH; the host reference is 1024×768 (`r_mode -1` + custom w/h). The Pi launch
**must** set the same three cvars. If the Phoenix SDL2/fbdev backend cannot honor
1024×768 on the Pi (unverified here — no Pi run), instead set the Pi's actual capture
resolution and re-run the host script with `WIDTH`/`HEIGHT` overridden to match it. Q1
used 1024×768 on both host and Pi, so that is the intended target.

`scr_capture_dir` should point at a host-visible export (as with Q1) or, if the NFS-write
bug bites, add a TCP-sink like Q1 (`scr_capture_host`) — not needed for host-only work.

### Then compare (game-agnostic, reused as-is)
```
.venv-quakecmp/bin/python scripts/quake-visual-compare.py \
    --pi <pi-cap-dir> --host /tmp/quake2-host   # skip 0-2 / mask top strip
```
→ per-frame SSIM (world), hud_ssim (HUD), blacktex% (black-object bug), + montages.

## Risks / open for the Pi run
- The `-DYQ2CAP_PHOENIX` `phxgl_capture_gl` branch in `gl1_sdl.c` is **both compile- and
  runtime-unverified**: the native host build only compiles the `#else` `glReadPixels`
  path, and a Phoenix cross-compile of `gl1_sdl.c` needs `libSDL2.a`/the SDL2 port headers
  (not built here). `gl1_main.c` (the gate one-liner) DID cross-compile clean. The branch
  is trivial C mirroring `gl1_misc.c`, and `phxgl_capture_gl` is confirmed present in the
  linked glue — low risk — but the next Pi cycle should watch for a build error there
  first, then for noise/black frames (would mean the scanout readback needs adjusting;
  Q1's identical glue worked, so expected OK).
- **The hook lives only as a local commit (`ea5d7ae`) on top of pinned `YQ2_SHA` in the
  gitignored `external/yquake2` clone.** A fresh `git clone yquake2 && checkout YQ2_SHA`
  (per the port README) gets the Phoenix port patch but NOT this capture commit, so a
  clean-build machine would silently build without capture. Consistent with how Q1's
  `external/quakespasm` carries local commits; fine for the next Pi cycle on THIS host. If
  a clean-build release is ever in scope, this commit must reach the fork-mirror.
- If the demo desyncs (frame N differs structurally, not just filtering), re-check that the
  Pi honors `fixedtime`/`timedemo` (both are stock yQuake2 `common/frame.c`, no Phoenix
  patch, so they should) and that `cl_particles 0` took.
