#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Phoenix Systems. Author: Witold Bołt.
# Build recipe for the QuakeSpasm Phoenix port (GPL-2.0-or-later); see COPYING.
"""Build Quakespasm for Phoenix (aarch64a72) on the ported Mesa GL stack (V3D).

Stage 1 (this script, "probe" mode): compile every PORTABLE Quakespasm TU — the GL
renderer (GLOBJS) + the engine core — against the aarch64-phoenix toolchain, our
ported Mesa GL headers, and a minimal SDL2 shim (sdl-shim/). The SDL *platform* TUs
(gl_vidsdl, in_sdl, snd_sdl, cd_sdl, sys_sdl_unix, pl_linux, main_sdl) are EXCLUDED —
they are replaced by Phoenix platform shims (vid->FBO/fb0, input->/dev/kbd0,
timing->clock, audio stub) written directly against Phoenix, not SDL.

Output: object files under /tmp/qsobj, and a printed pass/fail summary so the
remaining compile gaps are explicit. Linking against libGL-phoenix.a +
libv3d-phoenix.a is a later stage (after the platform shims exist).

Usage: python3 build-quakespasm-phoenix.py
"""
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TC   = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
AR   = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc-ar"
Q    = f"{ROOT}/external/quakespasm/Quake"
SHIM = f"{ROOT}/tools/quakespasm-port/sdl-shim"
GLINC = f"{ROOT}/external/mesa/include"
OBJ  = "/tmp/qsobj"

# SDL platform TUs we replace wholesale — never compiled here.
EXCLUDE = {"gl_vidsdl", "in_sdl", "snd_sdl", "cd_sdl", "sys_sdl_unix",
           "pl_linux", "main_sdl", "net_bsd"}  # net_bsd: BSD-sockets platform glue

# GL renderer + engine core (from the Makefile OBJS, minus EXCLUDE).
GLOBJS = ["gl_refrag", "gl_rlight", "gl_rmain", "gl_fog", "gl_rmisc", "r_part",
          "r_world", "gl_screen", "gl_sky", "gl_warp", "gl_draw", "image",
          "gl_texmgr", "gl_mesh", "r_sprite", "r_alias", "r_brush", "gl_model"]
CORE = ["strlcat", "strlcpy", "net_dgrm", "net_loop", "net_main", "net_udp",
        "chase", "cl_demo", "cl_input", "cl_main", "cl_parse", "cl_tent",
        "console", "keys", "menu", "sbar", "view", "wad", "cmd", "common",
        "miniz", "crc", "cvar", "cfgfile", "host", "host_cmd", "mathlib",
        "pr_cmds", "pr_edict", "pr_exec", "sv_main", "sv_move", "sv_phys",
        "sv_user", "world", "zone", "snd_dma", "snd_mix", "snd_mem", "bgmusic",
        "cd_null", "snd_codec"]   # portable CD-null + codec dispatcher

PLAT = f"{ROOT}/tools/quakespasm-port/platform"
MESA = f"{ROOT}/external/mesa"
COMPAT = f"{ROOT}/tools/v3d-driver-port/phoenix_mesa_compat.h"
GPU_LIBS = f"{ROOT}/tools/.gpu-libs"  # stable home for the prebuilt engine archives (was /tmp)
GLLIB = f"{GPU_LIBS}/libGL-phoenix.a"
V3DLIB = f"{GPU_LIBS}/libv3d-phoenix.a"
ELF = "/tmp/quakespasm-phoenix"

# Quake-side flags (Quake TUs + the Quake-facing platform shims).
# QSS_PHOENIX enables gl_screen.c's Phoenix capture path (qsv3d_capture_gl).
QFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-Wno-error",
          "-DQSS_PHOENIX=1",
          f"-I{SHIM}", f"-I{Q}", f"-I{GLINC}"]
# Mesa-side flags (glctx only) — the endianness/timespec -D's + include order the
# Mesa driver build uses (else u_endian #errors and struct timespec redefines).
MFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-Wno-error",
          "-Wno-undef", "-DUTIL_ARCH_LITTLE_ENDIAN=1", "-DUTIL_ARCH_BIG_ENDIAN=0",
          "-DHAVE_STRUCT_TIMESPEC", "-DQSS_PHOENIX=1", "-include", COMPAT,
          f"-I{MESA}/src", f"-I{MESA}/include", f"-I{MESA}/src/mesa",
          f"-I{MESA}/src/mapi", f"-I{MESA}/src/compiler",
          f"-I{MESA}/src/gallium/include", f"-I{MESA}/src/gallium/auxiliary",
          f"-I{MESA}/src/util", "-I/tmp/mesa-v3d-build/src"]

