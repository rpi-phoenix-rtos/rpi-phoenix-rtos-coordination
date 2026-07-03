#!/usr/bin/env bash
#
# Phoenix-RTOS — build xbill (the classic "stop Bill" X11 game) for aarch64-phoenix.
#
# xbill is the headline X11 sample app: defend a network of computers from the
# Bill virus by clicking it off the wires. The classic xbill 2.1 ships THREE UI
# backends (GTK / Motif / Athena); this stack has Athena (libXaw) but neither
# GTK nor Motif, so we build the ATHENA backend. The stock configure.in is
# GTK-tilted (it auto-detects toolkits and is fragile under cross-compile), so
# this script bypasses ./configure entirely: it writes a minimal config.h and
# compiles the known Athena object set directly with the cross gcc — the same
# direct-compile idiom as apps/build.sh. Renders as an X client on the Xphoenix
# kdrive fbdev server (DISPLAY=:0), under twm.
#
# Athena object set (from Makefile.in's WIDGET selection):
#   core game logic + x11.o (shared core-X driver) + x11-athena.o (Xaw widgets)
# Link closure: libXaw/Xmu/Xt/Xpm/Xext over libX11 (+ xcb closure).
#
# ASSETS (runtime, IMPORTANT): xbill loads its sprites at runtime from
# <IMAGES>/bitmaps/*.xbm and <IMAGES>/pixmaps/*.xpm (x11.c), falling back to "."
# when <IMAGES> is absent. We compile IMAGES=/usr/share/xbill and stage
# the bitmaps/ + pixmaps/ trees there, so the sprites resolve regardless of cwd.
# SCOREFILE points at /usr/share/xbill/scores (xbill degrades gracefully
# if it is unwritable on the read-only-ish NFS export).
#
# SOURCE: the alistairmcmillan/Xbill GitHub mirror of xbill 2.1 (has x11-athena.c;
# the SourceForge 2.1.4 release is GTK-ONLY and has no Athena backend). Host-side
# build only. Idempotent: skips clone/compile when outputs already exist.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -u

APP=xbill
# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

TC=${ROOT}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT=${ROOT}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
PREFIX=/tmp/x11-phoenix
TOOLS=${ROOT}/tools/x11-port
SRC=$TOOLS/src
XDIR=$SRC/Xbill
ART=${ROOT}/artifacts/x11
NFS="${SHOWCASE_STAGE_DIR:-/srv/phoenix-rpi4-nfs}"
REPO=https://github.com/alistairmcmillan/Xbill.git

# Runtime asset + score locations on the netboot NFS export.
IMAGES_DIR=/usr/share/xbill
SCOREFILE=/usr/share/xbill/scores

# Force-include the port shim (re-declares strncasecmp/strcasecmp, which xbill's
# own shadowing "strings.h" hides from the system header). Committed under
# tools/x11-port/xbill/ so a fresh clone can still find it.
SHIM="$TOOLS/xbill/xbill-phoenix-shim.h"
APP_CFLAGS="-DMAXHOSTNAMELEN=256 -DO_NOFOLLOW=0 -DXOS_USE_MTSAFE_PWDAPI -D_POSIX_THREAD_SAFE_FUNCTIONS=200809L -include $SHIM"
# Athena toolkit closure (Xaw<->Xmu<->Xt<->X11 circular -> start/end-group).
XCLOSURE="-Wl,--start-group -lXaw7 -lXmu -lXt -lSM -lICE -lXpm -lXext -lX11 -lxcb -lXau -lXdmcp -lphoenix -lc -lm -Wl,--end-group"

# Athena UI object set: core game logic + the shared core-X driver (x11.c) +
# the Athena widget driver (x11-athena.c). gtk.c / x11-motif.c are excluded.
SRCS="Bill.c Bucket.c Cable.c Computer.c Game.c Horde.c Network.c OS.c \
Scorelist.c Spark.c UI.c util.c x11.c x11-athena.c"

fail() { echo "FAIL: $*"; exit 1; }

[ -f "$PREFIX/lib/libX11.a" ] || { "$TOOLS/build-x11-phoenix.sh" || fail "build-x11-phoenix.sh failed"; }

# --- 1. fetch the Athena-capable source (git mirror of xbill 2.1) ---
mkdir -p "$SRC"
if [ ! -d "$XDIR" ]; then
	echo "=== cloning $REPO ==="
	git clone --depth 1 "$REPO" "$XDIR" || fail "clone failed (network egress?)"
fi
[ -f "$XDIR/x11-athena.c" ] || fail "$XDIR/x11-athena.c missing — wrong source (GTK-only release?)"

