#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Phoenix Systems. Author: Witold Bołt.
# Build recipe for the yQuake2 Phoenix port (GPL-2.0-or-later); see COPYING.
"""Cross-link yQuake2 into a single static aarch64-phoenix ELF (Phase 1).

Phoenix has no dlopen/dlsym, so yQuake2's two dynamic-load seams (the game
DLL and the renderer DLL) are folded into ONE ELF: client + baseq2 game +
ref_gl1 renderer + the Phoenix backend (platform/) are compiled together and
linked against the ported SDL2 + Mesa/V3D GL stack.

  client TUs (incl. the integrated server)
+ baseq2 game TUs (src/game/*)
+ ref_gl1 TUs (src/client/refresh/gl1/* + files/*)
+ platform/ backend (static GetGameAPI/GetRefAPI loader, malloc hunk, main)
+ kept unix backend (network.c, signalhandler.c)
+ backends/generic/misc.c
-> link against libSDL2.a + libGL-phoenix.a + libv3d-phoenix.a -> ELF.

A successful link (undefined symbols -> 0) is the Phase-1 milestone; there is
no Pi run and no game assets this pass. Source is a pinned yQuake2 clone under
external/yquake2 (NOT committed); this script + platform/ + the vid.c patch in
tools/yquake2-port ARE committed.

Usage: python3 tools/yquake2-port/build-yquake2-phoenix.py
"""
import os, subprocess, sys
import concurrent.futures

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TC   = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
YQ2  = f"{ROOT}/external/yquake2"
SRC  = f"{YQ2}/src"
PLAT = f"{ROOT}/tools/yquake2-port/platform"
PATCH = f"{ROOT}/tools/yquake2-port/yquake2-phoenix-port.patch"
# Pinned upstream commit (record any bump here + in the README + commit msg).
YQ2_SHA = "e27fdcceb47769463b53b6d6f2e4c2ee572178b2"

# Ported SDL2 + Mesa/V3D GL archives.
BUILD  = f"{ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b"
SDLLIB = f"{BUILD}/lib/libSDL2.a"
SDLINC = f"{BUILD}/include"          # yQuake2 includes <SDL2/SDL.h>
GLINC  = f"{ROOT}/external/mesa/include"
GPU_LIBS = f"{ROOT}/tools/.gpu-libs"
GLLIB  = f"{GPU_LIBS}/libGL-phoenix.a"
V3DLIB = f"{GPU_LIBS}/libv3d-phoenix.a"

OBJ  = "/tmp/yq2obj"
ELF  = "/tmp/yquake2-phoenix"

# One flag set for every yQuake2 TU. No USE_SDL3/USE_OPENAL/USE_CURL/USE_XDG/
# DEDICATED_ONLY/HAVE_EXECINFO (all left off -> the guarded code compiles to
# stubs). NOUNCRYPT/IOAPI_NO_64 mirror the CMake defaults for the unzip TUs.
COMPAT = f"{PLAT}/pl_phoenix_compat.h"
# -fcommon: yQuake2 builds the client, game and renderer as separate .so's,
# each with its own tentative definition of the shared cvar_t* globals
# (vid_fullscreen, vid_gamma, gl1_stereo*, maxclients, dedicated, ...). Folded
# into one ELF, -fno-common (the GCC 10+ default) turns those into multiple-
# definition errors; -fcommon merges each name to a single storage slot, which
# is exactly right here since every copy is Cvar_Get()'d to the same cvar.
CFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-fwrapv",
          "-fcommon", "-Wno-error", "-DNDEBUG", '-DYQ2OSTYPE="Phoenix"',
          '-DYQ2ARCH="aarch64"', "-DNOUNCRYPT", "-DIOAPI_NO_64", "-include", COMPAT,
          f"-I{SRC}", f"-I{SDLINC}", f"-I{GLINC}"]

