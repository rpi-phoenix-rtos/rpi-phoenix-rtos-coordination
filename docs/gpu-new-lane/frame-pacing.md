# frame-pacing — why GPU-heavy games lock to 30.00 fps on the new lane

**Status (2026-09-27):** analysis done, fix built (not yet run on the Pi). The frame waits in exactly
one place: SDL 2.30.12's `KMSDRM_GLES_SwapWindow` waits for the previous page flip **before** it calls
`eglSwapBuffers`. On V3D, `eglSwapBuffers` is where the frame's render job gets submitted. So the GPU
sits idle from the end of one frame's job until the previous flip reaches the screen, and every frame's
render starts at a vblank. For quake2-drm, 14.8 ms of GPU time plus the display's 2 ms latch guard
does not fit in 16.7 ms, so every frame lands one vblank late: exactly 30.00 fps. That happens even
though the CPU (8.8 ms per frame) and the GPU (14.8 ms per frame) could each sustain 60.

The fix is a 1-hunk SDL patch that swaps first and waits second
(`tools/gpu-lane/sdl2-drm/patches-pace/0001-…`). It is opt-in: the default patch set, and every
binary built from it, is unchanged. Two A/B cycles are pre-registered in §7: `pace-q2` and `pace-qs`.
The model predicts about 60 fps for quake2 and 40–56 for quakespasm.

Tags: [Pi] = measured on the Pi in an existing log · [read] = read in source · [model] =
`tools/gpu-lane/pace/frame-pacing-model.py` · [host] = checked on the host (build, objdump, cmp).

## 1. The evidence (mig-q2, `artifacts/rpi4b-uart/rpi4b-uart-20260927-125308-mig-q2.log`) [Pi]

| Quantity | Value | Source |
|---|---|---|
| fps, every steady 5 s window | **30.00**: 53 of 53 windows (150 or 151 frames in 5000/5033 ms) | `quake2-drm flipstat` |
| time inside `SDL_GL_SwapWindow` | **24.4–24.6 ms** average, max ≤ 26.0 ms, every steady window | `quake2-drm swapstat swap_us_avg/max` |
| CPU time per frame outside the swap | 33.33 − 24.5 = **8.8 ms** | the two lines above |
| GPU jobs per frame | **1** (150 render jobs per 5 s window = the frame count) | `V3DA srv qstat` deltas |
| GPU time per frame | bin **0.23 ms** + render **14.55 ms** (14.53–14.56 in every window) | `V3DA qstat` `bin=`/`render=` ms deltas ÷ jobs |
| GPU busy | **44 %** | `qstat busy` delta ÷ window |
| old lane (no vsync, serial CPU+GPU) | 38.86 fps = 25.7 ms ≈ 8.8 + 14.8 + ~2 | gate header |

The render jobs take the same 14.55 ms in every window, so jitter is well below 1 ms. That is why the
rate is *exactly* 30.00 rather than 30-something. The GPU is idle 56 % of the time, the CPU spends 74 %
of each frame inside the swap, and neither is saturated. **Something in the present path serialises
them.**

For comparison: kmscube is 60.00 (1.9 ms GPU per frame; poll-wake run B). quakespasm-drm is 30.9 in
timedemo (poll-wake) and 26–40 per window in `mig-qs` (`rpi4b-uart-20260927-135946-mig-qs.log`). stk-drm
is 11.9, GPU-bound, with the GPU 92 % busy (`stkdrm-2` qstat).

## 2. The four layers, checked

### 2.1 SDL KMSDRM, the serialisation point [read]

`SDL_kmsdrmopengles.c` (SDL 2.30.12, unpatched by 0001–0008), `KMSDRM_GLES_SwapWindow`:

