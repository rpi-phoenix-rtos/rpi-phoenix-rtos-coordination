#!/usr/bin/env bash
#
# Stage the scalable desktop fonts (DejaVu TTF) + fontconfig config + cache into
# the netboot NFS-root export, so the X11 desktop (Window Maker via the WINGs
# toolkit, xterm, dillo, ...) can resolve Xft/fontconfig fonts after a *fresh*
# rootfs re-export.
#
# WHY THIS EXISTS
#   The base build does not produce any scalable TTF font, /etc/fonts/fonts.conf,
#   or a fontconfig cache. Those were hand-staged into the export by hand once —
#   which means a pristine re-export (make-pristine-nfs-export.sh) or a Sat-night
#   clean rebuild would silently drop them, and wmaker would die with
#   "could not load any fonts". This script makes that staging reproducible and
#   is wired into sync-netboot-tree.sh so every restage guarantees the fonts.
#
#   The fonts.conf we deploy (tools/x11-port/fontconfig/fonts.conf) maps the
#   generic family names wmaker requests ("sans serif", "Sans", ...) to the one
#   TTF family we bundle (DejaVu), so XftFontOpenName() never returns NULL.
#
# SOURCE OF THE TTFs
#   Copied from the host's fonts-dejavu package (/usr/share/fonts/truetype/dejavu)
#   rather than committing binaries to git. If the host lacks them, install with
#   `sudo apt-get install fonts-dejavu-core fonts-dejavu-extra` (Debian/Ubuntu).
#
# Idempotent: safe to run repeatedly; re-copies fonts + regenerates the cache.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Resolve the export dir the same way sync-netboot-tree.sh does: the fsid=0
# NFSv4 pseudo-root actually served (the export the Pi mounts as "/"), overridable
# by RPI4B_NFS_EXPORT, falling back to the historical default.
# Read /etc/exports.d/*.exports too: the Phoenix export is declared there, and
# reading only /etc/exports silently fell through to the historical default
# /srv/phoenix-rpi4-nfs -- a directory nothing mounts, so the staging appeared to
# succeed while the Pi kept the old fonts (the dead-export trap again).
fsid0_export="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports /etc/exports.d/*.exports 2>/dev/null || true)"
export_dir="${RPI4B_NFS_EXPORT:-${fsid0_export:-/srv/phoenix-rpi4-nfs}}"

host_dejavu="${DEJAVU_SRC:-/usr/share/fonts/truetype/dejavu}"
conf_src="$repo/tools/x11-port/fontconfig/fonts.conf"

if [ ! -d "$export_dir" ]; then
	printf 'stage-desktop-fonts.sh: no NFS export at %s — nothing to stage (skipping)\n' "$export_dir"
	exit 0
fi
if [ ! -d "$host_dejavu" ]; then
	printf 'stage-desktop-fonts.sh: host DejaVu fonts not found at %s\n' "$host_dejavu" >&2
	printf '  install them (Debian/Ubuntu): sudo apt-get install fonts-dejavu-core fonts-dejavu-extra\n' >&2
	printf '  or point DEJAVU_SRC at a directory of DejaVu*.ttf\n' >&2
	exit 1
fi
if [ ! -f "$conf_src" ]; then
	printf 'stage-desktop-fonts.sh: missing %s\n' "$conf_src" >&2
	exit 1
fi

font_dst="$export_dir/usr/share/fonts/truetype/dejavu"
etc_fonts="$export_dir/etc/fonts"
cache_dir="$export_dir/var/cache/fontconfig"

printf 'stage-desktop-fonts.sh: staging desktop fonts -> %s\n' "$export_dir"

# 1) The scalable TTF family wmaker/Xft need.
install -d "$font_dst"
cp -f "$host_dejavu"/DejaVu*.ttf "$font_dst"/
printf '  fonts: %s DejaVu TTF -> /usr/share/fonts/truetype/dejavu\n' "$(ls -1 "$font_dst"/DejaVu*.ttf | wc -l | tr -d ' ')"

# 2) The self-contained fontconfig config (generic-family aliases -> DejaVu).
install -d "$etc_fonts"
cp -f "$conf_src" "$etc_fonts/fonts.conf"
printf '  config: /etc/fonts/fonts.conf\n'

# 3) The fontconfig cache, keyed to the Pi's view of the tree (--sysroot prepends
#    the export dir when scanning, so cache entries reference /usr/share/fonts,
#    not the host path). Non-fatal if fc-cache is missing — directory scanning
#    still resolves fonts at runtime, just slower on first open.
install -d "$cache_dir"
if command -v fc-cache >/dev/null 2>&1; then
	fc-cache -f --sysroot "$export_dir" /usr/share/fonts/truetype >/dev/null 2>&1 || \
		printf '  cache: fc-cache reported an issue (non-fatal; runtime dir-scan still works)\n'
	printf '  cache: /var/cache/fontconfig (%s files)\n' "$(ls -1 "$cache_dir" 2>/dev/null | wc -l | tr -d ' ')"
else
	printf '  cache: fc-cache not on host — skipped (runtime dir-scan still resolves fonts)\n'
fi

printf 'stage-desktop-fonts.sh: done\n'
