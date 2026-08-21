#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Phoenix Systems. Author: Witold Bołt.
# Build recipe for the quake3e Phoenix port (GPL-2.0); see external/quake3e/COPYING.txt.
"""Cross-link quake3e into a single static aarch64-phoenix ELF (Phase 1).

quake3e is structurally simpler to fold into one ELF than yQuake2: the game /
cgame / ui modules are interpreted QVM bytecode shipped in the pak (data, not C
we compile or link), so there is NO GetGameAPI folding and NO game-TU list. The
renderer is compiled in (USE_RENDERER_DLOPEN=0 -> the engine calls the renderer's
GetRefAPI directly, resolved at link time), and the QVM JIT is disabled
(NO_VM_COMPILED -> vm.c forces the pure vm_interpreted.c interpreter), so neither
of Phoenix's two missing seams (dlopen for game/renderer, mprotect(PROT_EXEC) for
the JIT) is exercised.

Config knobs (mirroring quake3e Makefile): USE_RENDERER_DLOPEN=0
RENDERER_DEFAULT=opengl (code/renderer, fixed-function GL 1.x fitting V3D's GL
2.1) USE_SDL=1 USE_VULKAN=0 USE_CURL=0 USE_OGG_VORBIS=0 NO_VM_COMPILED, bundled
libjpeg (USE_SYSTEM_JPEG off).

  qcommon core (incl. vm_interpreted, unzip, net_ip)
+ client (incl. integrated server, sound, cinematics)
+ botlib
+ renderercommon + renderer (opengl1)
+ code/sdl (SDL2 client backend: glimp/input/snd/gamma)
+ bundled libjpeg
+ Phoenix backend (tools/quake3-port/platform/: forked unix_main + unix_shared,
  kept linux_signals) + the SDL2 GL-context glue (sdl_phoenix_glctx.c)
-> link against libSDL2.a + libGL-phoenix.a + libv3d-phoenix.a -> ELF.

A successful link (undefined symbols -> 0) is the Phase-1 milestone; there is no
Pi run and no game assets this pass. Source is a pinned quake3e clone under
external/quake3e (NOT committed); this script + platform/ + the source patch in
tools/quake3-port ARE committed.

Usage: python3 tools/quake3-port/build-quake3e-phoenix.py
"""
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TC   = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
Q3   = f"{ROOT}/external/quake3e"
CODE = f"{Q3}/code"
PLAT = f"{ROOT}/tools/quake3-port/platform"
PATCH = f"{ROOT}/tools/quake3-port/quake3e-phoenix-port.patch"
# Pinned upstream commit (record any bump here + in the README + commit msg).
Q3_SHA = "623982900a132e5c812dcb5231a430f28fafabeb"

# Ported SDL2 + Mesa/V3D GL archives.
BUILD  = f"{ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b"
SDLLIB = f"{BUILD}/lib/libSDL2.a"
SDLINC = f"{BUILD}/include"          # our SDL2 headers live in include/SDL2
GLINC  = f"{ROOT}/external/mesa/include"
GPU_LIBS = f"{ROOT}/tools/.gpu-libs"
GLLIB  = f"{GPU_LIBS}/libGL-phoenix.a"
V3DLIB = f"{GPU_LIBS}/libv3d-phoenix.a"

OBJ  = "/tmp/q3obj"
ELF  = "/tmp/quake3e-phoenix"

# One flag set for every quake3e engine TU. The force-included compat header
# defuses the msg_t type clash (Phoenix SysV-IPC msg_t vs Q3 network msg_t) with
# zero Q3-source edits by pre-parsing the socket header chain with the Phoenix
# type renamed. -fcommon: the modular .so-era build gives several TUs their own
# tentative copy of shared cvar_t* globals; folded into one ELF, -fno-common
# (GCC 10+ default) would multiple-define them.
COMPAT = f"{PLAT}/pl_phoenix_compat.h"
INCS = ["-I" + CODE, f"-I{CODE}/qcommon", f"-I{CODE}/renderercommon",
        f"-I{CODE}/renderer", f"-I{CODE}/client", f"-I{CODE}/libjpeg",
        f"-I{SDLINC}/SDL2", f"-I{SDLINC}", f"-I{GLINC}"]
# NO -DUSE_RENDERER_DLOPEN (=0), NO -DUSE_CURL, NO -DUSE_VULKAN_API,
# NO -DUSE_SYSTEM_JPEG (bundle libjpeg). -DUSE_OPENGL_API + -DNO_VM_COMPILED +
# -DUSE_LOCAL_HEADERS=1 match the opengl1/interpreted/local-SDL-header config.
CFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-fwrapv",
          "-fcommon", "-Wno-error", "-DNDEBUG",
          "-DUSE_OPENGL_API", "-DUSE_LOCAL_HEADERS=1", "-include", COMPAT] + INCS

