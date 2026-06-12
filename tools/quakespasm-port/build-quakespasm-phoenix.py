#!/usr/bin/env python3
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
        "sv_user", "world", "zone", "snd_dma", "snd_mix", "snd_mem", "bgmusic"]

CFLAGS = ["-c", "-O2", "-ffreestanding", "-fno-strict-aliasing", "-Wno-error",
          f"-I{SHIM}", f"-I{Q}", f"-I{GLINC}"]

def main():
    os.makedirs(OBJ, exist_ok=True)
    units = [u for u in (GLOBJS + CORE) if u not in EXCLUDE]
    ok, fail = [], []
    for u in units:
        src = f"{Q}/{u}.c"
        if not os.path.exists(src):
            fail.append((u, "MISSING SOURCE"))
            continue
        r = subprocess.run([TC] + CFLAGS + ["-o", f"{OBJ}/{u}.o", src],
                           capture_output=True, text=True)
        if r.returncode == 0:
            ok.append(u)
        else:
            # first error line is the most informative
            errs = [l for l in r.stderr.splitlines() if "error:" in l]
            fail.append((u, errs[0] if errs else r.stderr.splitlines()[0] if r.stderr else "?"))
    print(f"\n=== Quakespasm compile probe: {len(ok)}/{len(units)} TUs OK ===")
    if fail:
        print(f"--- {len(fail)} FAILED ---")
        for u, e in fail:
            print(f"  [{u}] {e}")
    else:
        print("ALL portable TUs compiled. Next: write Phoenix platform shims + link.")
    return 1 if fail else 0

if __name__ == "__main__":
    sys.exit(main())
