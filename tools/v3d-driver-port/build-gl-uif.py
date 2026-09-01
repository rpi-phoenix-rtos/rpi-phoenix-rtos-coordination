#!/usr/bin/env python3
"""build-gl-uif.py — cross-compile + link the gl_uif_probe.c GL harness for aarch64-phoenix.

Reconstructs the (removed) gl-det-build.sh recipe generically: reuses build-v3d-phoenix.py's
prelude (TC/MESA/HOSTBUILD/PORT/GPU_LIBS + transform()) to compile the harness with the exact
Mesa GL include set (from HOSTBUILD/compile_commands.json, cwd=HOSTBUILD for the relative
generated-header -I paths), force-including phoenix_mesa_compat.h, then LINKS with g++ (the
C++ runtime — libGL has the GLSL compiler) against the two prebuilt folded archives
tools/.gpu-libs/{libGL-phoenix.a,libv3d-phoenix.a} + libphoenix, static aarch64.

The harness carries its own trace_context_create_threaded + pthread_getcpuclockid stubs (see
gl_uif_probe.c), same as gl_det_harness.c. Output: /tmp/gl-uif (deploy to the netboot export
/bin/gl-uif and run at psh). Requires HOSTBUILD (/tmp/mesa-v3d-build) + the gpu-libs present
(build-v3d-phoenix.py / build-gl-phoenix.py first if absent).

Copyright 2026 Phoenix Systems  SPDX-License-Identifier: BSD-3-Clause
"""
import os, json, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PORT_DIR = os.path.join(ROOT, "tools/v3d-driver-port")
SRC = os.path.join(PORT_DIR, "gl_uif_probe.c")
OBJ = "/tmp/gl_uif_probe.o"
OUT = os.environ.get("GL_UIF_OUT", "/tmp/gl-uif")

# Reuse build-v3d-phoenix.py's prelude (everything before "def main") for TC/MESA/HOSTBUILD/
# PORT/GPU_LIBS/transform. Give it a real __file__ so its ROOT derivation works.
_pre = open(os.path.join(PORT_DIR, "build-v3d-phoenix.py")).read().split("def main")[0]
g = {"__file__": os.path.join(PORT_DIR, "build-v3d-phoenix.py")}
exec(_pre, g)
TC, HOSTBUILD, GPU_LIBS, transform = g["TC"], g["HOSTBUILD"], g["GPU_LIBS"], g["transform"]
TCXX = TC.replace("gcc", "g++")

db = json.load(open(f"{HOSTBUILD}/compile_commands.json"))
tmpl = next(e for e in db if e["file"].endswith("src/mesa/main/context.c"))
cmd = transform(tmpl, SRC, OBJ)
print(">> COMPILE", os.path.basename(SRC))
r = subprocess.run(cmd, cwd=HOSTBUILD, capture_output=True, text=True)
if r.returncode != 0:
    # Exact command + COMPLETE compiler output. The old `error:`-only (capped 20) /
    # last-2000-chars filter could hide the diagnostic that explains the failure.
    print("COMPILE FAIL:\n$ " + " ".join(cmd))
    print(r.stderr.strip() or "(no stderr)")
    if r.stdout.strip():
        print("--- compiler stdout ---\n" + r.stdout.strip())
    sys.stdout.flush()
    sys.exit(1)

GL = f"{GPU_LIBS}/libGL-phoenix.a"
V3D = f"{GPU_LIBS}/libv3d-phoenix.a"
link = [TCXX, "-static", OBJ, "-Wl,--start-group", GL, V3D, "-Wl,--end-group",
        "-lphoenix", "-lm", "-lpthread", "-o", OUT]
print(">> LINK (g++)")
r = subprocess.run(link, capture_output=True, text=True)
if r.returncode != 0:
    # Keep the undefined-reference digest, then print the exact command + the
    # COMPLETE linker output. The old capped/tailed filter could reduce a real
    # failure to a bare "collect2: error: ld returned 1 exit status" with no
    # indication of the cause -- reported from a Docker build on macOS.
    ud = [l for l in r.stderr.splitlines() if "undefined reference" in l]
    print(f"LINK FAIL: {len(ud)} undefined-reference lines")
    for l in ud:
        print("  " + l.strip())
    print("--- link command ---\n$ " + " ".join(link))
    print("--- complete linker output ---")
    print(r.stderr.strip() or "(no stderr)")
    if r.stdout.strip():
        print("--- linker stdout ---\n" + r.stdout.strip())
    sys.stdout.flush()
    sys.exit(2)
print(f">> OK -> {OUT} ({os.path.getsize(OUT)//1024} KiB). Deploy: sudo cp {OUT} /srv/phoenix-rpi4-nfs/bin/gl-uif")
