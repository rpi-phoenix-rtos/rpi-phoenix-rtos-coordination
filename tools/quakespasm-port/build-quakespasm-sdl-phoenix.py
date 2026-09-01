#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Phoenix Systems. Author: Witold Bołt.
# Build recipe for the QuakeSpasm Phoenix port (GPL-2.0-or-later); see COPYING.
"""Build QuakeSpasm for Phoenix (aarch64a72) on the REAL ported SDL2 + Mesa/V3D.

This is the "real-SDL" variant of the QuakeSpasm port and a PARALLEL ARTIFACT to
build-quakespasm-phoenix.py: it never overwrites the proven flagship build's
outputs (libquakespasm.a / /tmp/quakespasm-phoenix). Where the flagship replaces
QuakeSpasm's SDL video/input/audio backends with hand-written Phoenix shims
(pl_phoenix_vid/in/snd.c) driving V3D + /dev/fb0 + /dev/kbd0 directly, THIS build
compiles the game's STOCK SDL2 backends (gl_vidsdl.c, in_sdl.c, snd_sdl.c) and
links the real ported SDL2 (libSDL2.a) — the same template the yQuake2 and
quake3e ports use.

  GL renderer + engine core (GLOBJS + CORE)
+ stock SDL2 backends (gl_vidsdl, in_sdl, snd_sdl)      <-- the change vs flagship
+ Phoenix sys/main/stubs shims (pl_phoenix_sys/main/stubs)
+ the SDL2 GL-context winsys glue (sdl_phoenix_glctx.c) — driven now BY SDL2
-> link against libSDL2.a + libGL-phoenix.a + libv3d-phoenix.a -> ELF.

A clean static link (undefined symbols -> 0) is the Burst-1 milestone; there is
no Pi run this pass (the owner runs HW validation in Burst 2).

Header strategy: -DNO_SDL_CONFIG + -DUSE_SDL2 selects quakedef.h's branch that
includes Mesa's <GL/gl.h>/<GL/glext.h> BEFORE the real <SDL2/SDL.h> +
<SDL2/SDL_opengl.h>. Mesa's gl.h and SDL's SDL_opengl.h share the __gl_h_ guard,
so the Mesa GL headers (which match libGL-phoenix) win and SDL's GL block is
neutralised — no clash, no duplicate GL declarations. The fake sdl-shim/ is NOT
on the include path here.

Usage: python3 tools/quakespasm-port/build-quakespasm-sdl-phoenix.py

Runtime note (HW-validated 2026-08-10): unlike the flagship (whose Phoenix video
shim forces the native 1920x1080), the STOCK SDL2 backend (gl_vidsdl.c) honours
QuakeSpasm's default vid_width/vid_height cvars (800x600). Rendering 800x600 into
the 1920x1080 /dev/fb0 scanout gives a garbled top band. Fix: set the native
resolution in id1/config.cfg (read early by VID_Init's read_vars[]):
    vid_width "1920"
    vid_height "1080"
    vid_fullscreen "1"
Then the SDL video driver's fb0 mode (1920x1080) matches the render target and
QuakeSpasm renders fullscreen on V3D/HDMI (same pattern as the yQuake2 port's
r_customwidth/r_customheight). Verified: boots -> phxgl GL up (V3D 4.2, Mesa) ->
SDL audio=phoenix -> map "start" -> in-game -> fullscreen 1920x1080, 0 faults.
"""
import os, subprocess, sys
import concurrent.futures

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TC   = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
AR   = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc-ar"
Q    = f"{ROOT}/external/quakespasm/Quake"
GLINC = f"{ROOT}/external/mesa/include"
OBJ  = "/tmp/qsobj-sdl"          # distinct from the flagship's /tmp/qsobj

# Real ported SDL2. yQuake2/quake3 link this identically.
BUILD  = f"{ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b"
SDLLIB = f"{BUILD}/lib/libSDL2.a"
SDLINC = f"{BUILD}/include"       # real SDL headers live in include/SDL2/

