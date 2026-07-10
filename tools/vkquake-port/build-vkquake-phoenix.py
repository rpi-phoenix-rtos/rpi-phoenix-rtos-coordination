#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Phoenix Systems. Author: Witold Bołt.
# Build recipe for the vkQuake Phoenix port (GPL-2.0-or-later); see COPYING.
"""Build vkQuake (Vulkan Quake) for Phoenix-RTOS Pi4 (aarch64-phoenix), on the ported
V3DV Vulkan ICD (libv3dv-phoenix.a + libv3d-phoenix.a back-end).

Mirrors build-quakespasm-phoenix.py (the GL port's stage-1 probe), but targets Vulkan:
the engine includes <vulkan/vulkan_core.h> + vid.h via quakedef.h, calls core Vulkan
commands DIRECTLY as link symbols (no VK_NO_PROTOTYPES), and loads KHR/EXT functions
via fp* pointers at runtime. The ICD does not export the public vk* symbols, so we link
a generated trampoline layer (vk_trampolines.c) that forwards each directly-called core
command through vkGetInstanceProcAddr (vk_icd_link.c aliases that to v3dv_*).

STAGE 1 (this script, "probe" mode): compile every PORTABLE vkQuake TU against the
aarch64-phoenix toolchain, the Vulkan headers, and a grown SDL2 shim. SDL platform TUs
(gl_vidsdl, in_sdl*, snd_sdl*, sys_sdl*, main_sdl, pl_linux, net_bsd) are EXCLUDED —
they are replaced by Phoenix platform shims (reused/adapted from quakespasm-port).
Print a pass/fail summary so the remaining compile gaps are explicit.

STAGE 2 (later): link against libv3dv-phoenix.a + libv3d-phoenix.a + the trampolines +
the Phoenix platform shims, capturing the undefined-symbol closure (libc + shim gaps).

The renderer (gl_*.c rewritten for Vulkan, the .spv shaders embedded via bintoc) is the
bulk of vkQuake's new code vs. quakespasm; it needs the Vulkan vid shim (pl_phoenix_vk_*)
which is the long-pole port work. This probe quantifies which TUs already compile and
which are blocked on the (not-yet-written) Vulkan vid shim / SDL_Vulkan surface.

Usage: python3 build-vkquake-phoenix.py [--link]
"""
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TC   = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
AR   = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc-ar"
NM   = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc-nm"
Q    = f"{ROOT}/external/vkquake/Quake"
SHADERS = f"{ROOT}/external/vkquake/Shaders"
PORT = f"{ROOT}/tools/vkquake-port"
QSPORT = f"{ROOT}/tools/quakespasm-port"           # reuse its SDL shim + platform shims
SDLSHIM = f"{QSPORT}/sdl-shim"
VKINC   = f"{ROOT}/external/mesa/include"           # <vulkan/vulkan_core.h>
OBJ  = "/tmp/vkqobj"

GPU_LIBS = f"{ROOT}/tools/.gpu-libs"  # stable home for the prebuilt engine archives (was /tmp)
V3DV_LIB = f"{GPU_LIBS}/libv3dv-phoenix.a"
V3D_LIB  = f"{GPU_LIBS}/libv3d-phoenix.a"
ELF      = "/tmp/vkquake-phoenix"

# SDL/platform TUs replaced wholesale by Phoenix shims — never compiled here.
# (matches the meson `srcs` SDL/platform set + the quakespasm-port EXCLUDE policy)
EXCLUDE = {
    "gl_vidsdl",                         # -> pl_phoenix_vk_vid.c (Vulkan vid shim, TODO)
    "in_sdl", "in_sdl2", "in_sdl3",      # -> pl_phoenix_in.c (/dev/kbd0)
    "snd_sdl", "snd_sdl3",               # -> pl_phoenix_snd.c
    # NOTE: sys_sdl.c is PORTABLE (the integer file-handle layer; no SDL dependency) and
    # is kept in ENGINE — only the platform-specific sys_sdl_{unix,win}.c are excluded
    # (replaced by pl_phoenix_sys.c).
    "sys_sdl_unix", "sys_sdl_win",       # -> pl_phoenix_sys.c
    "main_sdl",                          # -> pl_phoenix_main.c
    "pl_linux", "pl_win",                # platform glue
    "net_bsd", "net_win", "net_wins", "net_wipx",  # BSD/Win sockets -> loopback shim
    "cd_sdl",
    # optional codec backends (need external libs not in sysroot) — excluded from probe
    "snd_flac", "snd_mikmod", "snd_modplug", "snd_mp3", "snd_mp3tag",
    "snd_mpg123", "snd_opus", "snd_vorbis", "snd_xmp",
}

