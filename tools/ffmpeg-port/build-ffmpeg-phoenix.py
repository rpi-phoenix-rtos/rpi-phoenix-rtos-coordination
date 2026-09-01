#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Phoenix Systems. Author: Witold Bołt.
# Build recipe for the ffmpeg decode-core Phoenix port (LGPL-2.1); see COPYING.
"""Cross-build the ffmpeg decode core into static libs + a single static
aarch64-phoenix decode ELF (Phase 1, decode-only, LGPL).

This packages the proven E4 feasibility recipe
(docs/inprogress/2026-08-06-ffmpeg-port-feasibility.md) into a reproducible
driver that mirrors the tools/*-port convention (quake3-port / yquake2-port):
the upstream engine source is NOT vendored; this script fetches + pins it, and
only this driver + the demo glue + docs live in the coordination repo.

Pipeline:
  (a) locate or fetch+pin ffmpeg n6.1 into external/ffmpeg
  (b) ./configure for the cross target (decode-only, LGPL, asm on)
  (c) patch the GENERATED config.h: flip HAVE_{ERF,EXP2,EXP2F,LOG2F} 0 -> 1
  (d) build libav{util,codec,format}.a from THIS run's objects
  (e) link e4_decode_demo.c -> static AArch64 ELF against the FRESH libphoenix.a
  + verify: readelf -h (AArch64/EXEC) and 0 undefined externals (nm ' U ').

Why the config.h patch (c): ffmpeg's ./configure link-probes erf/exp2/exp2f/
log2f against the toolchain's STALE sysroot libphoenix, which declares them in
math.h but does not define them -> the probe fails -> configure sets
HAVE_*=0 -> ffmpeg emits its own `static inline` fallbacks in libavutil/libm.h,
which then clash with libphoenix's non-static prototypes (a compile error). The
FRESH buildroot libphoenix.a we actually link against DOES define all four, so
flipping the four HAVE_* flags to 1 is both the whole compile fix and
link-honest. We patch only the generated config.h -- never ffmpeg source.

Usage:
  python3 tools/ffmpeg-port/build-ffmpeg-phoenix.py
    -> fetch/pin external/ffmpeg, configure, build, link /tmp/e4_decode-phoenix

  FFMPEG_SRC=/path/to/ffmpeg python3 tools/ffmpeg-port/build-ffmpeg-phoenix.py
    -> use an existing clone AS-IS (no clone, no checkout); for fast local
       testing against an already-present n6.1 tree.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TCBIN = f"{ROOT}/.toolchain/aarch64-phoenix/bin"
TC = f"{TCBIN}/aarch64-phoenix-gcc"
READELF = f"{TCBIN}/aarch64-phoenix-readelf"
NM = f"{TCBIN}/aarch64-phoenix-nm"
SIZE = f"{TCBIN}/aarch64-phoenix-size"

# Upstream ffmpeg, pinned. Record any bump here + in the README + commit msg.
FFMPEG_URL = "https://github.com/FFmpeg/FFmpeg.git"
FFMPEG_TAG = "n6.1"
FFMPEG_SHA = "d4ff0020b40b524a490cf62eccbd3a318f4c0e58"
# Default (committed) source location; FFMPEG_SRC env overrides + uses as-is.
FFMPEG = os.environ.get("FFMPEG_SRC", f"{ROOT}/external/ffmpeg")
USE_AS_IS = "FFMPEG_SRC" in os.environ

# Fresh cross-built libc (has the new libm: erf/exp2/exp2f/log2f/scalbn...).
# The toolchain sysroot copy is STALE and must NOT be used for the link.
LIBPHOENIX = f"{ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/lib/libphoenix.a"

# Demos (pick via DEMO_SRC=...):
#   e4_decode_file.c  (default) REAL on-target MJPEG decode. HW-VALIDATED: 96x64 jpeg, plane0
#                     avg 127 == host ffmpeg.
#   e4_decode_h264.c  REAL on-target H.264 decode. HW-VALIDATED: 128x96 Annex-B, plane0 avg 123
#                     == host ffmpeg (bit-exact). Runs the decode on an 8 MB-stack pthread — H.264's
#                     DPB/deblocking overflow the small default main-thread stack (mjpeg does not).
#                     Build h264 in via --enable-decoder=h264 --enable-parser=h264 (already in CONFIGURE).
#   e4_fbshow.c       REAL decode → /dev/fb0 → HDMI (needs e4_fb_blit.h). HW-VALIDATED: a
#                     1280x720 jpeg decoded + displayed centered on the HDMI output with correct
#                     colors (YUV420→32bpp, byte order per pl011-tty). The first VISIBLE output.
#   e4_play.c         REAL MOVING VIDEO PLAYER — decode a multi-frame h264 clip in a loop, pace
#                     (usleep), and blit each frame to /dev/fb0. HW-VALIDATED: a color-cycling clip
#                     played 7 loops / 294 frames on the HDMI screen with visible motion, 0 faults.
#                     Runs on an 8MB-stack pthread (h264). Generate a clip with gen_e4_clip.py.
#   e4_decode_demo.c  minimal link-only variant (decodes nothing), kept for reference.
DEMO = os.environ.get("DEMO_SRC", f"{ROOT}/tools/ffmpeg-port/e4_decode_file.c")
ELF = "/tmp/e4_decode-phoenix"

# The proven decode-only, LGPL configure line. NO --enable-gpl / --enable-nonfree.
# asm ON (NEON) for perf; static single-ELF (no dlopen surface); no network/docs.
CONFIGURE = [
    "./configure",
    "--enable-cross-compile",
    "--arch=aarch64",
    "--target-os=none",
    "--cc=aarch64-phoenix-gcc",
    "--cross-prefix=aarch64-phoenix-",
    "--disable-everything",
    "--enable-decoder=mjpeg,h264,rawvideo,pcm_s16le",
    "--enable-parser=h264",
    "--enable-demuxer=mjpeg,wav",
    "--enable-protocol=file",
    "--enable-asm",
    "--disable-programs",
    "--disable-network",
    "--disable-shared",
    "--enable-static",
    "--disable-doc",
]

# The four libm HAVE_* flags configure zeroes against the stale sysroot (see
# module docstring). Anchored to whole macro names so HAVE_EXP2 cannot shadow
# HAVE_EXP2F.
LIBM_HAVE = ["HAVE_ERF", "HAVE_EXP2", "HAVE_EXP2F", "HAVE_LOG2F"]

ARCHIVES = [
    f"{FFMPEG}/libavformat/libavformat.a",
    f"{FFMPEG}/libavcodec/libavcodec.a",
    f"{FFMPEG}/libavutil/libavutil.a",
]


def toolchain_env():
    env = dict(os.environ)
    env["PATH"] = TCBIN + os.pathsep + env.get("PATH", "")
    return env


def run(cmd, cwd=None, env=None):
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd, env=env)


def dump_failure(label, cmd, r):
    """Print the exact command plus the COMPLETE stdout+stderr of a failed step.

    Every caller of this used to print a TAIL only (last 10-25 lines, stderr-only
    in places). A truncated log can drop the real cause entirely -- a configure/
    make/link tail that ends at `collect2: error: ld returned 1 exit status` with
    nothing explaining it, as reported from a Docker build on macOS. Both streams
    are printed in full because make/configure put compiler errors on stdout."""
    print(f"=== {label} (exit {r.returncode}) ===")
    print("$ " + " ".join(cmd))
    if r.stdout.strip():
        print("--- complete stdout ---")
        print(r.stdout.strip())
    print("--- complete stderr ---")
    print(r.stderr.strip() or "(no stderr)")
    sys.stdout.flush()


def fetch_source():
    """Clone + pin external/ffmpeg if absent. FFMPEG_SRC is used as-is (never
    cloned or checked out) so a shallow-graft test clone is left untouched."""
    if os.path.isfile(f"{FFMPEG}/configure"):
        return True
    if USE_AS_IS:
        print(f"FFMPEG_SRC set but no configure at {FFMPEG}")
        return False
    print(f"=== fetching ffmpeg {FFMPEG_TAG} -> {FFMPEG} ===")
    os.makedirs(os.path.dirname(FFMPEG), exist_ok=True)
    r = run(["git", "clone", "--depth", "1", "--branch", FFMPEG_TAG,
             FFMPEG_URL, FFMPEG])
    if r.returncode != 0:
        print(f"clone failed:\n{r.stderr}")
        return False
    # Pin: verify (or move to) the recorded SHA. Shallow clone of a tag lands
    # exactly on it; a full clone is pinned explicitly.
    head = run(["git", "-C", FFMPEG, "rev-parse", "HEAD"]).stdout.strip()
    if head != FFMPEG_SHA:
        co = run(["git", "-C", FFMPEG, "checkout", FFMPEG_SHA])
        if co.returncode != 0:
            print(f"could not pin {FFMPEG_SHA}:\n{co.stderr}")
            return False
    return True


def configure(env):
    print("=== configure (decode-only, LGPL, asm on) ===")
    r = run(CONFIGURE, cwd=FFMPEG, env=env)
    if r.returncode != 0:
        dump_failure("CONFIGURE FAILED", CONFIGURE, r)
        return False
    print("configure: exit 0")
    return True


def patch_config_h(env):
    """Flip HAVE_{ERF,EXP2,EXP2F,LOG2F} 0 -> 1 in the generated config.h.
    Fail loud if the substitution count is not exactly the expected 4."""
    cfg = f"{FFMPEG}/config.h"
    with open(cfg) as f:
        text = f.read()
    n = 0
    for macro in LIBM_HAVE:
        pat = re.compile(rf"^#define {macro} 0$", re.MULTILINE)
        text, k = pat.subn(f"#define {macro} 1", text)
        if k != 1:
            print(f"config.h patch: expected 1 '{macro} 0' line, found {k}")
            return False
        n += k
    if n != len(LIBM_HAVE):
        print(f"config.h patch: flipped {n}, expected {len(LIBM_HAVE)}")
        return False
    with open(cfg, "w") as f:
        f.write(text)
    # Grep-verify each landed as =1.
    with open(cfg) as f:
        after = f.read()
    for macro in LIBM_HAVE:
        if not re.search(rf"^#define {macro} 1$", after, re.MULTILINE):
            print(f"config.h patch: {macro} did not land as 1")
            return False
    print(f"config.h: flipped {n} libm HAVE_* flags to 1 ({', '.join(LIBM_HAVE)})")
    return True


def build_archives(env):
    """Remove any prior archives, then build them from THIS run's objects so the
    link is never against stale artifacts (the project's signature false-pass)."""
    for a in ARCHIVES:
        if os.path.exists(a):
            os.remove(a)
    targets = [os.path.relpath(a, FFMPEG) for a in ARCHIVES]
    print(f"=== make {' '.join(targets)} ===")
    make = ["make", "-j", str(os.cpu_count() or 4)] + targets
    r = run(make, cwd=FFMPEG, env=env)
    if r.returncode != 0:
        dump_failure("MAKE FAILED", make, r)
        return False
    for a in ARCHIVES:
        if not os.path.exists(a):
            print(f"archive not produced: {a}")
            return False
    for a in ARCHIVES:
        print(f"  {os.path.relpath(a, FFMPEG):32s} {os.path.getsize(a)/1e6:5.2f} MB")
    return True


def link_and_verify(env):
    obj = "/tmp/e4_decode_demo.o"
    print("=== compile demo ===")
    cc = [TC, "-c", "-O2", "-g", "-I", FFMPEG, "-o", obj, DEMO]
    r = run(cc, env=env)
    if r.returncode != 0:
        dump_failure("DEMO COMPILE FAILED", cc, r)
        return False
    # The proven link line: archives (format, codec, util) then the FRESH
    # libphoenix inside one group; -lm -lgcc trail. Fresh libphoenix is searched
    # before the driver's implicit stale -lphoenix, so the new libm wins.
    print("=== link ===")
    link = [TC, "-o", ELF, obj,
            "-Wl,--start-group",
            ARCHIVES[0], ARCHIVES[1], ARCHIVES[2], LIBPHOENIX,
            "-Wl,--end-group", "-lm", "-lgcc"]
    r = run(link, env=env)
    if r.returncode != 0:
        undef = sorted(set(
            l.split("undefined reference to ")[1].strip().strip("`'")
            for l in r.stderr.splitlines() if "undefined reference to" in l))
        print(f"LINK FAILED: {len(undef)} undefined")
        for s in undef:
            print(f"  U {s}")
        # Then the COMPLETE linker output. The last-10-lines tail this used to
        # print could reduce a real failure to a bare "collect2: error: ld
        # returned 1 exit status" with no indication of the cause.
        dump_failure("LINK FAILED", link, r)
        return False
    print(f"link: exit 0 -> {ELF}")

    # Verify: AArch64 EXEC + zero undefined externals.
    h = run([READELF, "-h", ELF], env=env).stdout
    is_aarch64 = "AArch64" in h
    is_exec = re.search(r"Type:\s+EXEC", h) is not None
    nm = run([NM, ELF], env=env).stdout
    # An undefined external prints with no address: "                 U symbol".
    undef = [l for l in nm.splitlines() if re.match(r"^\s+U ", l)]
    size = os.path.getsize(ELF)
    print("=== verify ===")
    print(f"  readelf: AArch64={is_aarch64}  Type=EXEC:{is_exec}")
    print(f"  size: {size} bytes ({size/1e6:.2f} MB)")
    print(f"  undefined externals (nm ' U '): {len(undef)}")
    for u in undef[:40]:
        print(f"    {u.strip()}")
    ok = is_aarch64 and is_exec and len(undef) == 0
    return ok


def main():
    if not os.path.exists(TC):
        print(f"missing toolchain gcc: {TC}")
        return 1
    if not os.path.exists(LIBPHOENIX):
        print(f"missing fresh libphoenix.a: {LIBPHOENIX}")
        print("  -> build it: ./scripts/rebuild-rpi4b-fast.sh --scope core")
        return 1
    if not fetch_source():
        return 1

    env = toolchain_env()
    if not configure(env):
        return 1
    if not patch_config_h(env):
        return 1
    if not build_archives(env):
        return 1
    ok = link_and_verify(env)

    print()
    if ok:
        print(f"=== PASS: static AArch64 decode ELF, 0 undefined -> {ELF} ===")
        subprocess.run([SIZE, ELF], env=env)
        return 0
    print("=== FAIL: see diagnostics above ===")
    return 2


if __name__ == "__main__":
    sys.exit(main())
