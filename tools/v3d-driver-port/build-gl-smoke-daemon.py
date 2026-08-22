#!/usr/bin/env python3
"""build-gl-smoke-daemon.py - build the gl_frontend_smoke GL render-clear test in TWO
flavors and diff the link:

  * BASELINE (in-process):  gl_frontend_smoke.o + libGL-phoenix.a + libv3d-phoenix.a
    -> the known-good reference (phoenix_v3d_ioctl + scanout/power come from the winsys
    objects folded into libv3d-phoenix.a).

  * DAEMON-CLIENT:          gl_frontend_smoke.o + libGL-phoenix.a
    + libv3d-phoenix.a MINUS {v3d_phoenix_winsys.o, v3d_phoenix_power.o}
    + libv3d-client.a
    -> the SAME GL stack, but phoenix_v3d_ioctl (and the scanout/power exports the
    winsys/power objects used to provide) now come from the daemon client library.

M1 step 2c-test: prove the winsys-as-client seam is a near-clean link swap. Additive:
v3d_phoenix_winsys.c / v3d_phoenix_power.c / the GL test .c / Mesa all stay unchanged;
only libv3d-client.c grows (to satisfy the exports the surfaceless GL stack references
beyond phoenix_v3d_ioctl).

Reuses build-v3d-phoenix.py's prelude (TC/HOSTBUILD/GPU_LIBS/PORT/ABI_FLAGS/transform).

Usage: python3 build-gl-smoke-daemon.py [--out-dir DIR]
Copyright 2026 Phoenix Systems  SPDX-License-Identifier: BSD-3-Clause
"""
import os, json, subprocess, sys, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PORT_DIR = os.path.join(ROOT, "tools/v3d-driver-port")
DEV_DIR = os.path.join(ROOT, "sources/phoenix-rtos-devices/gpu/rpi4-v3d")
# Coordinator's primary choice: gl_frontend_smoke.c - the simplest CL-path exercise
# (surfaceless: wrap an FBO-backed RT, glClear to green, glReadPixels). It never calls
# v3d_phoenix_powerOn directly (the winsys powers on lazily in-process; the daemon owns
# power in the client variant), so it is the cleanest daemon-seam target. Overridable:
#   GL_SMOKE_SRC=gl_det_harness.c python3 build-gl-smoke-daemon.py
SRC = os.path.join(PORT_DIR, os.environ.get("GL_SMOKE_SRC", "gl_frontend_smoke.c"))

OUT_DIR = "/tmp/gl-smoke-build"
if "--out-dir" in sys.argv:
    OUT_DIR = sys.argv[sys.argv.index("--out-dir") + 1]
os.makedirs(OUT_DIR, exist_ok=True)

# Reuse build-v3d-phoenix.py's prelude for TC/HOSTBUILD/GPU_LIBS/PORT/ABI_FLAGS/transform.
_pre = open(os.path.join(PORT_DIR, "build-v3d-phoenix.py")).read().split("def main")[0]
g = {"__file__": os.path.join(PORT_DIR, "build-v3d-phoenix.py")}
exec(_pre, g)
TC, HOSTBUILD, GPU_LIBS, ABI_FLAGS, transform = (
    g["TC"], g["HOSTBUILD"], g["GPU_LIBS"], g["ABI_FLAGS"], g["transform"])
AR = g["AR"]
TCXX = TC.replace("gcc", "g++")

GL = f"{GPU_LIBS}/libGL-phoenix.a"
V3D = f"{GPU_LIBS}/libv3d-phoenix.a"
V3D_DAEMON = f"{OUT_DIR}/libv3d-phoenix-daemon.a"   # V3D minus winsys+power
CLIENT_A = f"{OUT_DIR}/libv3d-client.a"

OBJ = f"{OUT_DIR}/gl_frontend_smoke.o"
BASELINE = f"{OUT_DIR}/gl-smoke"
DAEMON = f"{OUT_DIR}/gl-smoke-daemon"


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def undefined_syms(stderr):
    u = set()
    for line in stderr.splitlines():
        if "undefined reference to" in line:
            u.add(line.split("undefined reference to")[1].strip().strip("`'\""))
    return sorted(u)


# 1. compile the GL test with the exact Mesa GL include set (context.c template, cwd=HOSTBUILD)
db = json.load(open(f"{HOSTBUILD}/compile_commands.json"))
tmpl = next(e for e in db if e["file"].endswith("src/mesa/main/context.c"))
print(">> COMPILE", os.path.basename(SRC))
r = run(transform(tmpl, SRC, OBJ), cwd=HOSTBUILD)
if r.returncode != 0:
    errs = [l for l in r.stderr.splitlines() if "error:" in l]
    print("COMPILE FAIL:\n" + ("\n".join(errs[:20]) if errs else r.stderr[-2000:]))
    sys.exit(1)