# Portable engine TUs to probe-compile. Derived from external/vkquake/meson.build `srcs`
# minus EXCLUDE. Renderer (gl_*/r_*) included; they need the Vulkan vid shim to LINK but
# should COMPILE against the headers (quantifies the header/shim gap).
ENGINE = [
    "bgmusic", "cd_null", "cfgfile", "chase", "cl_demo", "cl_input", "cl_main",
    "cl_parse", "cl_tent", "cmd", "common", "console", "crc", "cvar",
    "gl_draw", "gl_fog", "gl_heap", "gl_mesh", "gl_model", "gl_refrag", "gl_rlight",
    "gl_rmain", "gl_rmisc", "gl_screen", "gl_sky", "gl_texmgr", "gl_warp",
    "host", "host_cmd", "image", "keys", "mathlib", "mdfour", "mem", "menu", "miniz",
    "net_dgrm", "net_loop", "net_main", "net_udp", "palette", "pr_cmds", "pr_edict",
    "pr_exec", "pr_ext", "r_alias", "r_brush", "r_part", "r_part_fte", "r_sprite",
    "r_world", "sbar", "snd_codec", "snd_dma", "snd_mem", "snd_mix", "snd_umx",
    "snd_wave", "strlcat", "strlcpy", "sv_main", "sv_move", "sv_phys", "sv_user",
    "tasks", "view", "wad", "world", "hash_map", "quakedef", "lodepng",
    # Portable file-handle layer (sys_sdl.c): the integer Sys_FileOpenRead/Write/Read/
    # Write/Seek/Close + Sys_MemFileOpenRead + Sys_filelength. No SDL dependency, so it
    # builds verbatim and supplies those 8 Sys_* symbols; the Phoenix sys shim provides
    # only the non-portable deltas (Sys_fseek/ftell/Init/Error/etc.).
    "sys_sdl",
]

# Phoenix platform shims (replace the EXCLUDEd SDL/platform TUs). Compiled with the same
# CFLAGS and archived alongside the engine TUs so their bodies resolve the platform-shim
# undefined surface (Sys_*/IN_*/SNDDMA_*/SDL_* threading + net loopback table + libc gaps).
PLATFORM = [
    f"{PORT}/platform/pl_phoenix_sdlcompat.c",   # SDL2 threading/path bodies + copysign + pthread_mutex_timedlock
    f"{PORT}/platform/pl_phoenix_sys.c",         # Sys_* (non-portable) + PL_ErrorDialog/PL_GetClipboardData
    f"{PORT}/platform/pl_phoenix_in.c",          # IN_* (/dev/kbd0 + /dev/mouse0) + SDL_GetMouseState
    f"{PORT}/platform/pl_phoenix_snd.c",         # SNDDMA_* (/dev/audio0 feeder)
    f"{PORT}/platform/pl_phoenix_main.c",        # main() + host loop
    f"{PORT}/platform/pl_phoenix_stubs.c",       # net_drivers loopback table + pthread_getcpuclockid
    f"{PORT}/platform/pl_phoenix_vk_vid.c",      # Vulkan vid shim (VID_*/GL_*/vid+cvars) -> fb0 scanout
]

# Engine include flags. quakedef.h pulls <vulkan/vulkan_core.h> + vid.h, so VKINC and the
# SDL shim are both on the path. -DUSE_SDL2 keeps the SDL2 codepaths (we shim SDL2).
COMPAT = f"{PORT}/vkq_phoenix_compat.h"
CFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-Wno-error",
          "-DLINUX", "-D_GNU_SOURCE", "-DUSE_SDL2", "-include", COMPAT,
          f"-I{PORT}/sdl-shim", f"-I{Q}", f"-I{VKINC}", f"-I{SHADERS}"]


def compile_one(src, obj, extra=None):
    flags = CFLAGS + (extra or [])
    r = subprocess.run([TC] + flags + ["-o", obj, src], capture_output=True, text=True)
    if r.returncode == 0:
        return None
    errs = [l for l in r.stderr.splitlines() if "error:" in l or "fatal error" in l]
    return errs[0] if errs else (r.stderr.splitlines()[-1] if r.stderr else "?")