# ref_gl1 has an initialized global `modes` (GL texture-filter table) that
# collides with the client's initialized `modes` (video-mode menu table).
# Neither is tentative, so -fcommon can't merge them; rename the renderer's
# consistently across all gl1 TUs at the preprocessor level (self-contained:
# no non-gl1 TU references the renderer's `modes`).
# -DYQ2CAP_PHOENIX: on Phoenix the client renders into a scanout-backed FBO
# (sdl_phoenix_glctx.c), not FB0, so the visual-regression capture hook in
# gl1_sdl.c must read pixels back via phxgl_capture_gl (a GPU blit) instead of a
# plain glReadPixels (which returns noise on the scanout FBO). The native host
# reference build (yquake2 Makefile, renders into FB0) does NOT define this and
# takes the plain glReadPixels path. See scripts/quake2-host-capture.sh and
# docs/inprogress/2026-08-22-quake23-visual-harness.md.
GL1_CFLAGS = CFLAGS + ["-Dmodes=yq2_gl1_modes", "-DYQ2CAP_PHOENIX"]

# The SDL2 GL-context glue (winsys bridge: phxgl_*) is compiled with Mesa's
# include/define set, verbatim from build-sdl2-gltest.py MFLAGS.
MESA   = f"{ROOT}/external/mesa"
MCOMPAT = f"{ROOT}/tools/v3d-driver-port/phoenix_mesa_compat.h"
GLUE   = f"{ROOT}/sources/phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glctx.c"
# Shared libc/Mesa gap-filler (Zlib). Was a per-port copy (platform/pl_phoenix_glstubs.c);
# deduped onto the SDL2 port's glstubs. Its former lroundf stub is gone — libphoenix's
# libm now provides lroundf (libm/phoenix/exp.c), so -lm resolves it (same reasoning the
# quake3 port used to drop its rint stub).
GLSTUBS = f"{ROOT}/sources/phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glstubs.c"
MFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-Wno-error",
          "-Wno-undef", "-DUTIL_ARCH_LITTLE_ENDIAN=1", "-DUTIL_ARCH_BIG_ENDIAN=0",
          "-DHAVE_STRUCT_TIMESPEC", "-include", MCOMPAT,
          f"-I{MESA}/src", f"-I{MESA}/include", f"-I{MESA}/src/mesa",
          f"-I{MESA}/src/mapi", f"-I{MESA}/src/compiler",
          f"-I{MESA}/src/gallium/include", f"-I{MESA}/src/gallium/auxiliary",
          f"-I{MESA}/src/util", "-I/tmp/mesa-v3d-build/src", f"-I{SDLINC}"]

# ---- TU lists, transcribed from CMakeLists.txt (paths relative to src/) ----

# Client-Source (CMake 434-499). NOTE: this block already contains the
# integrated server (the sv_*.c below) -- do NOT also add the separate
# Server-Source target or every sv_*/common TU multiple-defines.
CLIENT = [
    "client/cl_cin", "client/cl_console", "client/cl_download", "client/cl_effects",
    "client/cl_entities", "client/cl_input", "client/cl_image", "client/cl_inventory",
    "client/cl_keyboard", "client/cl_lights", "client/cl_main", "client/cl_network",
    "client/cl_parse", "client/cl_particles", "client/cl_prediction", "client/cl_screen",
    "client/cl_tempentities", "client/cl_view",
    "client/curl/download", "client/curl/qcurl",
    "client/input/gyro",
    "client/menu/menu", "client/menu/qmenu", "client/menu/videomenu",
    "client/sound/ogg", "client/sound/openal", "client/sound/qal", "client/sound/sdl",
    "client/sound/sound", "client/sound/wave",
    "client/vid/vid",
    "common/argproc", "common/clientserver", "common/collision", "common/crc",
    "common/cmdparser", "common/cvar", "common/filesystem", "common/glob", "common/md4",
    "common/movemsg", "common/frame", "common/netchan", "common/pmove", "common/szone",
    "common/zone",
    "common/shared/flash", "common/shared/rand", "common/shared/shared",  # the shared-TU singles
    "common/unzip/ioapi", "common/unzip/unzip", "common/unzip/miniz/miniz",
    "common/unzip/miniz/miniz_tdef", "common/unzip/miniz/miniz_tinfl",
    "server/sv_cmd", "server/sv_conless", "server/sv_entities", "server/sv_game",
    "server/sv_init", "server/sv_main", "server/sv_save", "server/sv_send",
    "server/sv_user", "server/sv_world",
]

# SDL2 variant of Client-SDL-Source (CMake 507-510).
CLIENT_SDL = ["client/input/sdl2", "client/vid/glimp_sdl2"]

# Backends-Generic-Source (CMake 308-310).
GENERIC = ["backends/generic/misc"]

