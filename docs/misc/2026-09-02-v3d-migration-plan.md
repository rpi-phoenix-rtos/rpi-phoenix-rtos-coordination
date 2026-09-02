# V3D driver → phoenix-rtos-devices (D2): what is actually achievable

Investigation before implementation, because the obvious reading of D2 turns out
to be structurally impossible.

## The blocking fact

`libv3d-phoenix.a`, `libGL-phoenix.a` and `libv3dv-phoenix.a` **cannot be built
by a `phoenix-rtos-devices` framework component.** The recipe is not a Makefile:
it runs `meson setup` on `external/mesa` (a 1.2 GB gitignored clone pinned to
tag `mesa-26.2.0` plus `patches/mesa/phoenix-rpi4-v3d.patch`), takes every
per-file compile flag from the resulting host `compile_commands.json`, transforms
those flags for the cross toolchain, and runs `ninja` inside the host build tree
to materialise ~50 generated sources and headers. A devices-tree component has
none of that available.

By archive bytes the result is ~99% compiled Mesa. Of ~400 objects in
`libv3d-phoenix.a`, **five** are ours.

## What we actually own, and where half of it already lives

Ours is ~3.7k lines of glue: `v3d_phoenix_winsys.c` (1759), `v3d_phoenix_power.c`,
`v3d_phoenix_stubs.c`, `v3d_libdrm_shim.c`, `gl_stubs.c`, `libvcmbox.c`,
`phoenix_mesa_compat.h`, `shim-include/`, plus the Vulkan-side shims — and the
three build scripts.

**Half the driver has already migrated**: `sources/phoenix-rtos-devices/gpu/rpi4-v3d/`
holds the concurrent-GPU server (`rpi4-v3d.c`, `v3d_gpu.c`, `libv3d-client`) with
a real framework Makefile, and it already vendors the same three UAPI headers
that `tools/v3d-driver-port/` duplicates (~3.5k lines of duplication to delete).

## Achievable scope

Move **our glue + the build recipe** into `sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/`
on the `wifi/rpi4-wifi` model — source in the devices repo, built by a standalone
script, outputs gitignored — while `external/mesa`, the `/tmp/mesa-*-build` host
trees and `tools/.gpu-libs/` stay exactly where they are. That satisfies D2's
intent (the V3D driver source lives beside the V3D server it pairs with, in a
publishable core repo) without inventing a build the framework cannot run.

Note `wifi/rpi4-wifi` is precedent for exactly this: it lives in devices with no
Makefile and is built by `build-standalone.sh`.

## Stages

- **Stage 0 — DONE.** Deleted two superseded Mesa patches (56 KB; superseded by
  `patches/mesa/phoenix-rpi4-v3d.patch`, and three source comments that named
  them were corrected), untracked `csd-matmul`/`csd-probe` (1.4 MB of build
  output). No consumer paths touched.
- **Stage 1 — DONE (2026-09-02, owner approved D9 option A).** Glue + scripts moved
  into `gpu/rpi4-v3d/mesa/`; UAPI headers deduplicated against
  `gpu/rpi4-v3d/uapi/`; `ROOT` derivation fixed (env-first, mirroring
  `build-standalone.sh`); **no Makefile** (the devices root Makefile `include`s any
  Makefile at depth ≥2 and would break every devices build). Verified by archive
  equivalence: the scripts already guarantee deterministic member order, so
  `nm -g --defined-only` on old vs new is identical.
