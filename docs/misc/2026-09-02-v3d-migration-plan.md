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
- **Stage 1** — copy the glue + scripts into `gpu/rpi4-v3d/mesa/`, deduplicate the
  UAPI headers against `gpu/rpi4-v3d/uapi/`, fix `ROOT` derivation (env-first,
  mirroring `build-standalone.sh`), **no Makefile** (the devices root Makefile
  `include`s any Makefile at depth ≥2 and would break every devices build).
  Verify by archive equivalence: the scripts already guarantee deterministic
  member order, so `nm -g --defined-only` on old vs new must be identical.
- **Stage 2** — repoint consumers in lockstep: `build-showcase-apps.sh` (including
  `archive_fresh()`'s freshness root — miss it and stale archives ship silently),
  five `port.def.sh` in phoenix-rtos-ports, the x11/sdl2/quakespasm build scripts,
  and two `$(info)` hints in `_user`. `tools/.gpu-libs/` paths do not change.
- **Stage 3** — cross-repo commit, `--scope core --with-showcase` rebuild, and one
  Pi cycle proving GLQuake **and** the X11 GPU desktop, then a manifest.
- **Stage 4** — the ~3.2k lines of harnesses/probes stay behind; they are the open
  D4 "genuine tool" boundary and must not block D2.

## Hazard worth naming

`tools/x11-port/build-xfbdev.sh` and `build-gl-x11-window.sh` perform `ar` surgery
**by member name** (`v3d_phoenix_winsys.o`, `v3d_phoenix_power.o`) to swap the
in-process winsys for the client library. Renaming any moved file leaves the
in-process winsys in the daemon build — two GPU owners at runtime, **with no
build error**. Filenames must not change, and Stage 3 must include the X11 check,
which is the only thing that catches it.