# SDL platform TUs we STILL replace wholesale (Phoenix sys/main own these):
#   cd_sdl       -> cd_null (portable CD stub, in CORE)
#   sys_sdl_unix -> pl_phoenix_sys.c
#   main_sdl     -> pl_phoenix_main.c (owns main(); do NOT compile main_sdl)
#   pl_linux     -> PL_* hooks provided by pl_phoenix_stubs/main (SDL2-guarded)
#   net_bsd      -> pl_phoenix_stubs.c (net driver tables)
# gl_vidsdl / in_sdl / snd_sdl are NOW compiled (the real SDL2 backends).
EXCLUDE = {"cd_sdl", "sys_sdl_unix", "pl_linux", "main_sdl", "net_bsd"}

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

# Stock SDL2 backends (the flagship replaces these with pl_phoenix_vid/in/snd).
SDLBK = ["gl_vidsdl", "in_sdl", "snd_sdl"]

PLAT = f"{ROOT}/tools/quakespasm-port/platform"
MESA = f"{ROOT}/external/mesa"
COMPAT = f"{ROOT}/tools/v3d-driver-port/phoenix_mesa_compat.h"
# Shared GL-context glue (Zlib): the SDL2 video driver inside libSDL2.a calls its
# phxgl_* entry points. Same file the flagship links; here it is driven by SDL2,
# not by pl_phoenix_vid.c.
GLUE = f"{ROOT}/sources/phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glctx.c"
GPU_LIBS = f"{ROOT}/tools/.gpu-libs"  # stable home for the prebuilt engine archives
GLLIB = f"{GPU_LIBS}/libGL-phoenix.a"
V3DLIB = f"{GPU_LIBS}/libv3d-phoenix.a"
QSLIB = f"{GPU_LIBS}/libquakespasm-sdl.a"     # NEW archive (flagship: libquakespasm.a)
ELF = "/tmp/quakespasm-sdl-phoenix"           # NEW ELF (flagship: /tmp/quakespasm-phoenix)

# QSS_PHOENIX enables gl_screen.c's Phoenix screen-capture path (opt-in, expensive);
# kept identical to the flagship. Default builds ship WITHOUT it.
CAPTURE = os.environ.get("QS_CAPTURE", "0") not in ("", "0")
_cap_def = ["-DQSS_PHOENIX=1"] if CAPTURE else []

# Quake-side flags (Quake TUs + the stock SDL2 backends). -DNO_SDL_CONFIG +
# -DUSE_SDL2 select the Mesa-GL-first / real-SDL2 include branch of quakedef.h
# (see header strategy in the module docstring). The fake sdl-shim is dropped;
# -I{SDLINC} resolves <SDL2/SDL.h>/<SDL2/SDL_opengl.h>, -I{SDLINC}/SDL2 resolves
# the bare "SDL.h" form used by a few TUs.
QFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-Wno-error",
          "-DUSE_SDL2", "-DNO_SDL_CONFIG", *_cap_def,
          f"-I{Q}", f"-I{GLINC}", f"-I{SDLINC}", f"-I{SDLINC}/SDL2"]
# Mesa-side flags (glctx only) — the endianness/timespec -D's + include order the
# Mesa driver build uses (else u_endian #errors and struct timespec redefines).
MFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-Wno-error",
          "-Wno-undef", "-DUTIL_ARCH_LITTLE_ENDIAN=1", "-DUTIL_ARCH_BIG_ENDIAN=0",
          "-DHAVE_STRUCT_TIMESPEC", *_cap_def, "-include", COMPAT,
          f"-I{MESA}/src", f"-I{MESA}/include", f"-I{MESA}/src/mesa",
          f"-I{MESA}/src/mapi", f"-I{MESA}/src/compiler",
          f"-I{MESA}/src/gallium/include", f"-I{MESA}/src/gallium/auxiliary",
          f"-I{MESA}/src/util", "-I/tmp/mesa-v3d-build/src", f"-I{SDLINC}"]

# Phoenix sys/main/stubs shims. The flagship also lists pl_phoenix_vid/in/snd;
# here the stock SDL2 backends (SDLBK) provide video/input/audio instead.
QUAKE_SHIMS = ["pl_phoenix_sys", "pl_phoenix_main", "pl_phoenix_stubs"]
# GL-context glue: the shared Zlib GLUE, compiled with Mesa-side flags (below).

