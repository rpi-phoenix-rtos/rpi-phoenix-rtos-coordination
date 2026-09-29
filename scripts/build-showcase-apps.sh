#!/usr/bin/env bash
#
# Phoenix-RTOS RPi4 — showcase-app staging step.
#
# Everything the showcase image ships is a framework port now (the GPU stack, the
# games, the X and Wayland desktops, the userland apps: sources/phoenix-rtos-ports,
# listed in the project's ports.yaml). What remains here is the one step that is
# not a port: the small in-repo helper programs and data files of
# scripts/build-rootfs-helpers.sh (ram-stage-play, pty-run, game-res, the
# diagnostics), staged into the rootfs tree the image is packed from.
#
# It runs AFTER build.sh has populated _fs/<target>/root (the fs/core/ports/project
# stages), because those stages repopulate the tree and would clobber anything
# staged before them. scripts/rebuild-rpi4b-fast.sh --with-showcase calls it there.
#
# History: this script used to build the first GPU stack (host Mesa builds of a Mesa
# fork into static archives, "phase gpu", run before the ports stage) and the ad-hoc
# X11 pieces (the X11 lib stack, xlaunch, the glamor X
# server, the GL-in-X client). Both were deleted with that stack in GPU migration P3
# (docs/gpu-new-lane/MIGRATION.md §8, P3-removal.md).
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt

set -uo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"

# Rootfs staging tree the ext2 image consumes. Default matches
# build-rpi4b-rootfs-ext2.sh's RPI4B_ROOTFS_TREE. When wired into --variant sd
# the caller points this at _fs/<target>/root (post-build.sh) so apps land in
# the ext2 image; a standalone run may point it at the NFS export instead.
stage_dir="${SHOWCASE_STAGE_DIR:-${buildroot}/_fs/${target}/root}"

phase="stage"

usage() {
	cat <<'EOF'
Usage: build-showcase-apps.sh [--phase stage|all] [--stage-dir DIR]

Stage the in-repo helper programs and data files (scripts/build-rootfs-helpers.sh)
into the rootfs tree. Run AFTER build.sh populated it. Every other showcase program
is a framework port (ports.yaml).

Options:
  --phase stage   (default) the staging step; `all` is the same
  --phase gpu     accepted for old callers; does nothing (the GPU stack is ports)
  --stage-dir DIR override the rootfs staging tree (default:
                  $RPI4B_BUILDROOT/_fs/<target>/root)
  -h, --help      show this help

Environment:
  RPI4B_BUILDROOT, RPI4B_TARGET, SHOWCASE_STAGE_DIR
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--phase) shift; phase="${1:-}";;
		--stage-dir) shift; stage_dir="${1:-}";;
		-h|--help) usage; exit 0;;
		*) printf 'error: unknown option: %s\n' "$1" >&2; usage >&2; exit 2;;
	esac
	shift
done

case "$phase" in gpu|stage|all) ;; *) printf 'error: bad --phase %s\n' "$phase" >&2; exit 2;; esac

# --- logging helpers -------------------------------------------------------
c_hdr='\033[1;36m'; c_ok='\033[1;32m'; c_err='\033[1;31m'; c_off='\033[0m'
log()  { printf "${c_hdr}==> %s${c_off}\n" "$*"; }
ok()   { printf "${c_ok}[OK] %s${c_off}\n" "$*"; }
die()  { printf "${c_err}[FAIL] %s${c_off}\n" "$*" >&2; exit 1; }

if [ "$phase" = gpu ]; then
	ok "phase gpu: nothing to do (the GPU stack is built by the ports stage)"
	exit 0
fi

phase_stage() {
	log "PHASE stage — rootfs helpers -> ${stage_dir}"
	[ -d "${stage_dir}/bin" ] || die "staging tree ${stage_dir} has no bin/ — run this AFTER build.sh has populated the rootfs (fs/core stages)"

	# Pre-create the data-file destinations the helper script writes into.
	mkdir -p "${stage_dir}/usr/share" "${stage_dir}/etc" "${stage_dir}/usr/lib"

	# Hard-fail: the game launchers need ram-stage-play, and psh cannot set env vars or
	# chain commands, so these tiny static programs are the entry points. The game DATA
	# is staged into the rootfs overlay by scripts/stage-game-data.sh.
	log "rootfs helper binaries (ram-stage-play, pty-run, game-res, diagnostics)"
	SHOWCASE_STAGE_DIR="${stage_dir}" "${repo_root}/scripts/build-rootfs-helpers.sh" --stage-dir "${stage_dir}" \
		|| die "scripts/build-rootfs-helpers.sh failed"

	ok "PHASE stage complete — staged into ${stage_dir}"
}

phase_stage