# botlib TUs select their engine-build include set with -DBOTLIB (mirrors the
# Makefile's do_cc_botlib rule); without it their local headers never pull the
# prerequisites and source_t/punctuation_t/fielddef_t are undefined.
BOTLIB_CFLAGS = CFLAGS + ["-DBOTLIB"]

# The opengl1 renderer TUs get -DQ3CAP_PHOENIX so tr_init.c's visual-regression
# capture hook reads the just-rendered frame back via phxgl_capture_gl (the V3D
# scanout-FBO blit+readback in the SDL2 glue) instead of a plain glReadPixels,
# which returns noise on the render-to-scanout path. The native host reference
# build (renders into FB0) does NOT define it and uses the plain glReadPixels
# #else branch. Same design as Q1 (QSS_PHOENIX) / Q2 (YQ2CAP_PHOENIX).
REND1_CFLAGS = CFLAGS + ["-DQ3CAP_PHOENIX"]

# The SDL2 GL-context glue (winsys bridge: phxgl_*) is compiled with Mesa's
# include/define set, verbatim from the yQuake2 / sdl2-gltest MFLAGS.
MESA   = f"{ROOT}/external/mesa"
MCOMPAT = f"{ROOT}/tools/v3d-driver-port/phoenix_mesa_compat.h"
GLUE   = f"{ROOT}/sources/phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glctx.c"
# Shared libc/Mesa gap-filler (Zlib). Was a per-port copy (platform/pl_phoenix_stubs.c);
# deduped onto the SDL2 port's glstubs, which provides the same pthread_getcpuclockid.
GLSTUBS = f"{ROOT}/sources/phoenix-rtos-ports/sdl2/glue/sdl_phoenix_glstubs.c"
MFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-Wno-error",
          "-Wno-undef", "-DUTIL_ARCH_LITTLE_ENDIAN=1", "-DUTIL_ARCH_BIG_ENDIAN=0",
          "-DHAVE_STRUCT_TIMESPEC", "-include", MCOMPAT,
          f"-I{MESA}/src", f"-I{MESA}/include", f"-I{MESA}/src/mesa",
          f"-I{MESA}/src/mapi", f"-I{MESA}/src/compiler",
          f"-I{MESA}/src/gallium/include", f"-I{MESA}/src/gallium/auxiliary",
          f"-I{MESA}/src/util", "-I/tmp/mesa-v3d-build/src", f"-I{SDLINC}"]

# ---- TU lists, transcribed from Makefile (Q3OBJ/Q3REND1OBJ/JPGOBJ) ----
# Grouped by *source directory* because the Makefile flattens all object names
# to $(B)/client|ded/ via pattern rules across many dirs.

QCOMMON = [
    "cm_load", "cm_patch", "cm_polylib", "cm_test", "cm_trace",
    "cmd", "common", "cvar", "files", "history", "keys", "md4", "md5", "msg",
    "net_chan", "net_ip", "huffman", "huffman_static", "q_math", "q_shared",
    "unzip", "puff",
    "vm", "vm_interpreted", "vm_aarch64",   # aarch64 JIT (Phoenix honors PROT_EXEC) + interpreter fallback
]

CLIENT = [
    "cl_cgame", "cl_cin", "cl_console", "cl_input", "cl_keys", "cl_main",
    "cl_net_chan", "cl_parse", "cl_scrn", "cl_ui", "cl_avi", "cl_jpeg",
    "snd_adpcm", "snd_dma", "snd_mem", "snd_mix", "snd_wavelet",
    "snd_main", "snd_codec", "snd_codec_wav",
]

SERVER = [
    "sv_bot", "sv_ccmds", "sv_client", "sv_filter", "sv_game", "sv_init",
    "sv_main", "sv_net_chan", "sv_snapshot", "sv_world",
]

BOTLIB = [
    "be_aas_bspq3", "be_aas_cluster", "be_aas_debug", "be_aas_entity",
    "be_aas_file", "be_aas_main", "be_aas_move", "be_aas_optimize",
    "be_aas_reach", "be_aas_route", "be_aas_routealt", "be_aas_sample",
    "be_ai_char", "be_ai_chat", "be_ai_gen", "be_ai_goal", "be_ai_move",
    "be_ai_weap", "be_ai_weight", "be_ea", "be_interface",
    "l_crc", "l_libvar", "l_log", "l_memory", "l_precomp", "l_script", "l_struct",
]

