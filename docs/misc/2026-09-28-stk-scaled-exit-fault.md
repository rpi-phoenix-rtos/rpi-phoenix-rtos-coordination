# STK exit Data Abort in Mesa `release_buffer` (m9b-stk-720, -540, -900)

2026-09-28. SuperTuxKart (`/usr/bin/supertuxkart-drm`, stripped `71ac4f58a678dc20`) through
`rpi4-kms-g9` in a scaled mode: gameplay fine, then an EL0 Data Abort at exit.

**Verdict (high confidence): an SDL 2.30.12 teardown-order use-after-free, fixed upstream.**
`KMSDRM_DestroySurfaces` destroys the EGL surface and *then* returns the two locked GBM front
buffers. Mesa's `eglDestroySurface` frees the `dri2_egl_surface` that `release_buffer()` walks, so the
release reads freed memory. This happens on **every** exit after two swaps, at any mode. It faults
only when libphoenix has already unmapped the heap page that held the freed struct. The KMS server
plays no part. The fix is SDL patch **0010**, a verbatim backport of upstream SDL
`9cc2f248f5` ("kmsdrm: Fix order of GBM and EGL teardown", SDL2 branch, 2026-04-11).

## 1. The dump, decoded

Logs: `artifacts/rpi4b-uart/rpi4b-uart-20260928-102005-m9b-stk-720.log`,
`…-102855-m9b-stk-540.log` (lines 500–577 in both), `…-103737-m9b-stk-900.log` (562–589). As usual, the EL0 dump appears twice.

| reg | 720 | 540 / 900 | meaning (from the disassembly below) |
|---|---|---|---|
| pc | `0x19d8630` | same | `release_buffer+0x10`, `platform_drm.c:79`: `ldr x2, [x3, x2]` |
| esr | `0x92000007` | same | DABT from EL0, **level-3 translation fault**, read: the page is unmapped |
| far = x3 | `0x3eada20` | **same** | `&dri2_surf->color_buffers[0].bo` |
| x4 | `0x3ead8d0` | **same** | `dri2_surf = gbm_surf->dri_private`, loaded fine from the live gbm surface |
| x0, x2 | 0, 0 | same | loop index 0: the **first** read of the struct faults |
| x1 | `0x3069a68` | `0x3f6cdd0` / `0x370efb8` | the `bo` being released (`windata->bo`) |
| lr | `0xf5f570` | same | `KMSDRM_DestroySurfaces`, `SDL_kmsdrmvideo.c:1133/1134` (return after `blr x2` at `0xf5f56c`) |

```
00000000019d8620 <release_buffer>:
 19d8620: ldr  x4, [x0, #40]      // x4 = surf->dri_private         (offsetof = 0x28, DWARF)
 19d8628: add  x3, x4, #0x150     // x3 = &dri2_surf->color_buffers (offsetof = 0x150, DWARF)
 19d8630: ldr  x2, [x3, x2]       // <-- far = x3 + 0: color_buffers[0].bo
 ...
 19d8650: strb wzr, [x0, #344]    // on a match: color_buffers[i].locked = false  (a WRITE)
```
DWARF of the unstripped `tools/gpu-lane/sdl2-drm/build-out/stk-drm/supertuxkart-drm` (gdb-multiarch):
`sizeof(struct dri2_egl_surface)` = 448, `color_buffers` at 0x150, 4 × 16 B.
`&gbm_dri_surface.dri_private` = 0x28.

In `KMSDRM_DestroySurfaces` (`0xf5f4a0`), the calls come in the pristine 2.30.12 order:
`bl SDL_EGL_MakeCurrent` (+0xa0), then `bl SDL_EGL_DestroySurface` (+0xb0), then the inlined
`gbm_surface_release_buffer` as `blr x2` twice (+0xcc, +0xe8). The fault's lr comes right after the
first of those.

**Stack words** (addr2line against the same ELF). The live chain is `IrrDriver::~IrrDriver`
(`0x4edaf8`/`0x4edb30`) → `CIrrDeviceSDL::~CIrrDeviceSDL` (`0x988b88`, `0x988c68`) →
`SDL_DestroyWindow` (`SDL_video.c:3328/3337`, `0xf5d1e0`/`0xf5d208`) → `KMSDRM_DestroyWindow`
(`:1454`, `0xf60018`, the return after the `:1447` call) → `KMSDRM_DestroySurfaces` (`:1125`,
`0xf5f554`, the return from `SDL_EGL_DestroySurface`) → `release_buffer`. Below sp, the dead words
`0x19cae04`/`0x19caea4` are `eglDestroySurface` (`eglapi.c:1308/1309`): the frame of the call that
had just returned. So the EGL surface was destroyed immediately before the faulting read. The other
words (`0x2b8…`, `0x2be…`, `0x3ea1050` = x6) are heap data pointers.

