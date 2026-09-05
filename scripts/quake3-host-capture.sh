#!/usr/bin/env bash
# quake3-host-capture.sh — build + run the HOST reference quake3e headless
# (SDL offscreen + llvmpipe software GL) with the deterministic demo-video
# capture, producing cap_NNNN.tga frames that pair 1:1 (by frame index) with the
# Pi's capture for the visual-regression harness. The q3 analogue of
# scripts/quake-host-capture.sh (Quake1/quakespasm).
#
# quake3e (ec-/quake3e) has NO classic per-frame `cl_avidemo` TGA dumper (that is
# vanilla ioquake3). Its built-in per-frame capture is the `video` command (AVI
# writer, cl_avi.c). During AVI recording the engine FORCES a fixed timestep
# (msec = 1000/cl_aviFrameRate, cl_main.c CL_Frame) so a demo advances
# deterministically regardless of wall-clock — this is the q3 equivalent of Q1's
# `host_framerate`. RNG-driven client effects (marks/gibs/brass) are disabled for
# cross-arch determinism, mirroring Q1's `r_particles 0`.
#
# TRANSPORT: we keep `+video` (to engage that fixed timestep + the CA_ACTIVE gate)
# but the port's capture hook (external/quake3e tr_init.c Q3Cap_Stream, active when
# scr_capture>0) intercepts each video frame and writes it as a per-frame BGR TGA
# — the SAME code path (byte-for-byte TGA writer + readback) the Pi runs, so host
# and Pi frames pair by construction. This replaces the old AVI+ffmpeg split, which
# risked host/Pi format skew (bottom-up-ness, R/B order, gamma) between two
# different pipelines. On the Pi the hook streams over TCP instead; here (empty
# scr_capture_host) it writes files straight into $OUT. No ffmpeg needed.
#
# The 1999 demo-release `.dm3` demos shipped in demoq3 use an old protocol
# quake3e refuses (DEMOEXT is `dm_<proto>`), so the reference camera path is a
# committed protocol-68 demo (tools/quake3-port/demos/cap.dm_68) recorded once on
# q3dm7 in spectator-follow mode. Both host and Pi replay that SAME file.
#
# See docs/inprogress/2026-08-22-quake23-visual-harness.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
Q3_SRC="$ROOT/external/quake3e"
HOSTDIR="${QUAKE3_HOST_DIR:-/tmp/quake3-host}"
# Read-only source of the game paks (the Pi NFS export).
# Resolve the LIVE export (the fsid=0 one the Pi actually mounts), not the
# historical /srv/phoenix-rpi4-nfs literal: that tree is superseded by the -gcc16
# export and nothing mounts it, so a default pointing there reads stale assets (or
# nothing at all). Read /etc/exports.d/*.exports too -- the Phoenix export lives
# there, and scripts that looked only at /etc/exports fell through to the dead path.
_fsid0="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports /etc/exports.d/*.exports 2>/dev/null || true)"
_export="${RPI4B_NFS_EXPORT:-${_fsid0:-/srv/phoenix-rpi4-nfs}}"
PAK_SRC="${QUAKE3_PAK_SRC:-${_export}/usr/share/quake3/demoq3}"
DEMO_SRC="${QUAKE3_DEMO_SRC:-$ROOT/tools/quake3-port/demos/cap.dm_68}"
DEMO="${QUAKE3_DEMO:-cap}"          # demo name (without demos/ prefix or .dm_68)
FPS="${QUAKE3_FPS:-25}"             # cl_aviFrameRate: demo-time per frame = 1000/FPS ms
WIDTH="${QUAKE3_WIDTH:-1024}"
HEIGHT="${QUAKE3_HEIGHT:-768}"
WAITFRAMES="${QUAKE3_WAIT:-800}"    # +wait N game-frames before +stopvideo (caps capture length)
OUT="$HOSTDIR/cap"                  # cap_*.tga land here (compare.py --host)

fail() { echo "ERROR: $*" >&2; exit 1; }

command -v sdl2-config >/dev/null || fail "sdl2-config not found (SDL2 dev headers required to build quake3e)"
[ -d "$Q3_SRC/code" ] || fail "missing quake3e clone at $Q3_SRC (git clone https://github.com/ec-/quake3e)"
[ -f "$PAK_SRC/pak0.pk3" ] || fail "missing game paks at $PAK_SRC/pak0.pk3"
[ -f "$DEMO_SRC" ] || fail "missing reference demo $DEMO_SRC (regenerate with RECORD=1; see the harness doc)"