def main():
    do_link = "--link" in sys.argv
    os.makedirs(OBJ, exist_ok=True)
    units = [u for u in ENGINE if u not in EXCLUDE]
    objs, fail = [], []

    for u in units:
        src = f"{Q}/{u}.c"
        if not os.path.exists(src):
            fail.append((u, "MISSING SOURCE")); continue
        e = compile_one(src, f"{OBJ}/{u}.o")
        (fail.append((u, e)) if e else objs.append(f"{OBJ}/{u}.o"))

    # trampolines (generated) — public vk* symbols vkQuake calls directly.
    tramp_src = f"{PORT}/vk_trampolines.c"
    if os.path.exists(tramp_src):
        e = compile_one(tramp_src, f"{OBJ}/vk_trampolines.o")
        (fail.append(("vk_trampolines", e)) if e else objs.append(f"{OBJ}/vk_trampolines.o"))

    # Generated build-infra TUs (see gen-vkquake-shaders.py + Misc/vq_pak/Makefile):
    #   vkquake_shaders.c — the 41 embedded-SPIR-V arrays (real bytes if glslang on PATH,
    #                       else minimal valid placeholders; *_spv / *_spv_size symbols).
    #   embedded_pak.c    — the base pak (gfx/maps/default.cfg) compressed + bin2c'd by
    #                       mkpak+bintoc (vkquake_pak / _size / _decompressed_size).
    # Both are AUTO-GENERATED here when absent so --link is reproducible from a clean
    # checkout (embedded_pak.c is gitignored in external/vkquake; vkquake_shaders.c is a
    # generated artifact too). Re-run gen-vkquake-shaders.py by hand to refresh shaders
    # after putting glslang on PATH (it then emits REAL SPIR-V instead of placeholders).
    SHADERS_C = f"{PORT}/vkquake_shaders.c"
    PAK_C     = f"{Q}/embedded_pak.c"
    if not os.path.exists(SHADERS_C):
        print(f"=== generating {SHADERS_C} (gen-vkquake-shaders.py) ===")
        subprocess.run([sys.executable, f"{PORT}/gen-vkquake-shaders.py", SHADERS_C])
    if not os.path.exists(PAK_C):
        print(f"=== generating {PAK_C} (Misc/vq_pak make) ===")
        subprocess.run(["make", "-C", f"{ROOT}/external/vkquake/Misc/vq_pak"])

    GENERATED = [SHADERS_C, PAK_C]
    gen_total = 0
    for src in GENERATED:
        if not os.path.exists(src):
            fail.append((os.path.basename(src), "MISSING (auto-gen failed; run "
                         "gen-vkquake-shaders.py / `make -C external/vkquake/Misc/vq_pak`)"))
            continue
        gen_total += 1
        name = os.path.splitext(os.path.basename(src))[0]
        e = compile_one(src, f"{OBJ}/{name}.o")
        (fail.append((name, e)) if e else objs.append(f"{OBJ}/{name}.o"))

    # Phoenix platform shims (replace the EXCLUDEd SDL/platform TUs). Same CFLAGS.
    plat_total = 0
    for src in PLATFORM:
        if not os.path.exists(src):
            fail.append((os.path.basename(src), "MISSING SOURCE")); continue
        plat_total += 1
        name = os.path.splitext(os.path.basename(src))[0]
        e = compile_one(src, f"{OBJ}/{name}.o")
        (fail.append((name, e)) if e else objs.append(f"{OBJ}/{name}.o"))

    total = len(units) + (1 if os.path.exists(tramp_src) else 0) + plat_total + gen_total
    print(f"\n=== compile: {len(objs)}/{total} TUs OK (incl. {plat_total} platform shims) ===")
    if fail:
        print(f"--- {len(fail)} FAILED ---")
        for u, e in fail:
            print(f"  [{u}] {e}")

    if not do_link:
        return 0 if not fail else 1

    # STAGE 2: archive + link-drive against the V3DV ICD + back-end. Captures the
    # undefined-symbol closure (libc gaps + the platform-shim surface still to write).
    LIB = f"{GPU_LIBS}/libvkquake.a"  # stable home (was /tmp)
    subprocess.run(["rm", "-f", LIB])
    subprocess.run([AR, "rcs", LIB] + objs, check=True)
    print(f"=== archived {len(objs)} objs -> {LIB} ===")

    # whole-archive on libvkquake.a is REQUIRED for a meaningful closure: a bare archive
    # link only pulls members crt0's `main` reaches (~nothing), falsely reporting "1
    # undefined: main". Whole-archive forces every engine TU into the link so its real
    # references enter the undefined set.
    #
    # whole-archive on V3DV_LIB is ALSO required: the Mesa Vulkan *runtime* layer (vk_buffer.c,
    # vk_fence.c, vk_command_pool.c, vk_synchronization.c, vk_cmd_copy.c, ...) is bundled into
    # libv3dv-phoenix.a, and its vk_common_* fallback entrypoints are reachable ONLY through
    # WEAK relocs in the generated dispatch tables (vk_common_device_entrypoints). Weak refs do
    # NOT pull archive members, so a non-whole-archive link silently drops those objects: the
    # dispatch slots resolve to 0 and any command V3DV implements via the common fallback (e.g.
    # vkGetBufferMemoryRequirements, vkQueueSubmit, vkCreateFence, vkWaitForFences,
    # vkCreateCommandPool, vkCmdPipelineBarrier) becomes a NULL function pointer -> pc=0 abort
    # at runtime, with a 0-undefined link masking it. Upstream Mesa whole-archives the runtime
    # into the ICD for exactly this reason. The V3D back-end (V3D_LIB) stays in the normal group.
    link = [TC, "-o", ELF, "-Wl,--allow-multiple-definition",
            "-Wl,--whole-archive", LIB, V3DV_LIB, "-Wl,--no-whole-archive",
            "-Wl,--start-group", V3DV_LIB, V3D_LIB, "-Wl,--end-group", "-lm"]
    r = subprocess.run(link, capture_output=True, text=True)
    if r.returncode == 0:
        print(f"=== LINK OK -> {ELF} ===")
        return 0
    undef = sorted(set(l.split("undefined reference to ")[1].strip().strip("`'")
                       for l in r.stderr.splitlines() if "undefined reference to" in l))
    print(f"=== LINK: {len(undef)} undefined symbols ===")
    for s in undef:
        print(f"  U {s}")
    return 2


if __name__ == "__main__":
    sys.exit(main())