The 540 and 900 runs have **identical** far/x3/pc/esr (3 of 3 scaled runs; x4/lr checked on 540). Phoenix has no ASLR, so the heap layout at exit is
deterministic. Only the bo pointer (x1) differs, because the buffers are sized by the mode.

## 2. Mechanism, with file:line references

SDL 2.30.12, `tools/gpu-lane/sdl2-drm/build-out/sdl-src/src/video/kmsdrm/SDL_kmsdrmvideo.c` (same
as the pristine tarball `sources/phoenix-rtos-ports/sdl2/SDL2-2.30.12.tar.gz`: EGL block at :1116,
GBM buffers at :1127; none of our patches 0001–0009 touches this function):

1. `:1102` `drmModeSetCrtc(crtc->buffer_id = 0, original_mode)`. This is g9's restore rule 1,
   `KMS mode crtc=0 1920x1080 native … why=primary_off` (log line 561), and it works.
2. `:1120–1124` `SDL_EGL_MakeCurrent(NONE)` + `SDL_EGL_DestroySurface` → Mesa
   `dri2_drm_destroy_surface()` (`mesa-src/src/egl/drivers/dri2/platform_drm.c:202–218`):
   `gbm_bo_destroy()` of every color buffer (including the two SDL still holds as `bo`/`next_bo`),
   then **`free(surf)`**, which frees the 448-byte `dri2_egl_surface` calloc'd at `:146`. It does not
   clear `gbm_surf->dri_private` (set at `:174`). Upstream Mesa is the same
   (`external/mesa/…/platform_drm.c:174`).
3. `:1132–1140` `gbm_surface_release_buffer(gs, bo)` → `release_buffer()` (`platform_drm.c:73–84`)
   → `surf->dri_private` (dangling) → reads `color_buffers[i].bo`, and **writes** `locked = false`
   on a match. So the bug is also a 1-byte write-after-free.
4. `:1147` `gbm_surface_destroy(gs)`.

**Why a translation fault here and silence on Linux:** glibc keeps a freed 448-byte block mapped, so
the read returns stale data. libphoenix `malloc_dl.c:2935–3035` **munmaps a heap as soon as its last
chunk is freed** (small classes get one-page heaps, `_malloc_heapAlloc` `:2057–2060`,
`lookup[]` `:2518`). By the time `eglDestroySurface` has freed the surface, its drawable and the
buffers, the page `0x3ead000` holding `dri2_surf` had no live chunk left. The page was released,
and the next read of it faulted.

**Which pointer is stale:** `gbm_dri_surface.dri_private` (x4). The gbm surface itself (x0) and
`windata->gs` are still valid, and `windata->bo`/`next_bo` are dangling `gbm_bo` pointers too, but
`release_buffer` only compares them, so they never fault.

**Does the mode-change path matter?** Not in the code. With a lower mode, SDL_video.c calls
`KMSDRM_SetDisplayMode` (`:1393`: the modes' `driverdata` differ, so `SDL_SetDisplayModeForDisplay`'s
memcmp `SDL_video.c:1115` does not short-circuit). Both that call and `KMSDRM_SetWindowFullscreen`
(`:1703`) only mark the surfaces dirty (`KMSDRM_DirtySurfaces` `:1173`). The recreate
(`KMSDRM_CreateSurfaces` → `DestroySurfaces` `:1216`) runs at the top of the **first** swap
(`SDL_kmsdrmopengles.c`), where `bo == next_bo == NULL`, so no release happens and no stale read is
possible. Nothing is released twice, and no freed gbm object is left in `windata`. After ≥ 2 swaps
(with patch 0009's rotation `bo ← next_bo ← new_bo`, always two distinct buffers), the exit path is
the same at every mode.

What the scaled path does change is the **heap layout**: the extra `SetDisplayMode` (another
`SDL_SetCurrentDisplayMode` and RESIZED event) and g9's 8-mode connector (SDL's display-mode list
and `connector->modes` grow from 1 to 8 entries). Both change which chunks share `dri2_surf`'s page
[inferred, not traced]. The earlier native exits that were "0 faults" (`stkdrm-2`, `mig-all-stk`)
ran under `rpi4-kms-gate` (1 mode), not g9. They read a still-mapped freed block, found or missed
the bo, and moved on. A clean `m9b-stk-1080` would therefore **not** show that the bug is specific to
scaled modes: the stale read runs there too.