# --- 2. write a minimal config.h (bypasses the GTK-tilted ./configure) ---
cat > "$XDIR/config.h" <<EOF
/* Minimal Phoenix config.h — Athena backend, written by build-xbill.sh. */
#define STDC_HEADERS 1
#define HAVE_UNISTD_H 1
#define USE_ATHENA 1
/* USE_MOTIF / USE_GTK intentionally undefined: not in the Phoenix X11 stack. */
EOF

# Runtime paths baked into the two files that reference them. Written into a
# header (not -D) to avoid shell quote-stripping of the embedded C string
# literals. Scorelist.c / x11.c include config.h indirectly? They do NOT, so
# the build force-includes this path header instead (see PATHS_HDR below).
PATHS_HDR="$XDIR/phoenix_paths.h"
cat > "$PATHS_HDR" <<EOF
/* Phoenix runtime asset/score paths — force-included by build-xbill.sh. */
#ifndef XBILL_PHOENIX_PATHS_H
#define XBILL_PHOENIX_PATHS_H
#define IMAGES    "$IMAGES_DIR"
#define SCOREFILE "$SCOREFILE"
#endif
EOF

# --- 3. compile each Athena object + link static (direct cross gcc) ---
echo "=== building $APP (Athena backend, direct compile) ==="
OBJS=""
( cd "$XDIR" && for s in $SRCS; do
	o="${s%.c}.o"
	# x11.c (IMAGES) and Scorelist.c (SCOREFILE) get the runtime-paths header
	# force-included; all files get the strncasecmp shim. Force-include avoids
	# shell quote-stripping of the embedded C string literals.
	paths=""
	case "$s" in
	Scorelist.c|x11.c) paths="-include $PATHS_HDR" ;;
	esac
	${TC}gcc --sysroot="$SYSROOT" $APP_CFLAGS $paths -I. -I"$PREFIX/include" \
		-c "$s" -o "$o" || exit 1
done ) || fail "compile failed"
for s in $SRCS; do OBJS="$OBJS $XDIR/${s%.c}.o"; done

${TC}gcc --sysroot="$SYSROOT" -static -o "$XDIR/$APP" $OBJS \
	-L"$PREFIX/lib" -L"$SYSROOT/lib" $XCLOSURE \
	>/tmp/$APP-link.log 2>&1 || { tail -30 /tmp/$APP-link.log; fail "link failed"; }
[ -x "$XDIR/$APP" ] || fail "$APP not produced"

# --- 4. publish + stage (binary + sprite trees + a scores seed) ---
mkdir -p "$ART"; cp "$XDIR/$APP" "$ART/$APP"
echo "=== published -> $ART/$APP ==="
if [ -d "$NFS/bin" ]; then
	cp "$XDIR/$APP" "$NFS/bin/$APP"; chmod 755 "$NFS/bin/$APP"
	# Stage the runtime sprite directories at the compiled-in IMAGES path.
	dest="$NFS/usr/share/xbill"
	mkdir -p "$dest/bitmaps" "$dest/pixmaps"
	cp "$XDIR"/bitmaps/*.xbm "$dest/bitmaps/" 2>/dev/null
	cp "$XDIR"/pixmaps/*.xpm "$dest/pixmaps/" 2>/dev/null
	# Seed an empty high-score file so the first read succeeds.
	[ -f "$dest/scores" ] || cp "$XDIR/scores" "$dest/scores" 2>/dev/null || : > "$dest/scores"
	echo "=== staged -> $NFS/bin/$APP (+ $dest/{bitmaps,pixmaps,scores}) ==="
fi

# --- 5. pre-flight checks (no Pi cycle) ---
echo "=== PRE-FLIGHT ==="
file "$ART/$APP"
case "$(file "$ART/$APP")" in
	*"ARM aarch64"*"statically linked"*) echo "[OK] aarch64 static ELF" ;;
	*) fail "binary is not an aarch64 static ELF" ;;
esac
und=$(${TC}nm -u "$ART/$APP" 2>/dev/null)
[ -z "$und" ] && echo "[OK] 0 undefined symbols" || { echo "$und"; fail "undefined symbols present"; }
strings "$ART/$APP" | grep -qx "$IMAGES_DIR" && echo "[OK] IMAGES dir compiled in ($IMAGES_DIR)" \
	|| fail "IMAGES path not in binary — sprites would not resolve"
echo "=== ALL PRE-FLIGHT CHECKS PASSED ==="
echo "HW: in an xterm under twm:  xbill &     (sprites load from $IMAGES_DIR)"
