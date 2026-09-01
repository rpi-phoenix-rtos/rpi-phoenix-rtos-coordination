#!/usr/bin/env python3
# SPDX-License-Identifier: Zlib
# Build + cross-link the Phoenix SDL2 audio smoke test (sdl2-audiotest).
#
# Proves the SDL2 port's audio seam links end-to-end with NO GL stack:
#   sdl2-audiotest.o          (zlib, SDL audio client + 440 Hz sine callback)
# + sdl2-audio-videostubs.o   (zlib, link-only no-op stubs for the video-path
#                              symbols SDL's core unconditionally pulls in — the
#                              GPL GL-context glue + libGL/libv3d are deliberately
#                              NOT linked; see the stub file's header)
# + libSDL2.a                 (zlib, phoenix audio driver over /dev/audio0)
# + -lm                       (sinf/sin)
# -> aarch64-phoenix ELF.
#
# A successful link is the phase-1 step-5 milestone (no Pi run this pass; audible
# sign-off on the 3.5 mm jack is a later manual check).
#
# Usage: python3 tools/sdl2-port/build-sdl2-audiotest.py
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TC   = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
SIZE = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-size"

# libSDL2.a + installed SDL headers from the port build (build-sdl2-port.sh).
BUILD  = f"{ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b"
SDLLIB = f"{BUILD}/lib/libSDL2.a"
SDLINC = f"{BUILD}/include/SDL2"

PLAT  = f"{ROOT}/tools/sdl2-port"
TEST  = f"{PLAT}/sdl2-audiotest.c"
STUBS = f"{PLAT}/sdl2-audio-videostubs.c"
OBJ   = "/tmp/sdl2audio-obj"
ELF   = "/tmp/sdl2-audiotest-phoenix"

# SDL client-side flags (mirrors the gltest / quakespasm QFLAGS shape).
CFLAGS = ["-c", "-O2", "-g", "-ffreestanding", "-fno-strict-aliasing", "-Wno-error",
          f"-I{SDLINC}"]


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def compile_one(src, obj):
    cmd = [TC] + CFLAGS + ["-o", obj, src]
    r = run(cmd)
    if r.returncode != 0:
        # The exact command + the COMPLETE compiler output. This used to print
        # only the `error:` lines, capped at 20, which can drop the diagnostic
        # that actually explains the failure (notes, the first fatal include
        # error). A build that fails has to show why.
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
    if not os.path.exists(SDLLIB):
        print(f"missing prerequisite archive: {SDLLIB}\n(run scripts/build-sdl2-port.sh first)")
        return 1
    os.makedirs(OBJ, exist_ok=True)

    test_o = compile_one(TEST, f"{OBJ}/sdl2-audiotest.o")
    if not test_o:
        return 1
    stubs_o = compile_one(STUBS, f"{OBJ}/sdl2-audio-videostubs.o")
    if not stubs_o:
        return 1
    print("=== compiled test + videostubs OK ===")

    # No GL libraries and no GPL glue: audio path only. --start-group covers the
    # SDL <-> stub back-references. 32 MB stack matches the rpi4 _user binary.mk.
    link = [TC, test_o, stubs_o,
            "-Wl,--start-group", SDLLIB, "-Wl,--end-group",
            "-lm", "-Wl,-z,stack-size=33554432", "-o", ELF]
    r = run(link)
    if r.returncode == 0:
        print(f"=== LINK OK -> {ELF} ===")
        subprocess.run([SIZE, ELF])
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