- **Stage 2 — DONE (2026-09-02).** Repointed consumers in lockstep: `build-showcase-apps.sh` (including
  `archive_fresh()`'s freshness root — miss it and stale archives ship silently),
  five `port.def.sh` in phoenix-rtos-ports, the x11/sdl2/quakespasm build scripts,
  and two `$(info)` hints in `_user`. `tools/.gpu-libs/` paths do not change.
- **Stage 3 — DONE, HW-VERIFIED (2026-09-02).** Cross-repo commit, `--scope core --with-showcase`
  rebuild + restage (shader cache cleared first), and three Pi cycles, all 0 faults
  (`Exception`/`Data Abort`/`Fatal`/`process "` = 0 in every log). Manifest:
  `manifests/2026-09-02-d9-v3d-mesa-migration-hw-verified.md`.
  - GLQuake (`/usr/bin/rpi4-quake`, log `…-221813-d9quake`): textured 3D demo playback at
    37–43 fps. **`v3d-winsys: scanout init … virt_h=3240 -> 3 buffer(s) TRIPLE-BUFFER+page-flip`** —
    so the de-duplicated `libvcmbox` (canonical `misc/rpi4-vcmbox/` copy, with the
    `valBufSize` guard) works: hazard 2 cleared. Consecutive HDMI ticks are clean and
    flicker-free (`artifacts/hdmi/20260902-2020{14,31}-d9quake-tick.png`).
  - X11 GPU desktop (`startx_gpu deskapps`, log `…-222215-d9xgpu`): WindowMaker + painted
    root + xterm (live BusyBox shell) + xclock + xcalc, stable over 3.5 min, all V3D work
    routed through `rpi4-v3d` (`CL submit #N done`) — so the `ar d` surgery really did
    remove the in-process winsys.
  - `gl-x11-window-daemon` as the sole client (`startx_gpu /bin/gl-x11-window-daemon`, log
    `…-222807-d9xglwin`): 1380+ GPU-rendered frames, 0 wedges — the SECOND `ar`-surgery
    consumer, runtime-proven too. Hazard 1 fully cleared (also statically: `nm` on both
    fresh daemons shows only the three client symbols, no `v3d_phoenix_scanout_init`/
    `_flip`/`_last_bin_crc`).
  - En route, one real build bug was found + fixed (phoenix-rtos-project `ca91eb9`): the
    rpi4-quake stale-archive guard was a **no-op** (`binary.mk` resets `NAME :=`, so
    `$(OBJS.$(NAME))` expanded to an empty target list, which GNU make silently ignores),
    so the freshly rebuilt archives would NOT have relinked the engine.
- **Stage 4** — the ~3.2k lines of harnesses/probes stay behind; they are the open
  D4 "genuine tool" boundary and must not block D2.

## Hazard worth naming

`tools/x11-port/build-xfbdev.sh` and `build-gl-x11-window.sh` perform `ar` surgery
**by member name** (`v3d_phoenix_winsys.o`, `v3d_phoenix_power.o`) to swap the
in-process winsys for the client library. Renaming any moved file leaves the
in-process winsys in the daemon build — two GPU owners at runtime, **with no
build error**. Filenames must not change, and Stage 3 must include the X11 check,
which is the only thing that catches it.


## Stage 1/2 as executed (2026-09-02)

Destination is `sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/` (note: the repo has
no `devices/` sub-level — the component path is `gpu/rpi4-v3d`). No `Makefile`, no
`README`, no in-dir `.gitignore`, mirroring `wifi/rpi4-wifi`; the root Makefile's
`find . -mindepth 2 -name Makefile` would otherwise `include` it and break every
devices build. Moved: the 11 glue `.c`/`.h`, `shim-include/` (5 headers), the three
`build-*-phoenix.py` scripts, `resolve-syms.py`, and the three committed Mesa source
manifests. `ROOT` is now env-first (`PHOENIX_RPI_ROOT`) with `PORT` derived from
`__file__`, so the two coord-repo scripts that `exec` the prelude keep working.

Deviations from the sketch above, both deliberate:

- **`libvcmbox.c`/`.h` were NOT moved — the duplicate was deleted.** The canonical
  copy is `misc/rpi4-vcmbox/` in this same repo and the `tools/` copy had drifted
  (missing a `valBufSize` bounds/alignment guard). `build-v3d-phoenix.py` now
  compiles the canonical source. The guard is unreachable for the driver's single
  call site (`vcmbox_call(VC_PROP_GET_VIRTUAL_WH, 8u, ...)` — 8 bytes, word-aligned).
- **`drm.h`/`drm_mode.h`/`v3d_drm.h` stay in `tools/`.** Only the stay-behind
  compute probes (`csd_*.c`, `mlp_gpu.c`) include them, and `gpu/rpi4-v3d/uapi/`
  already holds the devices-side copies — so the dedupe is satisfied by *not*
  moving them, and no Linux-UAPI header is newly introduced into a core repo.

Stage 4 harnesses stay in `tools/v3d-driver-port/`; the two the link-drive loop needs
(`harness_screen_create.c`, `v3dv_harness.c` + its generated `triangle_spirv.h`) are
reached through the new `HARNESS_DIR` var (`V3D_HARNESS_DIR` to override), commented
as the open D4 boundary.