# 1b. link-glue stubs (trace_context_create_threaded + pthread_getcpuclockid) that
#     gl_frontend_smoke.c does not carry inline. Plain compile (no mesa flags needed).
STUBS_C = os.path.join(PORT_DIR, "gl_smoke_stubs.c")
STUBS_O = f"{OUT_DIR}/gl_smoke_stubs.o"
r = run([TC, "-c", STUBS_C, "-o", STUBS_O] + ABI_FLAGS + ["-O2", "-std=gnu11", "-lpthread"])
if r.returncode != 0:
    print("STUBS COMPILE FAIL:\n" + r.stderr[-1500:]); sys.exit(1)

# 2. build libv3d-client.a from the devices-repo source, with the SAME on-device ABI flags
#    as the Mesa objects (cortex-a72 / strict-align) so the HW binary is ABI-consistent.
CLIENT_C = os.path.join(DEV_DIR, "libv3d-client.c")
CLIENT_O = f"{OUT_DIR}/libv3d-client.o"
print(">> COMPILE libv3d-client.c (ABI-matched)")
r = run([TC, "-c", CLIENT_C, "-o", CLIENT_O, f"-I{DEV_DIR}", f"-I{DEV_DIR}/uapi"]
        + ABI_FLAGS + ["-O2", "-std=gnu11"])
if r.returncode != 0:
    print("CLIENT COMPILE FAIL:\n" + r.stderr[-2000:])
    sys.exit(1)
if os.path.exists(CLIENT_A):
    os.remove(CLIENT_A)
run([AR, "rcs", CLIENT_A, CLIENT_O])

# 3. BASELINE link (in-process): the known-good reference.
print(">> LINK baseline (in-process winsys)")
base_link = [TCXX, "-static", OBJ, STUBS_O, "-Wl,--start-group", GL, V3D, "-Wl,--end-group",
             "-lphoenix", "-lm", "-lpthread", "-o", BASELINE]
r = run(base_link)
if r.returncode != 0:
    print("BASELINE LINK FAIL:\n" + "\n".join(undefined_syms(r.stderr)[:40]) or r.stderr[-2000:])
    sys.exit(2)
print(f"   baseline OK -> {BASELINE} ({os.path.getsize(BASELINE)//1024} KiB)")

# 4. DAEMON archive = V3D minus winsys+power.
shutil.copy(V3D, V3D_DAEMON)
run([AR, "d", V3D_DAEMON, "v3d_phoenix_winsys.o", "v3d_phoenix_power.o"])
print(">> built daemon archive (V3D minus winsys+power)")

# 5. DAEMON-CLIENT link.
print(">> LINK daemon-client (libv3d-client provides phoenix_v3d_ioctl)")
dae_link = [TCXX, "-static", OBJ, STUBS_O,
            "-Wl,--start-group", GL, V3D_DAEMON, CLIENT_A, "-Wl,--end-group",
            "-lphoenix", "-lm", "-lpthread", "-o", DAEMON]
r = run(dae_link)
if r.returncode != 0:
    u = undefined_syms(r.stderr)
    print(f"DAEMON LINK: {len(u)} undefined symbols:")
    for s in u:
        print(f"    {s}")
    if not u:
        print(r.stderr[-2500:])
    sys.exit(3)
print(f"   daemon-client OK -> {DAEMON} ({os.path.getsize(DAEMON)//1024} KiB)")

# 6. the v3d-server binary (same source tree as libv3d-client), ABI-matched, so the whole
#    HW test stages from one dir. Links standalone (does not link the GL test).
SRV = f"{OUT_DIR}/rpi4-v3d"
GPU_O = f"{OUT_DIR}/v3d_gpu.o"
SRV_O = f"{OUT_DIR}/rpi4-v3d.o"
print(">> BUILD v3d-server (rpi4-v3d)")
for src, obj in ((os.path.join(DEV_DIR, "v3d_gpu.c"), GPU_O),
                 (os.path.join(DEV_DIR, "rpi4-v3d.c"), SRV_O)):
    r = run([TC, "-c", src, "-o", obj, f"-I{DEV_DIR}", f"-I{DEV_DIR}/uapi"]
            + ABI_FLAGS + ["-O2", "-std=gnu11"])
    if r.returncode != 0:
        print("SERVER COMPILE FAIL:\n" + r.stderr[-2000:]); sys.exit(4)
r = run([TC, "-static", SRV_O, GPU_O, "-o", SRV])
if r.returncode != 0:
    print("SERVER LINK FAIL:\n" + r.stderr[-2000:]); sys.exit(4)
print(f"   server OK -> {SRV} ({os.path.getsize(SRV)//1024} KiB)")

print("\nBUILD OK: server + baseline + daemon-client all linked 0-undefined.")
print(f"  server (rpi4-v3d):     {SRV}")
print(f"  baseline (in-process): {BASELINE}")
print(f"  daemon-client:         {DAEMON}")