# Unix backend TUs kept as-is (no dlopen; portable POSIX/BSD-sockets).
UNIX_KEEP = ["backends/unix/network", "backends/unix/signalhandler"]

# Game-Source (CMake 349-398), MINUS the shared TUs (compiled once in CLIENT).
GAME = [
    "game/g_ai", "game/g_chase", "game/g_cmds", "game/g_combat", "game/g_func",
    "game/g_items", "game/g_main", "game/g_misc", "game/g_monster", "game/g_phys",
    "game/g_spawn", "game/g_svcmds", "game/g_target", "game/g_trigger", "game/g_turret",
    "game/g_utils", "game/g_weapon",
    "game/monster/berserker/berserker", "game/monster/boss2/boss2",
    "game/monster/boss3/boss3", "game/monster/boss3/boss31", "game/monster/boss3/boss32",
    "game/monster/brain/brain", "game/monster/chick/chick", "game/monster/flipper/flipper",
    "game/monster/float/float", "game/monster/flyer/flyer", "game/monster/gladiator/gladiator",
    "game/monster/gunner/gunner", "game/monster/hover/hover", "game/monster/infantry/infantry",
    "game/monster/insane/insane", "game/monster/medic/medic", "game/monster/misc/move",
    "game/monster/mutant/mutant", "game/monster/parasite/parasite", "game/monster/soldier/soldier",
    "game/monster/supertank/supertank", "game/monster/tank/tank",
    "game/player/client", "game/player/hud", "game/player/trail", "game/player/view",
    "game/player/weapon",
    "game/savegame/savegame",
]

# GL1-Source (CMake 597-621) -- ref_gl1 ONLY (gl3/soft/glad redefine GetRefAPI).
# MINUS shared.c + md4.c (compiled once in CLIENT).
GL1 = [
    "client/refresh/gl1/qgl", "client/refresh/gl1/gl1_draw", "client/refresh/gl1/gl1_image",
    "client/refresh/gl1/gl1_light", "client/refresh/gl1/gl1_lightmap",
    "client/refresh/gl1/gl1_main", "client/refresh/gl1/gl1_mesh", "client/refresh/gl1/gl1_misc",
    "client/refresh/gl1/gl1_model", "client/refresh/gl1/gl1_scrap", "client/refresh/gl1/gl1_surf",
    "client/refresh/gl1/gl1_warp", "client/refresh/gl1/gl1_sdl", "client/refresh/gl1/gl1_buffer",
    "client/refresh/files/common", "client/refresh/files/models", "client/refresh/files/pcx",
    "client/refresh/files/stb", "client/refresh/files/surf", "client/refresh/files/wal",
    "client/refresh/files/pvs",
]

# Phoenix backend (platform/) replacing backends/unix/{system,main,shared/hunk}.c.
# The libc/Mesa gap-filler (pthread_getcpuclockid) is the shared Zlib GLSTUBS, compiled
# below; the trace stub lives in the shared GL-context glue.
PHOENIX = ["pl_phoenix_sys", "pl_phoenix_main", "pl_phoenix_hunk"]


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def ensure_patch():
    """Apply the vid.c renderer-gate patch idempotently (fresh clones)."""
    if not os.path.isdir(f"{YQ2}/.git"):
        return
    # If reverse-apply checks clean, the patch is already in the tree.
    if run(["git", "-C", YQ2, "apply", "--reverse", "--check", PATCH]).returncode == 0:
        return
    r = run(["git", "-C", YQ2, "apply", PATCH])
    if r.returncode != 0:
        print(f"=== WARN: could not apply {PATCH} ===\n{r.stderr}")


def compile_one(unit, src, flags):
    obj = f"{OBJ}/{unit.replace('/', '_')}.o"
    r = run([TC] + flags + ["-o", obj, src])
    if r.returncode != 0:
        errs = [l for l in r.stderr.splitlines() if "error:" in l]
        return None, (errs[0] if errs else (r.stderr.splitlines() or ["?"])[0])
    return obj, None


