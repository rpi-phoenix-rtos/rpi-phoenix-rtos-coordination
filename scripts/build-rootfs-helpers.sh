#!/usr/bin/env bash
#
# build-rootfs-helpers.sh — build the small static helper binaries the rootfs needs
# that no framework port produces, into the rootfs staging tree.
#
# Owner directive 2026-09-03: "Why are you hand copying old binaries. This makes
# zero sense! Never do this!" Everything executable on the Pi must be produced by
# the build. Ports cover the engines and the big userland; these four (soon five)
# tiny C programs live in the coordination repo's tools/ and had no build home, so
# they were being carried forward by hand from the previous NFS export. This script
# is their build home.
#
# The ENGINES are framework ports (ports.yaml -> /usr/bin/{quakespasm,yquake2,
# quake3e,vkquake,supertuxkart}). But psh cannot set environment variables and
# cannot chain commands, while three of the engines need env vars and/or a long
# fixed argv to find their data and match the 1920x1080-only /dev/fb0. That glue
# lives in tiny static C launchers under tools/*, and until now each was built by
# an ad-hoc one-off (or, for stk, a script that wrote straight into the live NFS
# export). This is the single place that builds all of them:
#
#   /bin/ram-stage-play  tools/ram-stage/ram-stage-play.c
#                        copy an asset tree into the /tmp RAM disk, then exec the
#                        engine against the RAM copy (NFS reads are latency-bound;
#                        ~20x per scattered read). Used by quake2 + quake3 below.
#   /usr/bin/quake2      tools/yquake2-port/quake2-launcher.c
#                        RAM-stage /usr/share/quake2, then yquake2 with the
#                        fb-native custom video mode + demo1.
#   /usr/bin/quake3      tools/quake3-port/quake3-launcher.c
#                        RAM-stage /usr/share/quake3, then quake3e with
#                        fs_basepath/fs_game pointed at the RAM copy.
#   /bin/stk             tools/supertuxkart-port/stk-launcher.c
#                        set SUPERTUXKART_{DATADIR,ASSETS_DIR,SAVEDIR}, seed a
#                        first-run profile, then exec supertuxkart at 1080p.
#   /usr/bin/pty-run     tools/pty-run/pty-run.c
#                        getty-style /dev/ptmx forwarder, for programs that want
#                        their own controlling terminal. Not a game helper, but the
#                        same class of thing: a coord-repo tool the export used to
#                        be hand-fed.
#
# quakespasm and vkquake need no launcher and get none: their Phoenix glue
# (ports/{quakespasm,vkquake}/glue/pl_phoenix_main.c, wait_for_gamedata()) probes
# /ramtmp/quake, /tmp/quake, /usr/share/quake, /opt/quake and / for id1/pak0.pak
# and takes an optional `-basedir <dir>` override, so `quakespasm` / `vkquake` with
# no arguments already finds the staged data. The old /bin/quakespasm,
# /bin/quakespasm-sdl and /bin/vkquake on the NFS export were not wrappers at all —
# they were the ad-hoc tools/*-port ENGINE builds, now superseded by the ports'
# /usr/bin/quakespasm and /usr/bin/vkquake.
#
# All four are static aarch64-phoenix ELFs built with the same .toolchain gcc as
# the engines, so they are ABI-consistent with them. Nothing here touches the NFS
# export: the staging tree is the same _fs/<target>/root that the ext2 packer and
# sync-netboot-tree.sh both consume, so one build reaches both variants.
#
# Usage:
#   scripts/build-rootfs-helpers.sh [--stage-dir DIR]
#
# Env:
#   SHOWCASE_STAGE_DIR / --stage-dir   rootfs staging tree
#                                      (default $RPI4B_BUILDROOT/_fs/<target>/root)
#   RPI4B_BUILDROOT, RPI4B_TARGET, PHOENIX_AARCH64_TOOLCHAIN
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
toolchain="${PHOENIX_AARCH64_TOOLCHAIN:-${repo_root}/.toolchain/aarch64-phoenix/bin}"
cc="${toolchain}/aarch64-phoenix-gcc"

stage_dir="${SHOWCASE_STAGE_DIR:-${buildroot}/_fs/${target}/root}"
while [ "$#" -gt 0 ]; do
	case "$1" in
		--stage-dir) shift; stage_dir="${1:?--stage-dir needs a value}" ;;
		-h|--help) sed -n '2,45p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) printf 'error: unknown option: %s\n' "$1" >&2; exit 2 ;;
	esac
	shift
done

log()  { printf '\033[0;36m[helpers]\033[0m %s\n' "$*"; }
die()  { printf '\033[0;31m[helpers] ERROR\033[0m %s\n' "$*" >&2; exit 1; }

[ -x "$cc" ] || die "cross compiler not found: $cc (run scripts/bootstrap-linux-host.sh)"
[ -d "$stage_dir" ] || die "staging tree does not exist: $stage_dir (run a build first)"

# "<source>|<install path under the staging tree>"
helpers=(
	"tools/ram-stage/ram-stage-play.c|bin/ram-stage-play"
	"tools/yquake2-port/quake2-launcher.c|usr/bin/quake2"
	"tools/quake3-port/quake3-launcher.c|usr/bin/quake3"
	"tools/supertuxkart-port/stk-launcher.c|bin/stk"
	"tools/pty-run/pty-run.c|usr/bin/pty-run"
)

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

log "cc         = $cc"
log "stage tree = $stage_dir"
for entry in "${helpers[@]}"; do
	src="${repo_root}/${entry%%|*}"
	dst="${stage_dir}/${entry##*|}"
	name="$(basename "$dst")"
	[ -f "$src" ] || die "helper source missing: $src"
	"$cc" -O2 -static -Wall -Wextra -o "${tmp}/${name}" "$src" \
		|| die "compile failed: $src"
	# Phoenix has no dynamic loader for ordinary programs, so a PT_INTERP here
	# would be a binary that cannot start at all — check rather than trust.
	if readelf -l "${tmp}/${name}" 2>/dev/null | grep -q INTERP; then
		die "$name has a PT_INTERP segment (not a static ELF)"
	fi
	install -Dm755 "${tmp}/${name}" "$dst"
	log "built $(printf '%-14s' "$name") -> ${dst#"${stage_dir}"/} ($(stat -c%s "$dst") bytes)"
done

log "done"
