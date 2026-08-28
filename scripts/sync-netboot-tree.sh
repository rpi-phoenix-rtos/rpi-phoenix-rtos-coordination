#!/usr/bin/env bash
#
# Sync the freshly-built base rootfs into the netboot NFS-root export, so the userspace the
# Pi mounts over NFS matches the kernel/bootfs served over TFTP from the buildroot.
#
# On the Linux dev host the netboot path serves TWO trees from different sources:
#   - kernel + bootfs : TFTP, straight from the buildroot -> always current after a rebuild.
#   - root filesystem : NFS, from RPI4B_NFS_EXPORT (default /srv/phoenix-rpi4-nfs).
# The NFS root is a hand-maintained SUPERSET: the base rootfs PLUS hand-staged games/assets
# (baseq2, /usr/bin/yquake2, X11 app configs, ...) that are not produced by a base build.
# Nothing kept it in step with the build, so it drifted (observed 2026-08-05: ~2 weeks stale
# userspace on a fresh kernel) -> syscall/errno ABI mismatches that masquerade as "NFS
# runtime-read" failures. This script closes that gap.
#
# We rsync WITHOUT --delete so hand-staged extras survive, and skip volatile/remounted dirs
# (/dev, /proc, /tmp are re-bound at the nfs-fs takeover; /mnt is a mountpoint). Run it after a
# rebuild and before a netboot cycle; netboot-server-up.sh calls it automatically.
#
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
buildroot="${PHOENIX_BUILDROOT:-$repo/.buildroot}"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
src="$buildroot/_fs/$target/root"

# Default the sync target to the NFSv4 pseudo-root actually served — the export
# marked fsid=0 in /etc/exports is what the Pi mounts as "/". Detecting it (rather
# than hardcoding a name) stops the drift where the export dir gets renamed
# (e.g. a leftover -gcc16 root) but the sync keeps writing the old default, so the
# Pi silently boots a stale userspace. An explicit RPI4B_NFS_EXPORT still wins;
# fall back to the historical default if no fsid=0 export is found.
fsid0_export="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports 2>/dev/null || true)"
export_dir="${RPI4B_NFS_EXPORT:-${fsid0_export:-/srv/phoenix-rpi4-nfs}}"

if [ ! -d "$export_dir" ]; then
	printf 'sync-netboot-tree.sh: no NFS export at %s — nothing to sync (skipping)\n' "$export_dir"
	exit 0
fi
if [ ! -d "$src" ]; then
	printf 'sync-netboot-tree.sh: no built rootfs at %s — build first (skipping)\n' "$src"
	exit 0
fi

printf 'sync-netboot-tree.sh: syncing base rootfs -> NFS export (no --delete; hand-staged assets preserved)\n'
printf '  src: %s\n  dst: %s\n' "$src" "$export_dir"
rsync -a \
	--exclude=/dev \
	--exclude=/proc \
	--exclude=/tmp \
	--exclude=/mnt \
	"$src/" "$export_dir/"

# The base build produces no scalable TTF / fontconfig config / cache, so the X11
# desktop (wmaker via WINGs, xterm, dillo) would have no Xft fonts on a fresh
# re-export. Stage them reproducibly here (idempotent; non-fatal so a font hiccup
# never blocks the rootfs sync). See scripts/stage-desktop-fonts.sh.
RPI4B_NFS_EXPORT="$export_dir" "$repo/scripts/stage-desktop-fonts.sh" || \
	printf 'sync-netboot-tree.sh: desktop-font staging reported an issue (non-fatal)\n'

printf 'sync-netboot-tree.sh: done\n'