1. line 115: `KMSDRM_WaitPageflip()` blocks in `poll()` until the **previous** flip's event arrives;
2. line 121: releases the old front buffer;
3. line 130: `eglSwapBuffers()`, which flushes the frame, i.e. **submits this frame's render job**;
4. line 139: `gbm_surface_lock_front_buffer()`, then `KMSDRM_FBFromBO`, then `drmModePageFlip()` (line 181).

So job *k* cannot be submitted before flip *k−1* is on screen. The wait is **unconditional**.
`SDL_HINT_VIDEO_DOUBLE_BUFFER` (`SDL_kmsdrmvideo.c:1525`, default off) only adds a *second* wait right
after the flip (line 199). The premise that SDL's non-double-buffer mode already lets the GPU overlap a
pending flip is therefore wrong for 2.30.12. That mode overlaps the CPU's next frame with the pending
flip, but not the GPU's work. The in-code comment says the same thing: "Wait for confirmation that the
next front buffer has been flipped, at which point the previous front buffer can be released". The
wait exists to free a buffer, but it is placed before the swap, where it also delays the submit.

### 2.2 Mesa GBM/EGL: no wait on the GPU, 4 buffers [read]

* `egl_dri2.h:363`: `color_buffers[4]`. `platform_drm.c` `get_back_bo()` (line 245) picks any buffer that
  is neither locked nor `current`. Three are ever needed: one on screen, one queued, one being rendered.
* `dri2_drm_swap_buffers()` (line 364) flushes, invalidates the drawable and makes the back buffer
  `current`. It never waits for a flip or for the GPU.
* The one wait is the DRI swap throttle, `dri_drawable.c:508–524`. After flushing frame *k* it waits on
  the fence of frame **k−1**. That fence has always signalled by then, because the stock order already
  waited for flip *k−1*, which needs it. This is the `SYNCOBJ_WAIT timeout=∞` named in poll-wake Finding 1.
  It does not serialise anything.

### 2.3 rpi4-kms: DRM semantics, one commit in flight, fence-gated, 2 ms latch guard [read]