**Mesa patch 0009** (`mesa-drm/patches/mesa/0009-…`, `get_back_bo` dumb-buffer guard) does not touch
release or destroy, and its warning (`no DRI image`) is absent from both logs. **SDL patch 0009**
changes only `KMSDRM_GLES_SwapWindow`'s order; it does not change what is locked at exit. **SDL
0006** (static EGL) does not touch `SDL_EGL_DestroySurface`.

## 3. The KMS server is not involved

- far is a **malloc-heap** address: `calloc` at `platform_drm.c:146` + 0x150, in the same range as
  x5/x6 (`0x3ea1010`/`0x3ea1050`) and x19–x23. It is not a kmsbuf mapping (901-page, page-aligned
  imports). `dri2_surf` is not page-aligned (`…8d0`).
- The `V3DA srv import released … id=3/4/5` lines at 562–564 are the `gbm_bo_destroy()` calls inside
  step 2 (FB destroy callback → RMFB, GEM close) and the process's descriptors closing. They are
  releases the client started. Nothing a server does can unmap a page in the client's address space.
- g9's "restore to native on primary off" came from SDL's own SetCrtc (step 1), and `KMS srv client 1
  closed planes_off=0` confirms the primary was already off. `KMSTEST crtc rc=0 mode=1920x1080 fb=0`
  afterwards passes in both runs (m9b row 9 is fine).

## 4. Fix

`tools/gpu-lane/sdl2-drm/patches/0010-kmsdrm-release-gbm-buffers-before-destroying-the-egl-surface.patch`
moves the "Destroy the EGL surface" block after "Destroy the GBM buffers". The result is upstream
`9cc2f248f5` byte for byte; upstream reported the same UAF in `libnvidia-egl-gbm.so`. There is no
Mesa change: upstream Mesa's contract is that the EGL surface must outlive the release, and SDL main
does it in this order too. The `bo != next_bo` guard from SDL main is not needed, because with 0009
the two are always distinct. Checks: 0001–0010 apply cleanly to a scratch extraction of the tarball
(`patch -p1`, what `build.sh` does), and the result equals the intended tree (`diff -r`).

Scope: every KMSDRM GL game is affected (stk-drm, quakespasm-drm, quake2/3-drm; vkquake-drm's
Vulkan windows skip `DestroySurfaces`). They all get the fix at their next relink against a rebuilt
`libSDL2.a`. Adding 0010 changes `build.sh`'s SDL source stamp, so the next `sdl2-drm/build.sh`
re-extracts SDL and rebuilds `libSDL2.a` **and overwrites `build-out/quakespasm-drm`**, which is the
addr2line reference for the staged quakespasm-drm. Keep a copy first (§6 step 1).

Ports mirror: `sources/phoenix-rtos-ports` branch **`agent/sdl2-kmsdrm-teardown-order`** (`c2f3795`,
pushed to `publish`; `sdl2_kmsdrm/patches/0010` + one comment line in `port.def.sh`). **Not merged to
master**, so `scripts/check-gpu-lane-ports-sync.sh` reports this one file MISSING until it is.

## 5. Host reproduction: `tools/gpu-lane/sdl2-drm/hosttest-teardown/run.sh`

`teardown.c` drives the host's own Mesa GBM + EGL (26.0.8, amdgpu render node) through SDL's call
sequence: 4 swaps with 0009's lock/release bookkeeping, then `DestroySurfaces` in the 2.30.12 order
(`old`) or in 0010's order (`fixed`), under valgrind, at three sizes:

| size | old | fixed |
|---|---|---|
| 1920×1080 | **4** invalid accesses from the release calls (2 reads + 2 **writes**) | 0 |
| 1280×720 | 4 | 0 |
| 960×540 | 4 | 0 |

valgrind names the block: "800 bytes inside a block of size 1,152 free'd" (the host build's
`dri2_egl_surface` is larger), freed by `eglDestroySurface` and alloc'd by `eglCreateWindowSurface`.
`result: fails=0 PASS`. This shows the UAF with the real Mesa code path at every size, native
included, and shows that the reorder removes it. It cannot show the Phoenix page unmap; the Pi check
below does that.

## 6. Pre-registered Pi check: `m9b-stk-720-t10` (and `m9b-stk-1080-t10`)