# Bundled libjpeg (USE_SYSTEM_JPEG off) -- JPGOBJ.
JPEG = [
    "jaricom", "jcapimin", "jcapistd", "jcarith", "jccoefct", "jccolor",
    "jcdctmgr", "jchuff", "jcinit", "jcmainct", "jcmarker", "jcmaster",
    "jcomapi", "jcparam", "jcprepct", "jcsample", "jctrans", "jdapimin",
    "jdapistd", "jdarith", "jdatadst", "jdatasrc", "jdcoefct", "jdcolor",
    "jddctmgr", "jdhuff", "jdinput", "jdmainct", "jdmarker", "jdmaster",
    "jdmerge", "jdpostct", "jdsample", "jdtrans", "jerror", "jfdctflt",
    "jfdctfst", "jfdctint", "jidctflt", "jidctfst", "jidctint", "jmemmgr",
    "jmemnobs", "jquant1", "jquant2", "jutils",
]

# renderercommon TUs used by Q3REND1OBJ (the Makefile prefixes them $(B)/rend1/
# but resolves via RCDIR); the rest of Q3REND1OBJ comes from code/renderer/.
RENDCOMMON = [
    "tr_font", "tr_image_png", "tr_image_jpg", "tr_image_bmp", "tr_image_tga",
    "tr_image_pcx", "tr_noise",
]

REND1 = [
    "tr_animation", "tr_arb", "tr_backend", "tr_bsp", "tr_cmds", "tr_curve",
    "tr_flares", "tr_image", "tr_init", "tr_light", "tr_main", "tr_marks",
    "tr_mesh", "tr_model", "tr_model_iqm", "tr_scene", "tr_shade",
    "tr_shade_calc", "tr_shader", "tr_shadows", "tr_sky", "tr_surface",
    "tr_vbo", "tr_world",
]

# SDL2 client backend (USE_SDL=1 branch of Q3OBJ).
SDLBK = ["sdl_glimp", "sdl_gamma", "sdl_input", "sdl_snd"]

# Unix backend TU kept verbatim (no dlopen; pure POSIX signal handling).
UNIX_KEEP = ["linux_signals"]

# Phoenix backend (platform/): forks of unix_main.c + unix_shared.c with the
# dlopen seam stubbed and the unused SysV/pwd headers dropped. The libc/Mesa
# gap-filler (pthread_getcpuclockid) is the shared Zlib GLSTUBS, not a per-port copy.
PHOENIX = ["pl_phoenix_main", "pl_phoenix_sys"]


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def ensure_patch():
    """Apply the q_platform.h / qgl.h Phoenix patch idempotently."""
    if not os.path.isdir(f"{Q3}/.git") or not os.path.exists(PATCH):
        return
    if run(["git", "-C", Q3, "apply", "--reverse", "--check", PATCH]).returncode == 0:
        return
    r = run(["git", "-C", Q3, "apply", PATCH])
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
    if not os.path.isdir(CODE):
        print(f"missing quake3e clone: {Q3}")
        print(f"  -> git clone https://github.com/ec-/quake3e {Q3} && "
              f"git -C {Q3} checkout {Q3_SHA}")
        return 1

    ensure_patch()
    os.makedirs(OBJ, exist_ok=True)

    # (unit-list, source-directory, flags) triples.
    groups = [
        (QCOMMON,    f"{CODE}/qcommon",       CFLAGS),
        (CLIENT,     f"{CODE}/client",        CFLAGS),
        (SERVER,     f"{CODE}/server",        CFLAGS),
        (BOTLIB,     f"{CODE}/botlib",        BOTLIB_CFLAGS),
        (JPEG,       f"{CODE}/libjpeg",       CFLAGS),
        (RENDCOMMON, f"{CODE}/renderercommon", CFLAGS),
        (REND1,      f"{CODE}/renderer",      REND1_CFLAGS),
        (SDLBK,      f"{CODE}/sdl",           CFLAGS),
        (UNIX_KEEP,  f"{CODE}/unix",          CFLAGS),
        (PHOENIX,    PLAT,                    CFLAGS),
    ]
    objs, fail = [], []
    for units, base, flags in groups:
        for u in units:
            src = f"{base}/{u}.c"
            if not os.path.exists(src):
                fail.append((u, "MISSING SOURCE")); continue
            obj, err = compile_one(u, src, flags)
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
    # 4 MB stack matches the yQuake2 rpi4 GL binary link (Phoenix commits
    # PT_GNU_STACK eagerly at exec; keep the exec footprint modest).
    link = [TC] + objs + [
        "-Wl,--start-group", SDLLIB, GLLIB, V3DLIB, "-Wl,--end-group",
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
    for l in mdef[:60]:
        print(f"  M {l}")
    other = [l for l in r.stderr.splitlines()
             if "undefined reference" not in l and "multiple definition" not in l
             and ("error" in l.lower() or "cannot" in l.lower())][:10]
    for l in other:
        print(f"  {l}")
    return 2


if __name__ == "__main__":
    sys.exit(main())
