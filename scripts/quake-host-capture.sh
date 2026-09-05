#!/usr/bin/env bash
# quake-host-capture.sh — build + run the HOST reference quakespasm headless
# (SDL offscreen + llvmpipe software GL) with the deterministic demo-capture
# mode, producing cap_NNNN.tga frames that pair 1:1 (by demo timestamp) with the
# Pi's capture for the visual-regression harness.
# See docs/inprogress/2026-06-15-quake-visual-regression-harness.md.
set -euo pipefail

QUAKE_SRC="$(cd "$(dirname "$0")/.." && pwd)/external/quakespasm/Quake"
HOSTDIR="${QUAKE_HOST_DIR:-/tmp/quake-host}"
# Resolve the LIVE export (the fsid=0 one the Pi actually mounts), not the
# historical /srv/phoenix-rpi4-nfs literal: that tree is superseded by the -gcc16
# export and nothing mounts it, so a default pointing there reads stale assets (or
# nothing at all). Read /etc/exports.d/*.exports too -- the Phoenix export lives
# there, and scripts that looked only at /etc/exports fell through to the dead path.
_fsid0="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports /etc/exports.d/*.exports 2>/dev/null || true)"
_export="${RPI4B_NFS_EXPORT:-${_fsid0:-/srv/phoenix-rpi4-nfs}}"
PAK0_SRC="${PAK0_SRC:-${_export}/usr/share/quake/id1/pak0.pak}"
NSHOTS="${NSHOTS:-120}"        # number of frames to capture
EVERY="${EVERY:-5}"           # capture every Nth rendered frame
DT="${DT:-0.05}"             # fixed demo-time per frame (host_framerate)

echo "== build host quakespasm (audio codecs off; visual only) =="
make -C "$QUAKE_SRC" USE_SDL2=1 SDL_CONFIG=sdl2-config \
     USE_CODEC_VORBIS=0 USE_CODEC_MP3=0 USE_CODEC_WAVE=0 -j4 >/dev/null
test -x "$QUAKE_SRC/quakespasm"

echo "== stage gamedir $HOSTDIR/id1 =="
mkdir -p "$HOSTDIR/id1"
[ -f "$HOSTDIR/id1/pak0.pak" ] || cp "$PAK0_SRC" "$HOSTDIR/id1/pak0.pak"
cat > "$HOSTDIR/id1/autoexec.cfg" <<EOF
host_framerate $DT
r_particles 0
scr_capture $EVERY
scr_capture_max $NSHOTS
EOF
rm -f "$HOSTDIR"/id1/cap_*.tga

echo "== run headless (SDL offscreen + llvmpipe), capturing $NSHOTS frames =="
timeout 300 env SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
    "$QUAKE_SRC/quakespasm" -basedir "$HOSTDIR" -width 1024 -height 768 \
    >/dev/null 2>&1 || true

N=$(ls "$HOSTDIR"/id1/cap_*.tga 2>/dev/null | wc -l)
echo "== done: $N host reference frames in $HOSTDIR/id1/cap_*.tga =="