Build (coordinator; no image build and no core scope; the Pi must not be mid-cycle only for the
staging step):
1. Keep the current references: `mkdir build-out/pre-0010 && cp -a build-out/quakespasm-drm*
   build-out/sdl-prefix/lib/libSDL2.a build-out/pre-0010/` (under `tools/gpu-lane/sdl2-drm/`).
2. `tools/gpu-lane/sdl2-drm/build.sh`. The log must show `tarball + 10 patches`. Mesa is a no-op
   when current.
3. Relink under a **new name** so a stale copy cannot be graded: copy `build-stk-drm.sh` to
   `build-stk-drm10.tmp.sh` in the same directory, with `name="drm"` → `name="drm10"`, and run it with
   `STKDRM_OUT=tools/gpu-lane/sdl2-drm/build-out/stk-drm10 … --no-control`. That produces
   `supertuxkart-drm10(.stripped)` and the launcher `stk-drm10`, which execs
   `/usr/bin/supertuxkart-drm10`. All the script's proofs still run. Delete the temporary copy
   afterwards.
4. **Static proof before staging**: `aarch64-phoenix-objdump -d` of `KMSDRM_DestroySurfaces` in the
   new unstripped ELF must show the two `blr x2` (release) **before** `bl <SDL_EGL_DestroySurface>`.
   The current ELF shows the reverse (+0xb0 < +0xcc). Also record the new `BUILD-INFO.txt`'s
   `libphoenix.a` sha: the staged binary used `2acb195e2e08…`, and the tree sysroot is now
   `83c07cf81b47e3f8` (2026-09-28). So the relink is **not single-variable**: it changes 0010 *and*
   libphoenix, and the heap layout with them.
5. Stage, checking each path is absent first: `supertuxkart-drm10.stripped` →
   `/usr/bin/supertuxkart-drm10`, `stk-drm10` → `/bin/stk-drm10`, then `cmp`.

Cycle (same as m9b §6.2, only the game line changes; detached, as the m9b runs):
```
./scripts/test-cycle-psh-interact.sh --label m9b-stk-720-t10 --wait-secs 220 --inter-cmd-secs 8 --idle-secs 60 \
    --max-cmd-secs 440 --ready-line 'V3DA srv detached|KMS srv detached|profile: Number of frames|KMSTEST done' \
    --ready-extra-secs 45 --hdmi-dense-on 'stk-drm: new GPU lane' -- \
    "/bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i" \
    "/bin/rpi4-kms-g9 -G" \
    "/bin/stk-drm10 --screensize=1280x720 --track=hacienda --numkarts=4 --profile-laps=2" \
    "/bin/kmstest-g9 crtc"
```
(`game-res stk 1280x720` builds exactly that argv for `/bin/stk-drm`; this names the new launcher
directly.) Then `m9b-stk-1080-t10` with `--screensize=1920x1080`. That run is needed because the
native exit also does the stale read and was clean only by layout.

| # | line | predicted (fix works) | if instead… |
|---|---|---|---|
| 1 | `stk-drm10: DATADIR=…`, then `stk-drm: new GPU lane` | the new binary runs | `stk: DATADIR` / `stk-drm: DATADIR`: an old launcher; void |
| 2 | `KMS mode crtc=0 1280x720 scaled …` | as m9b-stk-720 | — |
| 3 | `profile: Number of frames …`, then `stk-drm: exit after <N> swaps` | the exit completes (this line is printed at exit and is **absent** from both faulting runs) | — |
| 4 | **`Exception #` count after the `profile:` line** | **0** | a dump with `process "/usr/bin/supertuxkart-drm10"`: addr2line against `build-out/stk-drm10/supertuxkart-drm10`. `pc` still in `release_buffer` means 0010 is not in the ELF (step 4 missed it); any other pc is a later teardown bug **or** libphoenix drift (step 4 note); grade it only after checking both |
| 5 | `KMS mode crtc=0 1920x1080 native … why=primary_off`, `KMS srv client <n> closed planes_off=0` | unchanged | — |
| 6 | `KMSTEST crtc rc=0 mode=1920x1080 fb=0` | unchanged | — |
| 7 | fps (`flipstat-summary.sh --seq`) | as m9b-stk-720 (22.3), ±1 | a change: 0010 only touches teardown, so this points to a different binary or bench state |

**Decides:** row 4 = 0 at 720, where the old binary faulted 2 of 2 runs at an identical far
(deterministic), confirms the fix. Row 4 = 0 at 1080 as well shows the native path is clean because
of the fix, not because of layout luck.
