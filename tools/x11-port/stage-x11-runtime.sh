#!/usr/bin/env bash
#
# Phoenix-RTOS — stage the X11 runtime assets that Xlib/Xt apps need at run time
# but which live at compiled-in host-prefix paths (so they must be copied onto
# the NFS rootfs + located via env vars the launcher sets: XLOCALEDIR,
# XFILESEARCHPATH).
#
#   * the libX11 locale database  -> $NFS/usr/share/X11/locale
#       Without it XCreateFontSet cannot map the C locale to its charset
#       (iso8859-1) and Xaw apps (xcalc/xedit) abort with
#       "unable to load any usable fontset".  (#51)
#   * the X server bitmap font dir -> $NFS/usr/share/fonts/X11/misc
#       fonts.dir registers 6x13 under BOTH iso10646-1 and iso8859-1 (the latter
#       is what the C-locale fontset needs); fonts.alias maps the common
#       fixed-font names apps request (fixed/6x13/7x13/8x13/9x15/10x20) to it.
#
# Idempotent. Run after build-x11-phoenix.sh (which builds the locale DB into the
# shared prefix).
#
# Copyright 2026 Phoenix Systems
set -u

PREFIX=/tmp/x11-phoenix
NFS=/srv/phoenix-rpi4-nfs
FONTDIR=$NFS/usr/share/fonts/X11/misc
FIXED="-misc-fixed-medium-r-semicondensed--13-120-75-75-c-60"

fail() { echo "FAIL: $*"; exit 1; }

[ -d "$NFS" ] || fail "$NFS not present"

# --- libX11 locale database ---
if [ -f "$PREFIX/share/X11/locale/locale.dir" ]; then
	mkdir -p "$NFS/usr/share/X11"
	cp -a "$PREFIX/share/X11/locale" "$NFS/usr/share/X11/locale"
	echo "staged locale DB -> $NFS/usr/share/X11/locale"
else
	echo "WARN: $PREFIX/share/X11/locale missing — run build-x11-phoenix.sh first"
fi

# --- X server misc font dir (6x13 must exist; staged earlier) ---
if [ -f "$FONTDIR/6x13.pcf.gz" ]; then
	cat > "$FONTDIR/fonts.dir" <<EOF
3
6x13.pcf.gz ${FIXED}-iso10646-1
6x13.pcf.gz ${FIXED}-iso8859-1
cursor.pcf.gz cursor
EOF
	{
		for a in fixed variable 6x13 6x10 7x13 7x14 8x13 8x16 9x15 10x20; do
			echo "$a ${FIXED}-iso8859-1"
		done
	} > "$FONTDIR/fonts.alias"
	echo "wrote fonts.dir + fonts.alias (iso8859-1 fontset + common aliases) -> $FONTDIR"
else
	echo "WARN: $FONTDIR/6x13.pcf.gz missing — stage a misc bitmap font first"
fi

echo "=== X11 runtime assets staged ==="