def main():
    for p in (SDLLIB, GLLIB, V3DLIB):
        if not os.path.exists(p):
            print(f"missing prerequisite archive: {p}")
            if p == SDLLIB:
                print("  -> build it: scripts/build-sdl2-port.sh")
            return 1
    if not os.path.isdir(SRC):
        print(f"missing yQuake2 clone: {YQ2}")
        print(f"  -> git clone https://github.com/yquake2/yquake2 {YQ2} && "
              f"git -C {YQ2} checkout {YQ2_SHA}")
        return 1

    ensure_patch()
    os.makedirs(OBJ, exist_ok=True)

    objs, fail = [], []
    # (unit-list, source-directory, flags) triples.
    groups = [(CLIENT, SRC, CFLAGS), (CLIENT_SDL, SRC, CFLAGS), (GENERIC, SRC, CFLAGS),
              (UNIX_KEEP, SRC, CFLAGS), (GAME, SRC, CFLAGS), (GL1, SRC, GL1_CFLAGS),
              (PHOENIX, PLAT, CFLAGS)]
    # Flatten the groups into one work list, then compile across all cores: each
    # unit is an independent gcc subprocess (these bypass MAKEFLAGS, so parallelism
    # must be explicit). ex.map preserves order -> deterministic archive.
    work = [(u, f"{base}/{u}.c", flags) for units, base, flags in groups for u in units]
    def _one(w):
        u, src, flags = w
        if not os.path.exists(src):
            return (u, None, "MISSING SOURCE")
        obj, err = compile_one(u, src, flags)
        return (u, obj, err)
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, os.cpu_count() or 1)) as _ex:
        for u, obj, err in _ex.map(_one, work):
            (objs.append(obj) if obj else fail.append((u, err)))

    # SDL2 GL-context glue (winsys bridge) — Mesa flags, from sources/.
    glue_o, glue_err = compile_one("sdl_phoenix_glctx", GLUE, MFLAGS)
    (objs.append(glue_o) if glue_o else fail.append(("sdl_phoenix_glctx", glue_err)))

    # Shared libc/Mesa gap-filler (Zlib) — plain CFLAGS, from sources/.
    stubs_o, stubs_err = compile_one("sdl_phoenix_glstubs", GLSTUBS, CFLAGS)
    (objs.append(stubs_o) if stubs_o else fail.append(("sdl_phoenix_glstubs", stubs_err)))

    total = sum(len(g[0]) for g in groups) + 2
    print(f"\n=== compile: {len(objs)}/{total} TUs OK ===")
    if fail:
        print(f"--- {len(fail)} FAILED ---")
        for u, e in fail:
            print(f"  [{u}] {e}")
        return 1

    # Circular refs (SDL <-> renderer <-> Mesa; libGL <-> libv3d) -> group.
    # 32 MB stack matches the canonical rpi4 GL binary link.
    link = [TC] + objs + [
        "-Wl,--start-group", SDLLIB, GLLIB, V3DLIB, "-Wl,--end-group",
        # 4 MB stack (was 32 MB). Quake2 uses only a few MB; the 32 MB was copied from the
        # deep-recursion vkQuake link. Phoenix commits PT_GNU_STACK eagerly at exec, so a
        # smaller stack cuts the exec-time eager-commit footprint (~58->~30 MB total, below
        # the reliable ~32 MB of sdl2-gltest) — a candidate fix for the intermittent
        # large-binary NFS-exec hang (see docs/inprogress/2026-08-05-large-binary-exec-investigation.md).
        "-lstdc++", "-lm", "-Wl,-z,stack-size=4194304", "-o", ELF]
    r = run(link)
    if r.returncode == 0:
        print(f"=== LINK OK -> {ELF} ===")
        subprocess.run([f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-size", ELF])
        return 0

    undef = sorted(set(l.split("undefined reference to ")[1].strip().strip("`'")
                       for l in r.stderr.splitlines() if "undefined reference to" in l))
    mdef = sorted(set(l for l in r.stderr.splitlines() if "multiple definition" in l))
    print(f"=== LINK FAILED: {len(undef)} undefined, {len(mdef)} multiple-def ===")
    for s in undef:
        print(f"  U {s}")
    for l in mdef[:40]:
        print(f"  M {l}")
    other = [l for l in r.stderr.splitlines()
             if "undefined reference" not in l and "multiple definition" not in l
             and ("error" in l.lower() or "cannot" in l.lower())][:10]
    for l in other:
        print(f"  {l}")
    return 2


if __name__ == "__main__":
    sys.exit(main())
