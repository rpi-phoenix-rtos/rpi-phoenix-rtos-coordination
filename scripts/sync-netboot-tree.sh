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
# ### THIS SCRIPT CAN NEVER PRODUCE A CLEAN EXPORT — BY DESIGN. ###
#
# No --delete means a binary that the build STOPPED producing (renamed, dropped, moved
# from /bin to /usr/bin, replaced by a framework port) stays on the export forever and
# the Pi keeps executing it. That is how the export became the reservoir of stale
# engines the owner hit on 2026-09-03: prepare-buildroot.sh wipes _fs/<target>/root on
# every rebuild, the build re-populates only what the chosen stage list covers, and this
# sync then tops the export back up from an incomplete tree without removing anything.
#
# So there are exactly two paths, and they are not interchangeable:
#   * THIS script          — incremental top-up between iterations. Additive only.
#   * make-pristine-nfs-export.sh — the ONLY way to get an export that contains nothing
#                            but what the current build produced. Use it for any
#                            clean-build verification or release test.
# Set SYNC_DELETE=1 to make this script mirror-with-deletion instead (equivalent to a
# pristine rebuild for the paths it covers, minus the junk audit).
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

delete_args=()
if [ "${SYNC_DELETE:-0}" = 1 ]; then
	delete_args=(--delete)
	printf 'sync-netboot-tree.sh: MIRROR mode (SYNC_DELETE=1) — files absent from the build WILL be removed from the export\n'
else
	printf 'sync-netboot-tree.sh: syncing base rootfs -> NFS export (no --delete; hand-staged assets preserved)\n'
	printf 'sync-netboot-tree.sh: NOTE this is ADDITIVE — anything the build stopped producing survives on the export.\n'
	printf '                      For a genuinely clean export use scripts/make-pristine-nfs-export.sh.\n'
fi
printf '  src: %s\n  dst: %s\n' "$src" "$export_dir"

# The Mesa shader disk cache is written by the Pi at runtime and is keyed by nothing
# the host can validate (the Phoenix Mesa build has no build-id — see
# docs project_v3d_shader_disk_cache). A cache written by a previous engine build
# renders GREEN SPECKLE over an otherwise valid frame, which reads as a GPU wedge and
# has cost debugging cycles. rsync never touches it (root-owned, and not in the source
# tree), so warn loudly and try to clear it; a failure here is informational only.
shader_cache="$export_dir/.mesa-shader-cache"
if [ -d "$shader_cache" ]; then
	if sudo -n rm -rf "$shader_cache" 2>/dev/null; then
		printf 'sync-netboot-tree.sh: cleared stale Mesa shader disk cache (%s)\n' "$shader_cache"
	else
		printf 'sync-netboot-tree.sh: WARNING stale Mesa shader disk cache present and NOT cleared:\n' >&2
		printf '                      %s\n' "$shader_cache" >&2
		printf '                      Run: sudo rm -rf %s\n' "$shader_cache" >&2
		printf '                      Leaving it can render green speckle that looks like a GPU wedge.\n' >&2
	fi
fi
# --no-owner --no-group: the sync runs as an unprivileged user and the NFS export
# may contain root-owned files (e.g. the fontconfig cache from stage-desktop-fonts);
# preserving owner/group needs root and makes rsync exit non-zero on chown/chgrp,
# aborting the sync. Ownership is irrelevant for the served rootfs, so skip it.
rsync -a --no-owner --no-group "${delete_args[@]+"${delete_args[@]}"}" \
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
