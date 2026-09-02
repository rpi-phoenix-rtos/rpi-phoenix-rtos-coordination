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
#       The FULL standard misc set (409 .pcf.gz + the upstream fonts.dir /
#       fonts.alias) is copied from the host X11 font tree. This is required for
#       NAMED bitmap fonts (#56): apps like xcalc/xedit/xterm request "8x13",
#       "9x15", "10x20", "-misc-fixed-*" — none of which are in the libXfont2
#       built-in set (only 6x13+cursor are built in), so they can ONLY be served
#       from this disk dir. The upstream fonts.dir already registers 6x13 under
#       iso8859-1/iso10646-1, so the #51 C-locale fontset chain
#       (fixed/6x13 alias -> iso8859-1) still resolves.
#       NOTE: do NOT regenerate a minimal hand-written fonts.dir here — the
#       earlier 3-line version (6x13+cursor only) silently broke every named
#       disk font (#56). Copy the full upstream dir verbatim instead.
#   * the X11 encodings DB -> $NFS/usr/share/fonts/X11/encodings
#       The server references .../encodings/encodings.dir (libfontenc) when
#       opening PCF fonts that name an encoding. Absent on the export until now;
#       staged here as a host-side correctness fix (and a prime suspect for #56
#       per-font load failures — verify with the xfontprobe HW run before
#       asserting it as the cause).
#
# Idempotent. Run after build-x11-phoenix.sh (which builds the locale DB into the
# shared prefix).
#
# Copyright 2026 Phoenix Systems
set -u

PREFIX=/tmp/x11-phoenix
# Destination is DERIVED, never hardcoded. /srv/phoenix-rpi4-nfs stopped being the
# served root when the export was renamed to /srv/phoenix-rpi4-nfs-gcc16 (it is the
# fsid=0 entry in /etc/exports that the Pi mounts as "/"), so this script was
# staging the locale DB and fonts into a directory nothing mounts, and the X clients
# on the Pi went on using whatever was already there. Prefer the rootfs staging tree
# (SHOWCASE_STAGE_DIR) so the content reaches the SD image as well; fall back to the
# fsid=0 export. Same detection as scripts/sync-netboot-tree.sh.
_fsid0_export="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports 2>/dev/null || true)"
NFS="${SHOWCASE_STAGE_DIR:-${RPI4B_NFS_EXPORT:-${_fsid0_export:-}}}"
FONTDIR=$NFS/usr/share/fonts/X11/misc
# Host X11 font tree (source of the standard bitmap fonts + encodings DB).
HOSTFONTS=/usr/share/fonts/X11

fail() { echo "FAIL: $*"; exit 1; }

[ -n "$NFS" ] || fail "no staging destination: set SHOWCASE_STAGE_DIR, or add an fsid=0 export to /etc/exports"
[ -d "$NFS" ] || fail "$NFS not present"

# --- libX11 locale database ---
if [ -f "$PREFIX/share/X11/locale/locale.dir" ]; then
	mkdir -p "$NFS/usr/share/X11"
	# rm first: `cp -a src dst` where dst already exists NESTS the tree as
	# dst/locale/locale, leaving the real (stale) locale DB in place. The header
	# claimed this script was idempotent; it was not.
	rm -rf "$NFS/usr/share/X11/locale"
	cp -a "$PREFIX/share/X11/locale" "$NFS/usr/share/X11/locale"
	echo "staged locale DB -> $NFS/usr/share/X11/locale"
else
	echo "WARN: $PREFIX/share/X11/locale missing — run build-x11-phoenix.sh first"
fi

# --- X server misc font dir: copy the FULL standard set + upstream indices ---
# (#56) Named disk fonts (8x13/9x15/10x20/-misc-fixed-*) live only here.
if [ -d "$HOSTFONTS/misc" ] && [ -f "$HOSTFONTS/misc/fonts.dir" ]; then
	mkdir -p "$FONTDIR"
	cp -a "$HOSTFONTS/misc/." "$FONTDIR/"
	n=$(grep -c '\.pcf' "$FONTDIR/fonts.dir" 2>/dev/null || echo '?')
	echo "staged full misc font dir ($n fonts.dir entries) + fonts.alias -> $FONTDIR"
else
	echo "WARN: $HOSTFONTS/misc (with fonts.dir) not found on host — named fonts will fail (#56)"
fi

# --- X11 encodings DB (libfontenc) ---
if [ -d "$HOSTFONTS/encodings" ] && [ -f "$HOSTFONTS/encodings/encodings.dir" ]; then
	mkdir -p "$NFS/usr/share/fonts/X11/encodings"
	cp -a "$HOSTFONTS/encodings/." "$NFS/usr/share/fonts/X11/encodings/"
	echo "staged encodings DB -> $NFS/usr/share/fonts/X11/encodings"
else
	echo "WARN: $HOSTFONTS/encodings missing on host — PCF fonts naming an encoding may fail"
fi

echo "=== X11 runtime assets staged ==="