def compile_one(src, flags, obj):
    """Compile one TU. None on success, else the compiler's COMPLETE stderr
    prefixed by the exact command.

    This used to return only the first `error:` line, which threw away the
    diagnostic that actually mattered whenever the first line was not the
    informative one -- a build that fails has to show why."""
    cmd = [TC] + flags + ["-o", obj, src]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode == 0:
        return None
    return "$ " + " ".join(cmd) + "\n" + (r.stderr.strip() or "(no stderr; exit %d)" % r.returncode)

def main():
    for p in (SDLLIB, GLLIB, V3DLIB):
        if not os.path.exists(p):
            print(f"missing prerequisite archive: {p}")
            if p == SDLLIB:
                print("  -> build it: scripts/build-sdl2-port.sh")
            return 1

    os.makedirs(OBJ, exist_ok=True)
    units = [u for u in (GLOBJS + CORE + SDLBK) if u not in EXCLUDE]
    objs, fail = [], []

    # Parallel compile across all cores (ex.map preserves order -> deterministic archive).
    def _compile_unit(u):
        src = f"{Q}/{u}.c"
        obj = f"{OBJ}/{u}.o"
        if not os.path.exists(src):
            return (u, obj, "MISSING SOURCE")
        return (u, obj, compile_one(src, QFLAGS, obj))
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, os.cpu_count() or 1)) as _ex:
        for u, obj, e in _ex.map(_compile_unit, units):
            (fail.append((u, e)) if e else objs.append(obj))
    for u in QUAKE_SHIMS:
        e = compile_one(f"{PLAT}/{u}.c", QFLAGS, f"{OBJ}/{u}.o")
        (fail.append((u, e)) if e else objs.append(f"{OBJ}/{u}.o"))
    e = compile_one(GLUE, MFLAGS, f"{OBJ}/sdl_phoenix_glctx.o")
    (fail.append(("sdl_phoenix_glctx", e)) if e else objs.append(f"{OBJ}/sdl_phoenix_glctx.o"))

    total = len(units) + len(QUAKE_SHIMS) + 1
    print(f"\n=== compile: {len(objs)}/{total} TUs OK ===")
    if fail:
        print(f"--- {len(fail)} FAILED ---")
        for u, e in fail:
            first = next((l for l in e.splitlines() if "error:" in l),
                         (e.splitlines() or ["?"])[0])
            print(f"  [{u}] {first}")
        print("\n--- full compiler output per failure ---")
        for u, e in fail:
            print(f"===== {u} =====\n{e}\n")
        sys.stdout.flush()
        return 1

    # Archive all objects into libquakespasm-sdl.a (the parallel artifact — the
    # flagship's libquakespasm.a is left untouched). main() lives in
    # pl_phoenix_main.o; crt0 pulls it, which pulls the rest.
    subprocess.run(["rm", "-f", QSLIB])
    ar = subprocess.run([AR, "rcs", QSLIB] + objs, capture_output=True, text=True)
    if ar.returncode != 0:
        print(f"=== AR FAILED ===\n{ar.stderr}")
        return 1
    print(f"=== archived {len(objs)} objs -> {QSLIB} ===")

    # Link the full ELF; capture undefined-symbol gaps. Circular refs
    # (SDL <-> renderer <-> Mesa; libGL <-> libv3d) -> group. 32 MB stack matches
    # the flagship link (Quake's deep render/host call chains overflow the default).
    link = [TC] + objs + ["-Wl,--start-group", SDLLIB, GLLIB, V3DLIB, "-Wl,--end-group",
                          "-lstdc++", "-lm", "-Wl,-z,stack-size=33554432", "-o", ELF]
    r = subprocess.run(link, capture_output=True, text=True)
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
    # Then the COMPLETE linker output. Filtering it to lines containing
    # "error"/"cannot" (what this used to do, capped at 10) could reduce a real
    # failure to a bare "collect2: error: ld returned 1 exit status" with no
    # indication of the cause -- reported from a Docker build on macOS.
    print("--- link command ---")
    print("$ " + " ".join(link))
    print("--- complete linker output ---")
    print(r.stderr.strip() or "(no stderr)")
    if r.stdout.strip():
        print("--- linker stdout ---")
        print(r.stdout.strip())
    sys.stdout.flush()
    return 2

if __name__ == "__main__":
    sys.exit(main())