echo "== build host quake3e (opengl1 renderer baked in, to MATCH the Pi build; audio/curl/vulkan off) =="
# USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=opengl folds the fixed-function opengl1
# renderer straight in — the same renderer tools/quake3-port builds for the Pi
# (RENDERER_DEFAULT=opengl), so host and Pi frames are directly comparable.
make -C "$Q3_SRC" USE_RENDERER_DLOPEN=0 RENDERER_DEFAULT=opengl \
     USE_CURL=0 USE_VULKAN=0 BUILD_SERVER=0 -j"$(nproc)" >/dev/null
Q3BIN="$(ls "$Q3_SRC"/build/release-*/quake3e* 2>/dev/null | grep -v '_opengl\|_vulkan\|\.avi' | head -1)"
[ -x "$Q3BIN" ] || fail "quake3e binary not found after build"
echo "   binary: $Q3BIN"

echo "== stage gamedir $HOSTDIR/demoq3 =="
mkdir -p "$HOSTDIR/demoq3/demos" "$OUT"
for p in pak0.pk3 pak1.pk3; do
    [ -f "$PAK_SRC/$p" ] && { [ -f "$HOSTDIR/demoq3/$p" ] || cp "$PAK_SRC/$p" "$HOSTDIR/demoq3/$p"; }
done
cp "$DEMO_SRC" "$HOSTDIR/demoq3/demos/$DEMO.dm_68"

# CRITICAL — shared render baseline. The Pi auto-execs the export's q3config.cfg
# at startup (r_customwidth 1920, com_blood 1, r_picmip/r_vertexLight/r_textureMode,
# ...); the host on a fresh /tmp would otherwise run on quake3e defaults, so EVERY
# Pi frame would diverge systematically (this is the exact Q1 stale-scr_conscale
# trap). Stage the SAME q3config.cfg on the host so both sides share one baseline
# by construction; autocap.cfg (exec'd after both configs, from the command line)
# overrides the capture + determinism + resolution cvars on top. Copy each run so
# the engine's quit-time config writeback can't drift the baseline.
[ -f "$PAK_SRC/q3config.cfg" ] && cp "$PAK_SRC/q3config.cfg" "$HOSTDIR/demoq3/q3config.cfg"

# Determinism cvars (the q3 analogue of Q1's `r_particles 0`): disable the
# rand()-driven client-side effects that would desync across arch (libphoenix vs
# glibc rand()). cl_aviMotionJpeg 0 => lossless raw BGR AVI (MJPEG would wreck
# blacktex%). cl_forceavidemo 0 (default) => capture only while CA_ACTIVE, so
# both machines start at demo snapshot 0 and skip the variable-length load frames.
# Resolution is pinned HERE (not just on the command line) so the identical
# autocap.cfg drives host and Pi to the same frame size regardless of q3config.
cat > "$HOSTDIR/demoq3/autocap.cfg" <<EOF
set r_mode -1
set r_customwidth $WIDTH
set r_customheight $HEIGHT
set r_fullscreen 0
set r_fbo 0
set cg_marks 0
set cg_gibs 0
set cg_brassTime 0
set com_blood 0
set cg_blood 0
set cg_drawFPS 0
set cl_aviMotionJpeg 0
set cl_aviFrameRate $FPS
set cl_forceavidemo 0
set scr_capture 1
set scr_capture_max 0
set scr_capture_dir "$OUT"
EOF

rm -rf "$HOSTDIR/demoq3/videos"
mkdir -p /tmp/q3home
# Clear any prior frames so a short run can't leave stale higher-index TGAs behind.
rm -f "$OUT"/cap_*.tga

echo "== run headless (SDL offscreen + llvmpipe), replaying demo '$DEMO', capturing per-frame TGA =="
# +video engages the fixed timestep + CA_ACTIVE gate; scr_capture (set in
# autocap.cfg) makes the port's Q3Cap_Stream hook write each video frame directly
# as $OUT/cap_NNNN.tga (identical writer to the Pi TCP path). The AVI file itself
# stays empty — the hook returns before the built-in AVI writer runs.
timeout 300 env HOME=/tmp/q3home SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
    "$Q3BIN" +set fs_basepath "$HOSTDIR" +set fs_homepath "$HOSTDIR" +set fs_game demoq3 \
    +set r_mode -1 +set r_customwidth "$WIDTH" +set r_customheight "$HEIGHT" +set r_fullscreen 0 \
    +set com_maxfps 0 +set s_initsound 0 +set sv_pure 0 +exec autocap.cfg \
    +demo "$DEMO" +video +wait "$WAITFRAMES" +stopvideo +quit >/tmp/q3-host-play.log 2>&1 \
    || fail "engine run failed (see /tmp/q3-host-play.log)"

N=$(ls "$OUT"/cap_*.tga 2>/dev/null | wc -l)
[ "$N" -gt 0 ] || fail "no cap_*.tga frames produced (see /tmp/q3-host-play.log)"
echo "== done: $N host reference frames in $OUT/cap_*.tga =="