QUAKE_SHIMS = ["pl_phoenix_sys", "pl_phoenix_snd", "pl_phoenix_in",
               "pl_phoenix_main", "pl_phoenix_vid", "pl_phoenix_stubs"]  # Quake-side flags
MESA_SHIMS = ["pl_phoenix_glctx"]                     # Mesa-side flags

def compile_one(src, flags, obj):
    r = subprocess.run([TC] + flags + ["-o", obj, src], capture_output=True, text=True)
    if r.returncode == 0:
        return None
    errs = [l for l in r.stderr.splitlines() if "error:" in l]
    return errs[0] if errs else (r.stderr.splitlines()[0] if r.stderr else "?")

def main():
    os.makedirs(OBJ, exist_ok=True)
    units = [u for u in (GLOBJS + CORE) if u not in EXCLUDE]
    objs, fail = [], []

    for u in units:
        src = f"{Q}/{u}.c"
        if not os.path.exists(src):
            fail.append((u, "MISSING SOURCE")); continue
        e = compile_one(src, QFLAGS, f"{OBJ}/{u}.o")
        (fail.append((u, e)) if e else objs.append(f"{OBJ}/{u}.o"))
    for u in QUAKE_SHIMS:
        e = compile_one(f"{PLAT}/{u}.c", QFLAGS, f"{OBJ}/{u}.o")
        (fail.append((u, e)) if e else objs.append(f"{OBJ}/{u}.o"))
    for u in MESA_SHIMS:
        e = compile_one(f"{PLAT}/{u}.c", MFLAGS, f"{OBJ}/{u}.o")
        (fail.append((u, e)) if e else objs.append(f"{OBJ}/{u}.o"))

    total = len(units) + len(QUAKE_SHIMS) + len(MESA_SHIMS)
    print(f"\n=== compile: {len(objs)}/{total} TUs OK ===")
    if fail:
        print(f"--- {len(fail)} FAILED ---")
        for u, e in fail:
            print(f"  [{u}] {e}")
        return 1

    # Archive all objects into libquakespasm.a so a binary.mk program (rpi4-quake)
    # can link it (+ libGL + libv3d) and bundle into loader.disk. main() lives in
    # pl_phoenix_main.o inside the archive; crt0 pulls it, which pulls the rest.
    QSLIB = f"{GPU_LIBS}/libquakespasm.a"  # stable home (was /tmp)
    subprocess.run(["rm", "-f", QSLIB])
    ar = subprocess.run([AR, "rcs", QSLIB] + objs, capture_output=True, text=True)
    if ar.returncode != 0:
        print(f"=== AR FAILED ===\n{ar.stderr}")
        return 1
    print(f"=== archived {len(objs)} objs -> {QSLIB} ===")

    # Link the full ELF; capture undefined-symbol gaps (the closure-reduction recon).
    # 32 MB stack matches the canonical _user/rpi4-quake binary.mk link
    # (-Wl,-z,stack-size=33554432); Quake's deep render/host call chains overflow
    # the default. With it, this standalone ELF is directly stageable (the _user
    # stub TU is just a binary.mk placeholder marker, no functional code).
    link = [TC] + objs + ["-Wl,--start-group", GLLIB, V3DLIB, "-Wl,--end-group",
                          "-lstdc++", "-lm", "-Wl,-z,stack-size=33554432", "-o", ELF]
    r = subprocess.run(link, capture_output=True, text=True)
    if r.returncode == 0:
        print(f"=== LINK OK -> {ELF} ===")
        return 0
    undef = sorted(set(l.split("undefined reference to ")[1].strip().strip("`'")
                       for l in r.stderr.splitlines() if "undefined reference to" in l))
    print(f"=== LINK FAILED: {len(undef)} undefined symbols ===")
    for s in undef:
        print(f"  U {s}")
    other = [l for l in r.stderr.splitlines()
             if "undefined reference" not in l and ("error" in l.lower() or "cannot" in l.lower())][:10]
    if other:
        print("--- other link errors ---")
        for l in other:
            print(f"  {l}")
    return 2

if __name__ == "__main__":
    sys.exit(main())