* `kms_main.c:712`: `-EBUSY` if a commit is pending on the CRTC ("one commit in flight per CRTC, as
  DRM"). It does not queue, so a client must wait for the event before it flips again, as SDL does.
* `commit()` → `kms_try_apply()` (line 544). If the in-fence (G13 implicit attach: the BO's last render
  fence) has not signalled, the commit is `deferred`. After the kick (poll-wake fix), the vblank thread
  polls it every `gate_us` = 500 µs (`kms_vblank.c:519–528`, `applied_gate`).
* On arm (lines 589–593) the target is V+1 if `since_last_vblank + guard_us < period`, otherwise V+2.
  `guard_us` = 2000 (E3: the firmware latches pending plane state about 1.6 ms before the vblank).
* **This is correct and must stay.** Lowering the guard would trade the 30-fps cliff for late latches,
  meaning a plane update the firmware has not taken yet.

### 2.4 rpi4-v3d-async: prompt fence, no client wait [Pi + read]

`V3DA srv irq mode=irq`: completion is interrupt-driven. The fence-to-arm path costs about 2 ms end to
end (kmscube run B: `q2a_us_avg` ≈ 2150 µs, `vbl1=600`). Mesa's only wait on it is the throttle in 2.2.
SUBMIT is asynchronous: kmscube and stk-drm both show `deferred ≈ flips`, so the fence is still pending
when the flip is committed.

## 3. Timeline of one frame (60 Hz, V = vblank, ms after V0)

**Stock (mig-q2)** [model = log]:

```
V0  0.0   flip k-1 completes -> event -> SDL's WaitPageflip returns (~60 us, poll-wake)
    0.1   eglSwapBuffers: job k submitted; GPU starts (it has been IDLE since job k-1 ended)
    0.4   drmModePageFlip(k) -> kms: fence pending -> deferred, kick
    0.4   SDL_GL_SwapWindow returns; engine builds frame k+1 on the CPU (8.8 ms)
    9.2   engine calls SDL_GL_SwapWindow -> WaitPageflip(k) blocks        <- swapstat clock starts
   15.2   job k done (0.23 bin + 14.55 render); kms gate poll sees the fence at <= 15.7
   15.7   arm: since 15.7 + guard 2.0 = 17.7 > 16.67  ->  target V2
V1 16.7   (flip k misses this vblank)
V2 33.3   flip k on screen -> event -> WaitPageflip returns; submit k+1 ...   <- swap took 24.1–24.6 ms
```

Per frame: vblank → submit → 14.8 ms of GPU → 0–0.5 ms poll → 2 ms guard ≈ 17.4 ms > 16.67. Quake
misses the cliff by less than 1 ms, every frame. That makes the rate exactly 30.00, and it explains why
the light kmscube runs at 60.

**Pace (patched)** [model]:

```
V0  0.0   flip k-1 completes; WaitPageflip returns; PageFlip(k) (job k already running) -> swap returns
    0.1   engine builds frame k+1 (8.8 ms)
    8.9   SDL_GL_SwapWindow: eglSwapBuffers -> job k+1 submitted (queued behind job k)
    8.9   throttle waits for job k; then WaitPageflip(k) blocks
   ~7-9   job k done (it started at ~-7.6, when job k-1 ended); armed well before V1-2ms
V1 16.7   flip k on screen -> WaitPageflip returns -> PageFlip(k+1)
V2 33.3   flip k+1 on screen ...                     -> one frame per vblank, swap ~7.9 ms
```

The GPU now always has the next job queued, so it runs back to back at 14.8 ms per frame. Each fence
lands mid-frame, and each flip makes the next latch. At most one flip is pending. The model's
steady state is 60.0 fps, with a swap of about 7.9 ms, `vbl1` = 100 % and `q2a` ≈ 7.5 ms.

## 4. The model [model]

`tools/gpu-lane/pace/frame-pacing-model.py` implements exactly the rules above: SDL order, throttle on
fence *k−1*, a serial GPU queue, `gate_us` polling, and the `guard_us` target rule. It is deterministic.
`--check` asserts that the stock order reproduces mig-q2 (30.00 fps, swap 24.5 ms) and kmscube (60.00).

| workload (CPU, GPU ms/frame) | stock fps / swap | **pace** fps / swap | pace vbl1 / q2a |
|---|---|---|---|
| quake2 as measured (8.8, 14.8) | 30.0 / 24.5 | **60.0 / 7.9** | 100 % / 7.5 ms |
| quake2, GPU +10 % at the higher rate | 30.0 / 24.5 | 60.0 / 7.9 | 100 % / 9.3 |
| quake2, GPU +20 % | 30.0 / 24.5 | 51.9 / 10.5 | 83 % / 10.9 |
| quake2, CPU 15 ms | 32.0 / 16.2 | 56.2 / 2.8 | 92 % / 12.8 |
| quakespasm (10, 19.5 ± 3) | 30.6 / 22.7 | 45.6 / 11.9 | 67 % / 11.1 |
| quakespasm light scene (10, 15 ± 3) | 37.2 / 16.9 | 56.4 / 7.7 | 93 % / 9.0 |
| GPU-bound, 25 ms | 30.0 / 24.6 | 39.4 / 16.6 | 46 % / 14.9 |
| kmscube-like (1.5, 1.9) | 60.0 / 15.2 | 60.0 / 15.2 | 100 % / 0 |

So the brief's "about 38–39 fps, one or two vblanks per frame" is the pace result for a game that is
truly GPU-bound at about 25 ms. Quake II is not GPU-bound: the old lane's 25.7 ms was CPU + GPU run
**in series**. Once they overlap, the vblank is the limit. The Pi will show whether the GPU stays at
about 14.8 ms per frame at twice the rate (memory bandwidth), which is the +10/+20 % rows.

## 5. The fix: `patches-pace/0001-kmsdrm-submit-frame-before-waiting-for-previous-flip.patch`

New order: `eglSwapBuffers` → `gbm_surface_lock_front_buffer` → `KMSDRM_FBFromBO` →
`KMSDRM_WaitPageflip` (previous flip) → release the old front buffer → `drmModeSetCrtc` (first frame) or
`drmModePageFlip`. The patch uses a local for the newly locked BO, so the first-swap `SetCrtc` branch and
`bo`/`next_bo` keep their meaning. On the error paths after the lock, the new BO is released. That drops
one frame instead of leaking a buffer.

**Invariants (why there is no tearing):**

* The buffer Mesa renders into is never locked. At the next frame's first draw, the locked buffers
  are the one on screen and the one queued, and `get_back_bo` skips both.
* A front buffer is released only after the flip that replaces it has completed, as before.
* At most one flip is pending (kms `EBUSY` rule), as before.
* Every flip is still fence-gated in rpi4-kms. The patch changes *when* SDL submits, not the gate.
* It needs three GBM buffers at most, and Mesa has four.
* `KMSDRM_FBFromBO` (`drmModeAddFB` on a BO's first use) now runs while a flip may be pending. In
  rpi4-kms, `KMS_OP_ADDFB2` → `kms_fb_add` (`kms_bo.c:~717–745`) has no pending-commit condition
  (only ENOENT/EINVAL/ENOSPC). The one `-EBUSY` besides the commit rule is `KMS_OP_RMFB` of a buffer that
  is in flight, and SDL never removes an FB mid-run. [read]

`SDL_VIDEO_DOUBLE_BUFFER=1` keeps its extra wait right after the flip, which now reads "before queuing
this flip" instead of "at the beginning". Nothing in the patch is Phoenix-specific. It is upstream-able
as written. I have not checked whether SDL3 changed this: no SDL3 tree is available offline.

**Not changed:** rpi4-kms (`guard_us`/`gate_us` are the tear-free latch model), Mesa, libdrm-phoenix,
and rpi4-v3d-async. **kmscube is unaffected by construction.** It does not link SDL, and its own loop
(`drm-legacy.c`) already draws only after the flip event. It stays at 60.00 (poll-wake run B). A
re-run of kmscube would be a control, not a requirement.

## 6. Build [host]

### 6.1 What was added (all opt-in; the default build and binaries are byte-for-byte as before)

| File | Change |
|---|---|
| `tools/gpu-lane/sdl2-drm/patches-pace/0001-…patch` | the fix (new dir, **not** `patches/`, so the default stamp `2f79883b…` and `libSDL2.a` `4abf34e0…` stay) |
| `sdl2-drm/build.sh` | `--extra-patches <dir>` (applied after `patches/`, part of the stamp; without it the stamp is unchanged, checked) and `--name <clone>` (quakespasm's banner/flipstat tag) |
| `sdl2-drm/gamedrm/relink-sdl-gl-game.sh` | optional `G_SDL_DIR` (which `sdl-prefix` to link), `G_SUFFIX` (staged names), `G_PORT_SHADOW` (port objects/build.log from a shadow build); defaults unchanged |
| `sdl2-drm/build-quake2-drm.sh` | `--variant <v>` (SDL from `build-out-<v>`, everything named `quake2-drm-<v>`), env `Q2DRM_PORT_SHADOW` |
| `sdl2-drm/gamedrm/shadow-port-build.sh` | **new**: re-runs a port's own `p_prepare`/`p_build` into a private dir (see 6.2) |
| `sdl2-drm/gamedrm/gamedrm_hooks.c` | `GAMEDRM_EXIT_SECS=N` (unset = never): `_exit(0)` at the first 5 s window boundary ≥ N s after the first swap, with an `exit after …` line, so two binaries run in one boot (neither engine ever exits on its own) |
| `tools/gpu-lane/pace/frame-pacing-model.py` | **new**: the model (§4) |

### 6.2 Why a shadow port build

Build 15 (`queue40`, `--with-ports`) logged `Build state changed for yquake2-8.71, cleaning` and then
failed in python's prepare step before it reached yquake2. The port's engine objects and `build.log`, which
`relink-sdl-gl-game.sh` reads, are gone from `.buildroot/_build/…/port-sources/`. Nothing may write
there, so `shadow-port-build.sh yquake2 build-out/quake2-drm-pace/port build-out/quake2-drm/link-cmd.txt`
re-runs the port's own recipe into a private dir. It uses the image build's CFLAGS/LDFLAGS, taken
verbatim from the recorded port link.

**Proof that the objects are the shipped ones:** the shadow's own old-stack link (the port's control)
has the same `size` and the same size for **every symbol** as the shipped `prog/yquake2` (`e36dd0ed…`).
Stripped, the two binaries differ in **2 bytes: one instruction**, `NET_Socket` (`network.c:964`)
`setsockopt(fd, IPPROTO_IPV6, 1 → 12, …)`. That is an `IPV6_*` value from build 15's
`netinet/in.h` (libphoenix `feat/ipv6mreq-execinfo`), a socket option on the IPv6 path with nothing to do
with rendering.

### 6.3 A/B builds: control and fix differ only in `KMSDRM_GLES_SwapWindow`

Both SDL trees were compiled minutes apart from the same sysroot. `ctl` has the default patch set and
`pace` adds patches-pace. The same Mesa (`build-out/mesa-gl`, symlinked), the same libdrm-phoenix, the
same shadow engine objects and the same hooks go into both.

```
tools/gpu-lane/sdl2-drm/build.sh --out build-out-ctl  --skip-mesa --name quakespasm-drm-ctl
tools/gpu-lane/sdl2-drm/build.sh --out build-out-pace --skip-mesa --extra-patches patches-pace --name quakespasm-drm-pace
tools/gpu-lane/sdl2-drm/gamedrm/shadow-port-build.sh yquake2 build-out/quake2-drm-pace/port build-out/quake2-drm/link-cmd.txt
Q2DRM_PORT_SHADOW=build-out/quake2-drm-pace/port tools/gpu-lane/sdl2-drm/build-quake2-drm.sh --variant ctl
Q2DRM_PORT_SHADOW=build-out/quake2-drm-pace/port tools/gpu-lane/sdl2-drm/build-quake2-drm.sh --variant pace
```
(`build-out-ctl/` and `build-out-pace/` each hold a `mesa-gl -> ../build-out/mesa-gl` symlink, which
`--skip-mesa` requires. Everything else in them is written by build.sh.)

Checks:

* `libSDL2.a`: `ctl` `3c086c48…` vs `pace` `655101f8…`. After `--strip-debug`, members differ only in
  `SDL_kmsdrmopengles.c.obj` (**code**) and in 3 renderer objects (**data only**: the build-dir path
  string). Against the *default* `4abf34e0…` (built at 04:59) `SDL_stdlib` also differs, in `SDL_ispunct`,
  because libphoenix's ctype moved on since then. That is why the control arm is a fresh `ctl` build and
  not the default.
* ELF pairs (`yquake2-drm-ctl`/`-pace`, `quakespasm-drm` ctl/pace): the same `text`/`data`/`bss`. `nm -S`
  differs **only** in `KMSDRM_GLES_SwapWindow` (0x24c → 0x244) and `gamedrm_banner` (the name). Every
  text/data/bss symbol is at the same address. In the quake2 pair, `.rodata` after the one-character-longer
  banner moved by 16 B; in the quakespasm pair `nm` is identical.
* Call order in `KMSDRM_GLES_SwapWindow` (objdump): ctl = `WaitPageflip`, blr, blr (eglSwapBuffers), blr
  (lock), `FBFromBO`. pace = blr (eglSwapBuffers), blr (lock), `FBFromBO`, **`WaitPageflip`**, blr
  (release).
* relink proofs as for quake2-drm: `nm -u` = 0, no PT_INTERP, no old-lane strings, the swap wrap is in
  the engine path, launcher exec target `/usr/bin/yquake2-drm-<v>`, `guarded shared files unchanged
  (15 checked)`. Control relink `DIFFERS from shipped` (the one-instruction header change, 6.2).
* Unchanged after all builds: default `libSDL2.a` `4abf34e0…`, `yquake2-drm.stripped` `33fb96f1…`,
  `quake2-drm` `b08ee6a4…`, `quakespasm-drm.stripped` `fa40faae…`, `quake3e-drm.stripped` `6d69a9db…`.

### 6.4 Staging

Frozen copies are in `tools/gpu-lane/sdl2-drm/build-out-pace/stage/`, with `SHA256SUMS` (gitignored):

| File | sha256 | Staged as |
|---|---|---|
| `yquake2-drm-ctl` | `3ef1cb5fa660cd5a…` | `/usr/bin/yquake2-drm-ctl` |
| `quake2-drm-ctl` | `d34c205283ea6ee2…` | `/usr/bin/quake2-drm-ctl` (execs `/usr/bin/yquake2-drm-ctl`) |
| `yquake2-drm-pace` | `1a42c881c73ecec3…` | `/usr/bin/yquake2-drm-pace` |
| `quake2-drm-pace` | `aeaca786dc7aa745…` | `/usr/bin/quake2-drm-pace` (execs `/usr/bin/yquake2-drm-pace`) |
| `quakespasm-drm-ctl` | `7529bdd84ef27ac8…` | `/usr/bin/quakespasm-drm-ctl` |
| `quakespasm-drm-pace` | `16fc1035dbed71d4…` | `/usr/bin/quakespasm-drm-pace` |

```
St=tools/gpu-lane/sdl2-drm/build-out-pace/stage; E=/srv/phoenix-rpi4-nfs-gcc16
( cd $St && sha256sum -c SHA256SUMS )
for f in yquake2-drm-ctl quake2-drm-ctl yquake2-drm-pace quake2-drm-pace quakespasm-drm-ctl quakespasm-drm-pace; do
    sudo -n install -m 755 $St/$f $E/usr/bin/$f && cmp $St/$f $E/usr/bin/$f || echo STAGE_FAIL $f; done
```

None of these overwrites a binary that a queued cycle uses: `quake2-drm`, `yquake2-drm`,
`quakespasm-drm`, `quake3-drm` and `vkquake-drm` are untouched. The servers are the ones already staged
(`rpi4-v3d-async-m3p2`, `rpi4-kms-gate`).

## 7. Pre-registered cycles

Both cycles run the same boot, the same servers as mig-q2, and the control and the fix back to back.
`GAMEDRM_EXIT_SECS=60` ends each game about 60 s after its first swap, which is 12 flipstat windows. The
ready-line also matches the `export` echo and the games' `exit after … (GAMEDRM_EXIT_SECS=60)` line, so
each command moves on 20 s after it and the KMS server's per-client `flipstat` line is captured.
Estimated cost: boot about 70 s, servers about 50 s, two games at about 100 s each, so roughly 330 s. Use
Bash `timeout: 600000`.

### 7.1 `pace-q2`

```
./scripts/test-cycle-psh-interact.sh --label pace-q2 --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 150 --ready-line 'V3DA srv detached|KMS srv detached|GAMEDRM_EXIT_SECS=[0-9]+' --ready-extra-secs 20 \
    --hdmi-dense-on 'quake2-drm-pace: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "export GAMEDRM_EXIT_SECS=60" \
    "/usr/bin/quake2-drm-ctl" \
    "/usr/bin/quake2-drm-pace"
```

| Line / observation | Predicted | If instead… |
|---|---|---|
| banners `quake2-drm-ctl: new GPU lane …`, then `quake2-drm-pace: new GPU lane …`; `ram-stage: exec /usr/bin/yquake2-drm-<v>` | one each, in that order | missing: staging (`cmp`) |
| `quake2-drm-ctl flipstat … fps` (steady windows) | **29.9–30.1** every window, as mig-q2 (the control reproduces the bug) | ≠ 30: the control differs from mig-q2; do not read B until explained |
| `quake2-drm-ctl swapstat swap_us_avg` | 24 000–25 000 | |
| `quake2-drm-pace flipstat … fps` (steady windows) | **≥ 38 in every window; central 55–60.0; never > 60.1** | **exactly 30.00 and swap ≈ 24.5 ms**: the reorder is not in the running binary (`cmp` the staging, objdump §6.3). **38–50**: the GPU got slower at the higher rate. Read `qstat` render ms per job against the §4 +10/+20 % rows. **> 60.1**: not vsync-paced, a bug. |
| `quake2-drm-pace swapstat swap_us_avg` | **5 000–13 000** (model 7.9 ms) | ≥ 20 000 with fps > 30: the wait moved but still blocks. Check `max` |
| `V3DA srv qstat` render ms/job while pace runs | 14.5–16.5 ms, 1 job per frame, busy 85–95 % | > 17: bandwidth. It explains a 45–55 result, not a failure |
| `KMS srv flipstat client=…` (ctl, printed after `exit after`) | `vbl2 ≥ 95 %` of flips, `deferred ≈ flips`, `applied_gate ≈ deferred`, `q2a_us_avg` 14 500–17 000, `late_target ≈ flips` | `vbl1` large: the control is not the stock order |
| `KMS srv flipstat client=…` (pace) | **`vbl1 ≥ 80 %`**, `deferred` ≥ 50 % of flips (job k often still running at commit), `applied_gate ≈ deferred`, `q2a_us_avg` 3 000–11 000, `late_target` ≤ 20 % | `deferred ≈ 0`: the fences signal before commit (fine, but check that `applied_gate` counts are sane). `vbl2` dominant with fps ≈ 30: as row 3 |
| SDL errors `Wait for previous pageflip failed` / `Could not queue pageflip` / `eglSwapBuffers failed` / `Could not lock front buffer` | **0** | any: buffer accounting is wrong (EBUSY = two flips pending; EGL_BAD_ALLOC = no free GBM buffer) |
| `exit after N swaps … (GAMEDRM_EXIT_SECS=60)` | once per game; the ctl N is about 1 800 + warm-up; the pace N is about 1.6–2 × ctl | missing: the hook did not arm (env not inherited through ram-stage-play), and the second game never ran |
| the second game starts (banner, `first swap`) after the first one's `_exit` | yes, within about 15 s | it hangs before `first swap`: either KNOWN-ISSUES C5 (SDL audio on `/dev/audio0` stalling after `SDL_OpenAudio`, which is more likely after an `_exit` that skipped the audio teardown), or rpi4-kms not releasing the CRTC after a client vanished [neither exercised yet]. Re-run with `"export SDL_AUDIODRIVER=dummy"` added before both games (a fair A/B either way). If it still hangs, run the pace binary alone and record a kms finding |
| HDMI (dense snapshots of pace) | demo1 in textured 3D, full screen, no torn or partial frames, no console bleed | a torn frame: the invariants of §5 are violated. Stop and read before adopting |
| faults / `V3DA qstat err/wedges/rej` | 0 / 0 | addr2line first (`build-out/quake2-drm-<v>/yquake2-drm-<v>` unstripped) |

### 7.2 `pace-qs`

The same cycle with quakespasm, which uses the same patch and a different engine (desktop GL, 1.25 jobs
per frame, scene-dependent load):

```
./scripts/test-cycle-psh-interact.sh --label pace-qs --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 150 --ready-line 'V3DA srv detached|KMS srv detached|GAMEDRM_EXIT_SECS=[0-9]+' --ready-extra-secs 20 \
    --hdmi-dense-on 'quakespasm-drm-pace: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-gate -G" \
    "export GAMEDRM_EXIT_SECS=60" \
    "/usr/bin/quakespasm-drm-ctl" \
    "/usr/bin/quakespasm-drm-pace"
```

| Line | Predicted | If instead… |
|---|---|---|
| `quakespasm-drm-ctl flipstat` | 26–40 per window, mean 29–33 (as mig-qs) | |
| `quakespasm-drm-pace flipstat` | **mean ≥ 1.3 × ctl mean; windows 38–60**, never > 60.1 | ≈ ctl: read `swapstat` as in 7.1 |
| `quakespasm-drm-pace swapstat swap_us_avg` | 5 000–14 000 (ctl 10 000–25 000) | |
| KMS flipstat (pace vs ctl) | `vbl1` share up from < 40 % to > 60 %; `q2a_us_avg` down | |
| SDL errors, faults, HDMI | as 7.1 | as 7.1 |

**Gate for adopting the patch** (moving it from `patches-pace/` to `patches/0009` and rebuilding the
SDL clones — quakespasm-drm, quake2/3-drm, stk-drm — in a later pass): pace-q2 ≥ 45 fps average with
every window ≥ 38, pace-qs mean ≥ 1.3 × ctl, 0 SDL errors, 0 faults, and clean HDMI.

## 8. Follow-ups (not done here)

* After the gate passes: `patches-pace/0001` → `patches/0009`, one default SDL rebuild, and relinking
  of all SDL clones. Update MIGRATION §6.1/§6.4, whose "28–33 fps vsync-bound" predictions assumed that
  one flip in flight must cost a whole frame. It does not.
* stk-drm (GPU 92 % busy, about 10 jobs per frame) probably submits most of its GPU work mid-frame
  through FBO passes [inferred]. If so, the reorder gains it little; it is expected to be neutral or
  slightly positive.
* vkquake-drm goes through the Vulkan WSI (`VK_KHR_display`), not this function, so it is unaffected.
* `GAMEDRM_EXIT_SECS` also lets the showcase gate run several SDL clones in one boot.

## Result — `pace-q2` / `pace-qs` (queue44, 2026-09-27 15:17–15:35): ✅ PASS both

Same boot per cycle, control then fix; `GAMEDRM_EXIT_SECS=60`. Logs `artifacts/rpi4b-uart/*-pace-{q2,qs}.log`;
HDMI `artifacts/hdmi/20260927-152043-pace-q2-tick.png` (demo on screen, in-game counter **60.00fps**, no tearing).

| cycle | arm | fps median (steady windows) | range | KMS flipstat |
|---|---|---|---|---|
| pace-q2 | ctl | **30.00** | 30.00 | flips 1368: vbl1 11, **vbl2 1337**, deferred 1367, `q2a_us_avg=14752` |
| pace-q2 | **pace** | **60.00** (8 of 8 steady windows; one 52.0 ramp) | 52.0–60.00 | flips 2692: **vbl1 2659**, vbl2 24, deferred 2683, applied_gate 2682, `q2a_us_avg=7768` |
| pace-qs | ctl | **30.00** | 25.8–39.5 | flips 1859: vbl1 358, vbl2 1500 |
| pace-qs | **pace** | **46.05** (1.53×) | 36.8–52.4 | flips 2713: vbl1 2066, vbl2 646, deferred 2622 |

0 exceptions in both cycles; the second game started fine after the first exited (no audio fallback needed).
The model's predictions held: q2 ≥ 38 in every steady window (60), qs ≥ 1.3× (1.53×), control reproduces mig-q2.

**Decides:** adopt `patches-pace/0001` into the default SDL patch set and relink every SDL clone (§7 gate met).
Quake 2 on the new lane is now **60 fps vsynced vs 38.86 unsynced on the old lane**.
