#!/usr/bin/env python3
"""Build Mesa's v3d gallium driver as a Phoenix static lib + link-drive a harness.

Path-C Phase 2. Cross-compiles for aarch64a72-phoenix by reusing the HOST Mesa
build's per-file compile flags (HOSTBUILD/compile_commands.json), transformed:
  - swap the compiler -> the Phoenix toolchain gcc
  - drop x86 -m* flags, the host /usr/include/libdrm + valgrind includes
  - force-include phoenix_mesa_compat.h (C99 math / posix_memalign / qsort_r / ...)
  - prepend the shim-include dir (xf86drm.h/sys/ioccom.h/dlfcn.h/libsync.h) + port dir

Compiles are run with cwd=HOSTBUILD so the generated-header `-Isrc/...` relative
includes resolve. Driver-file set = every .c the host built under
gallium/drivers/v3d (exact -DV3D_VERSION variants included). The winsys
(v3d_phoenix_winsys.c) + the screen-create harness are compiled with flags
synthesized from a template driver entry.

LINK-DRIVE: link the harness against the prebuilt core lib + an aux archive built
from /tmp/v3dphx-aux.txt (one mesa-relative .c path per line). The linker pulls
only the members it needs; undefined refs are written to /tmp/v3dphx-undef.txt.
Add the aux files that define those symbols to aux.txt and re-run. Converges in a
few passes (the real closure, not a speculative bulk compile).

Usage:  python3 build-v3d-phoenix.py [--compile-only]
"""
import json, os, shlex, subprocess, sys

ROOT      = "/home/houp/phoenix-rpi"
MESA      = f"{ROOT}/external/mesa"
HOSTBUILD = "/tmp/mesa-v3d-build"
TC        = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc"
AR        = f"{ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc-ar"
PORT      = f"{ROOT}/tools/v3d-driver-port"
SHIM      = f"{PORT}/shim-include"
COMPAT    = f"{PORT}/phoenix_mesa_compat.h"
OBJDIR    = "/tmp/v3dphx-obj"          # shared with the core-lib build
DRVOBJ    = "/tmp/v3dphx-drvobj"
AUXOBJ    = "/tmp/v3dphx-auxobj"
COREA     = "/tmp/libv3d-phoenix-core.a"
AUXLIST   = "/tmp/v3dphx-aux.txt"      # mesa-relative .c paths, one per line
UNDEF     = "/tmp/v3dphx-undef.txt"
HARNESS_BIN = "/tmp/v3dphx-harness"

db = json.load(open(f"{HOSTBUILD}/compile_commands.json"))
by_file = {e["file"]: e for e in db}


def abssrc(f):
    """compile_commands 'file' is relative to HOSTBUILD -> absolute, normalized."""
    return os.path.normpath(f if os.path.isabs(f) else os.path.join(HOSTBUILD, f))


by_abs = {abssrc(e["file"]): e for e in db}     # robust source lookup (any path form)


def transform(entry, src, out):
    """Host compile command -> Phoenix cross compile command for `src`->`out`."""
    toks = shlex.split(entry.get("command") or " ".join(entry["arguments"]))[1:]
    keep, i = [], 0
    while i < len(toks):
        t = toks[i]
        if t == "-c":                                   i += 1; continue
        if t == "-o":                                   i += 2; continue
        if t in ("-MD", "-MMD", "-MP"):                 i += 1; continue
        if t in ("-MF", "-MQ", "-MT"):                  i += 2; continue
        if t.startswith("-m"):                          i += 1; continue
        if t == "-pthread":                             i += 1; continue
        if t.startswith("-DHAVE_VALGRIND"):             i += 1; continue
        if t.startswith("-Werror"):                     i += 1; continue   # warnings ok for the port
        if t.startswith("-I/usr/include/libdrm"):       i += 1; continue
        if t.startswith("-I/usr/include/valgrind"):     i += 1; continue
        if t == entry["file"]:                          i += 1; continue
        keep.append(t); i += 1
    # GCC-14 promotes implicit-function-declaration/implicit-int to hard errors by
    # default; downgrade to warnings for the port (Phoenix libc misses some decls).
    return [TC, "-c", src, "-o", out, f"-I{SHIM}", f"-I{PORT}",
            "-Wno-error=implicit-function-declaration", "-Wno-error=implicit-int",
            "-include", COMPAT] + keep


def compile_one(entry, src, out):
    cmd = transform(entry, src, out)
    r = subprocess.run(cmd, cwd=HOSTBUILD, capture_output=True, text=True)
    return r.returncode, r.stderr, cmd


def driver_entries():
    return [e for e in db if "/gallium/drivers/v3d/" in e["file"] and e["file"].endswith(".c")]


def template_entry():
    for e in db:
        if e["file"].endswith("gallium/drivers/v3d/v3d_screen.c"):
            return e
    raise SystemExit("no template entry (v3d_screen.c) in compile_commands")


