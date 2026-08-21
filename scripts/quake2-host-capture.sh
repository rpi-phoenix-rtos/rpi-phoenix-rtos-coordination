#!/usr/bin/env bash
# quake2-host-capture.sh — build + run the HOST reference yQuake2 (ref_gl1)
# headless (SDL offscreen + llvmpipe software GL) with the deterministic
# demo-capture hook, producing cap_NNNN.tga frames that pair 1:1 (by demo
# timestamp) with the Pi's capture for the visual-regression harness.
#
# Sibling of scripts/quake-host-capture.sh (the Quake1/QuakeSpasm reference).
# See docs/inprogress/2026-08-22-quake23-visual-harness.md.
#
# Determinism: timedemo 1 (render one frame per loop iteration, no wall-clock
# throttle) + fixedtime <usec> (advance cl.time a fixed amount per frame,
# yQuake2's host_framerate analog) + cl_particles 0 (particles are rand()-placed
# and would desync across libc RNGs). => frame N is the same demo moment on host
# and Pi.  The capture hook (external/yquake2 gl1_sdl.c) dumps every EVERY-th
# in-game 3D frame and quits after NSHOTS shots.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
YQ2="$ROOT/external/yquake2"
WT="${QUAKE2_HOST_SRC:-/tmp/yq2-host-src}"      # clean worktree (HEAD, no Phoenix port patch)
HOSTDIR="${QUAKE2_HOST_DIR:-/tmp/quake2-host}"
PAK0_SRC="${PAK0_SRC:-/srv/phoenix-rpi4-nfs/usr/share/quake2/baseq2/pak0.pak}"
DEMO="${DEMO:-q2demo1.dm2}"        # shipped demo inside pak0 (demos/q2demo1.dm2)
NSHOTS="${NSHOTS:-120}"            # number of frames to capture
EVERY="${EVERY:-5}"               # capture every Nth rendered 3D frame
FIXEDTIME="${FIXEDTIME:-50000}"   # fixed demo-time per frame, microsec (50 ms = 20 fps)
WIDTH="${WIDTH:-1024}"
HEIGHT="${HEIGHT:-768}"
JOBS="${JOBS:-4}"

echo "== deps check =="
command -v sdl2-config >/dev/null || { echo "FATAL: sdl2-config missing (install libsdl2-dev)"; exit 1; }
command -v gcc >/dev/null || { echo "FATAL: gcc missing"; exit 1; }
command -v make >/dev/null || { echo "FATAL: make missing"; exit 1; }
ls /usr/lib/*/dri/swrast_dri.so >/dev/null 2>&1 || ls /usr/lib/*/dri/*softpipe* >/dev/null 2>&1 || \
    echo "WARN: no obvious llvmpipe/softpipe DRI driver found; LIBGL_ALWAYS_SOFTWARE may fail"
test -f "$PAK0_SRC" || { echo "FATAL: baseq2 pak0.pak not found at $PAK0_SRC (set PAK0_SRC)"; exit 1; }
test -d "$YQ2/.git" || { echo "FATAL: yQuake2 clone missing at $YQ2"; exit 1; }

echo "== sync clean worktree $WT to external/yquake2 HEAD (capture hook, NO Phoenix port patch) =="
# The main external/yquake2 working tree carries the Phoenix port patch (vid.c
# renderer gate, dropped .so forwarders) applied uncommitted — those would break
# a native build that dlopen's ref_gl1.so/game.so. A detached worktree at HEAD is
# clean upstream + the committed capture hook only.
HEAD_SHA="$(git -C "$YQ2" rev-parse HEAD)"
if [ ! -d "$WT/.git" ] && [ ! -f "$WT/.git" ]; then
    git -C "$YQ2" worktree add --detach "$WT" "$HEAD_SHA"
fi
git -C "$WT" checkout -q --detach "$HEAD_SHA" 2>/dev/null || {
    git -C "$YQ2" worktree remove --force "$WT" 2>/dev/null || rm -rf "$WT"
    git -C "$YQ2" worktree prune
    git -C "$YQ2" worktree add --detach "$WT" "$HEAD_SHA"
}

echo "== build host yQuake2 (SDL2, no curl/openal; client + game + ref_gl1) =="
make -C "$WT" WITH_SDL3=no WITH_CURL=no WITH_OPENAL=no WITH_SYSTEMWIDE=no \
     -j"$JOBS" client game ref_gl1 >/dev/null
REL="$WT/release"
test -x "$REL/quake2"      || { echo "FATAL: build did not produce $REL/quake2"; exit 1; }
test -f "$REL/ref_gl1.so"  || { echo "FATAL: build did not produce $REL/ref_gl1.so"; exit 1; }
test -f "$REL/baseq2/game.so" || { echo "FATAL: build did not produce $REL/baseq2/game.so"; exit 1; }

echo "== stage gamedir + assets =="
mkdir -p "$REL/baseq2" "$HOSTDIR"
[ -f "$REL/baseq2/pak0.pak" ] || cp "$PAK0_SRC" "$REL/baseq2/pak0.pak"
rm -f "$HOSTDIR"/cap_*.tga

echo "== run headless (SDL offscreen + llvmpipe), capturing $NSHOTS frames of $DEMO =="
# fixedtime + timedemo => deterministic fixed-timestep demo playback.
timeout 600 env SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy \
    "$REL/quake2" -datadir "$REL" \
    +set vid_renderer gl1 +set vid_fullscreen 0 +set s_initsound 0 \
    +set r_mode -1 +set r_customwidth "$WIDTH" +set r_customheight "$HEIGHT" \
    +set r_vsync 0 +set cl_particles 0 +set con_notifytime 0 +set fixedtime "$FIXEDTIME" +set timedemo 1 \
    +set scr_capture "$EVERY" +set scr_capture_max "$NSHOTS" +set scr_capture_dir "$HOSTDIR" \
    +demomap "$DEMO" \
    >"$HOSTDIR/run.log" 2>&1 || true

N=$(ls "$HOSTDIR"/cap_*.tga 2>/dev/null | wc -l)
echo "== done: $N host reference frames in $HOSTDIR/cap_*.tga =="
if [ "$N" -eq 0 ]; then
    echo "FATAL: no frames captured — tail of $HOSTDIR/run.log:"
    tail -n 25 "$HOSTDIR/run.log" || true
    exit 1
fi
