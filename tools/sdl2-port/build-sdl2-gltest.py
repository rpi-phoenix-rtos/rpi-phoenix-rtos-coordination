#!/usr/bin/env python3
# SPDX-License-Identifier: Zlib
# Build + cross-link the Phoenix SDL2 fullscreen-GL smoke test (sdl2-gltest).
#
# Proves the SDL2 port's video+GL seam links end-to-end:
#   sdl2-gltest.o  (zlib, SDL client)
# + sdl_phoenix_glctx.o  (the GL-context glue, zlib, compiled with Mesa flags;
#                         kept OUTSIDE libSDL2.a because it needs Mesa-internal
#                         headers/flags, see the port's glue/README.md)
# + libSDL2.a    (zlib, phoenix video+input driver)
# + libGL-phoenix.a + libv3d-phoenix.a  (the ported Mesa V3D GL stack)
# -> aarch64-phoenix ELF.
#
# A successful link is the phase-1 step-4 milestone (no Pi run this pass).
#
# Usage: python3 tools/sdl2-port/build-sdl2-gltest.py
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TC   = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
MESA = f"{ROOT}/external/mesa"
COMPAT = f"{ROOT}/tools/v3d-driver-port/phoenix_mesa_compat.h"
GPU_LIBS = f"{ROOT}/tools/.gpu-libs"
GLLIB  = f"{GPU_LIBS}/libGL-phoenix.a"
V3DLIB = f"{GPU_LIBS}/libv3d-phoenix.a"

# libSDL2.a + installed SDL headers from the port build (build-sdl2-port.sh).
BUILD = f"{ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b"
SDLLIB = f"{BUILD}/lib/libSDL2.a"
SDLINC = f"{BUILD}/include/SDL2"

PLAT = f"{ROOT}/tools/sdl2-port"
GLUEDIR = f"{ROOT}/sources/phoenix-rtos-ports/sdl2/glue"
GLUE = f"{GLUEDIR}/sdl_phoenix_glctx.c"
GLSTUBS = f"{GLUEDIR}/sdl_phoenix_glstubs.c"
OBJ  = "/tmp/sdl2test-obj"
ELF  = "/tmp/sdl2-gltest-phoenix"

# SDL client-side flags (mirrors the quakespasm QFLAGS shape).
CFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-Wno-error",
          f"-I{SDLINC}"]

# Mesa-side flags for the GL-context glue (verbatim from build-quakespasm-phoenix.py
# MFLAGS: the endianness/timespec -D's + Mesa include order + compat header).
MFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-Wno-error",
          "-Wno-undef", "-DUTIL_ARCH_LITTLE_ENDIAN=1", "-DUTIL_ARCH_BIG_ENDIAN=0",
          "-DHAVE_STRUCT_TIMESPEC", "-include", COMPAT,
          f"-I{MESA}/src", f"-I{MESA}/include", f"-I{MESA}/src/mesa",
          f"-I{MESA}/src/mapi", f"-I{MESA}/src/compiler",
          f"-I{MESA}/src/gallium/include", f"-I{MESA}/src/gallium/auxiliary",
          f"-I{MESA}/src/util", "-I/tmp/mesa-v3d-build/src"]


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r


def compile_one(src, flags, obj):
    cmd = [TC] + flags + ["-o", obj, src]
    r = run(cmd)
    if r.returncode != 0:
        # The exact command + the COMPLETE compiler output. This used to print
        # only the `error:` lines, capped at 20, which can drop the diagnostic
        # that actually explains the failure (notes, "required from here", the
        # first fatal include error). A build that fails has to show why.
        print(f"=== COMPILE FAILED: {src} ===")
        print("$ " + " ".join(cmd))
        print(r.stderr.strip() or "(no stderr)")
        if r.stdout.strip():
            print("--- compiler stdout ---")
            print(r.stdout.strip())
        sys.stdout.flush()
        return None
    return obj


def main():
    for p in (SDLLIB, GLLIB, V3DLIB):
        if not os.path.exists(p):
            print(f"missing prerequisite archive: {p}")
            return 1
    os.makedirs(OBJ, exist_ok=True)

    test_o = compile_one(f"{PLAT}/sdl2-gltest.c", CFLAGS, f"{OBJ}/sdl2-gltest.o")
    if not test_o:
        return 1
    glue_o = compile_one(GLUE, MFLAGS, f"{OBJ}/sdl_phoenix_glctx.o")
    if not glue_o:
        return 1
    # GL stack libc gap-fillers (pthread_getcpuclockid, referenced by Mesa
    # u_thread). Plain client-side flags — no Mesa headers needed.
    stubs_o = compile_one(GLSTUBS, CFLAGS, f"{OBJ}/sdl_phoenix_glstubs.o")
    if not stubs_o:
        return 1
    print("=== compiled test + glue + glstubs OK ===")

    # Circular refs (SDL -> glue -> Mesa; libGL <-> libv3d): wrap in a group.
    # 32 MB stack matches the canonical _user rpi4 GL binary.mk link.
    link = [TC, test_o, glue_o, stubs_o,
            "-Wl,--start-group", SDLLIB, GLLIB, V3DLIB, "-Wl,--end-group",
            "-lstdc++", "-lm", "-Wl,-z,stack-size=33554432", "-o", ELF]
    r = run(link)
    if r.returncode == 0:
        print(f"=== LINK OK -> {ELF} ===")
        subprocess.run([f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-size", ELF])
        return 0

    undef = sorted(set(l.split("undefined reference to ")[1].strip().strip("`'")
                       for l in r.stderr.splitlines() if "undefined reference to" in l))
    print(f"=== LINK FAILED: {len(undef)} undefined symbols ===")
    for s in undef:
        print(f"  U {s}")
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