def build_objs(entries, srcs_outs, objdir, label):
    """entries: list of (entry, src, out). Returns (ok_objs, fails)."""
    os.makedirs(objdir, exist_ok=True)
    ok, fails = [], []
    for entry, src, out in srcs_outs:
        rc, err, cmd = compile_one(entry, src, out)
        if rc == 0:
            ok.append(out)
        else:
            fails.append((src, err.strip().splitlines()[-3:]))
    print(f"[{label}] OK={len(ok)} FAIL={len(fails)}")
    for src, errtail in fails:
        print(f"  FAIL {os.path.relpath(src, MESA)}")
        for l in errtail:
            print(f"       {l}")
    return ok, fails


def main():
    compile_only = "--compile-only" in sys.argv
    tmpl = template_entry()

    # 1. driver set (exact host-built variants)
    drv = driver_entries()
    so = []
    for e in drv:
        base = os.path.basename(e["file"])
        # v3dx files compiled per-version; key the obj by file+any -DV3D_VERSION
        ver = ""
        cmd = e.get("command") or " ".join(e["arguments"])
        if "V3D_VERSION=42" in cmd: ver = "_v42"
        elif "V3D_VERSION=33" in cmd: ver = "_v33"
        elif "V3D_VERSION=71" in cmd: ver = "_v71"
        out = f"{DRVOBJ}/{base}{ver}.o"
        so.append((e, e["file"], out))
    drv_ok, _ = build_objs(drv, so, DRVOBJ, "driver")

    # 2. winsys + libdrm-shim + harness (synth flags from the template entry)
    winsys_c  = f"{PORT}/v3d_phoenix_winsys.c"
    libdrm_c  = f"{PORT}/v3d_libdrm_shim.c"
    harness_c = f"{PORT}/harness_screen_create.c"
    winsys_o  = f"{DRVOBJ}/v3d_phoenix_winsys.o"
    libdrm_o  = f"{DRVOBJ}/v3d_libdrm_shim.o"
    harness_o = f"{DRVOBJ}/harness_screen_create.o"
    build_objs([tmpl] * 3,
        [(tmpl, winsys_c, winsys_o), (tmpl, libdrm_c, libdrm_o),
         (tmpl, harness_c, harness_o)], DRVOBJ, "winsys+shim+harness")

    # 2b. peripheral stubs (generic signatures -> compile warnings-off)
    stubs_c = f"{PORT}/v3d_phoenix_stubs.c"
    stubs_o = f"{DRVOBJ}/v3d_phoenix_stubs.o"
    rc = subprocess.run([TC, "-c", stubs_c, "-o", stubs_o, "-std=gnu11", "-w",
                         "-include", COMPAT], capture_output=True, text=True)
    print(f"[stubs] rc={rc.returncode}" + ("" if rc.returncode == 0 else "\n" + rc.stderr))

    # 3. aux objs from the link-drive list (seed from the committed manifest if the
    #    /tmp working copy is absent, so a fresh checkout builds without re-resolving)
    committed = f"{PORT}/v3d-aux-sources.txt"
    if not os.path.exists(AUXLIST) and os.path.exists(committed):
        rels = [l.strip() for l in open(committed)
                if l.strip() and not l.startswith("#")]
        open(AUXLIST, "w").write("\n".join(rels) + "\n")
        print(f"[aux] seeded {AUXLIST} from committed manifest ({len(rels)} files)")
    aux_objs = []
    if os.path.exists(AUXLIST):
        rels = [l.strip() for l in open(AUXLIST) if l.strip() and not l.startswith("#")]
        so = []
        for rel in rels:
            src = os.path.normpath(os.path.join(MESA, rel))
            e = by_abs.get(src)
            if e is None:
                print(f"  [aux] NO compile_commands entry for {rel} -- skipping")
                continue
            out = f"{AUXOBJ}/{os.path.basename(rel)}.o"
            so.append((e, src, out))
        aux_objs, _ = build_objs([x[0] for x in so], so, AUXOBJ, "aux")

    if compile_only:
        print("compile-only: skipping archive + link")
        return

    # 4. aux archive (+ keep core as-is)
    auxa = "/tmp/libv3d-phoenix-aux.a"
    if aux_objs:
        if os.path.exists(auxa): os.remove(auxa)
        subprocess.run([AR, "rcs", auxa] + aux_objs, check=True)

    # 5. link-drive: harness + winsys + shim + stubs + driver objs, group(core, aux)
    link = [TC, "-o", HARNESS_BIN, harness_o, winsys_o, libdrm_o, stubs_o] + drv_ok + \
           ["-Wl,--start-group", COREA] + ([auxa] if aux_objs else []) + \
           ["-Wl,--end-group", "-lm"]
    r = subprocess.run(link, capture_output=True, text=True)
    print(f"[link] rc={r.returncode}")
    # collect undefined references
    undef = set()
    for line in r.stderr.splitlines():
        if "undefined reference to" in line:
            sym = line.split("undefined reference to")[1].strip().strip("`'\"")
            undef.add(sym)
    open(UNDEF, "w").write("\n".join(sorted(undef)) + "\n")
    if r.returncode == 0:
        print(f"[link] PASS -> {HARNESS_BIN}")
    else:
        print(f"[link] {len(undef)} undefined symbols -> {UNDEF}")
        for s in sorted(undef)[:40]:
            print(f"    {s}")
        if len(undef) > 40:
            print(f"    ... +{len(undef)-40} more")


if __name__ == "__main__":
    main()
