#!/usr/bin/env bash
#
# stage-game-data.sh — stage the RUNTIME game data for every shipped game engine
# into the rpi4b rootfs overlay, so ONE image (sd and nfsroot/netboot alike)
# carries playable data for all five engines.
#
# Why the overlay: phoenix-rtos-build/build.sh puts
# $PROJECT_PATH/rootfs-overlay first in ROOTFS_OVERLAYS, so whatever lands here
# is copied into _fs/<target>/root by the `fs` stage — which is exactly the tree
# the sd variant packs into the ext2 root AND the tree sync-netboot-tree.sh
# rsyncs into the NFS export. One staging path, both variants, no hand-copying.
#
# The ENGINES are built by the framework ports (ports.yaml: quakespasm, yquake2,
# quake3, vkquake, supertuxkart -> /usr/bin/...). This script owns only the data:
#
#   game  overlay path                                 source
#   ----  -------------------------------------------  ---------------------------
#   q1    usr/share/quake/id1/pak0.pak                 Quake shareware pak0
#   q2    usr/share/quake2/baseq2/pak0.pak             Quake II demo
#   q3    usr/share/quake3/demoq3/pak0.pk3             Quake III demo
#   stk   usr/share/supertuxkart/data/                 pinned stk-code 1.4 tarball
#   stk   usr/share/supertuxkart/stk-assets/           pinned SuperTuxKart 1.4 APK
#
# The three Quake fetches delegate to scripts/fetch-quake-data.sh (the single
# download/extract/verify implementation, also used by the Docker build), so this
# script adds no second copy of that logic — only the SuperTuxKart half, which
# had no staging path at all before (its assets were hand-copied onto the NFS
# export, which is why "SuperTuxKart is not on the card" was true).
#
# SuperTuxKart needs BOTH roots at runtime, and they come from two different
# pinned upstream artifacts:
#   * data/       — gui, shaders, ttf, skins, po, stk_config.xml ... ships inside
#                   the stk-code 1.4 source tarball the port already pins, so it
#                   costs no extra download.
#   * stk-assets/ — the art (karts, tracks, models, textures, music, sfx,
#                   library). NOT in the source tarball. Taken from the official
#                   SuperTuxKart 1.4 Android package, whose assets/data/ holds
#                   the mobile-reduced 1.4 art set (~149 MB) version-locked to
#                   stk-code 1.4. See sources/phoenix-rtos-ports/supertuxkart
#                   commit 7ac46d8 for the pin.
# /bin/stk (scripts/build-rootfs-helpers.sh) points SUPERTUXKART_DATADIR at
# /usr/share/supertuxkart and SUPERTUXKART_ASSETS_DIR at .../stk-assets.
#
# Usage:
#   scripts/stage-game-data.sh [--force] [all|q1|q2|q3|stk ...]
#
#   --force   re-stage even if the destination already looks populated
#
# Env overrides (all optional; the defaults match the Dockerfile build args so a
# local stage and the authoritative Docker build fetch the SAME bytes):
#   OVERLAY_ROOT      rootfs-overlay dir (default: the rpi4b project overlay)
#   PAK0_URL          / PAK0_SHA256       Quake I shareware
#   PAK0Q2_URL        / PAK0Q2_SHA256     Quake II demo
#   PAK0Q3_URL        / PAK0Q3_SHA256     Quake III demo
#   STK_CODE_TARBALL  pinned stk-code source tarball (default: the port's copy)
#   STK_CODE_URL      where to fetch it when absent (a fresh clone has none)
#   STK_ASSETS_APK    pinned STK 1.4 APK (default: the port's copy, if present)
#   STK_ASSETS_URL    / STK_ASSETS_SHA256 where to fetch the APK when absent
#
# Setting a *_URL to the empty string SKIPS that game's data (the engine still
# builds and ships; it just has nothing to load). A non-empty URL that fails to
# download/extract/verify FAILS — we never ship a half-baked image.
#
# LICENSING: only freely-redistributable demo/shareware data, and SuperTuxKart is
# GPL/CC-licensed content. Never point these at full retail paks in a public
# build.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
overlay_root="${OVERLAY_ROOT:-${repo_root}/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/rootfs-overlay}"
ports_stk="${repo_root}/sources/phoenix-rtos-ports/supertuxkart"

# Defaults kept in lockstep with the Dockerfile ARGs (see Dockerfile lines ~41-53).
PAK0_URL="${PAK0_URL-https://ftp.icm.edu.pl/pub/coast/games/idsoftware/quake/quake106.zip}"
PAK0_SHA256="${PAK0_SHA256-35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af}"
PAK0Q2_URL="${PAK0Q2_URL-https://deponie.yamagi.org/quake2/idstuff/q2-314-demo-x86.exe}"
PAK0Q2_SHA256="${PAK0Q2_SHA256-cae257182f34d3913f3d663e1d7cf865d668feda6af393d4ecf3e9e408b48d09}"
PAK0Q3_URL="${PAK0Q3_URL-https://ftp.gwdg.de/pub/misc/ftp.idsoftware.com/idstuff/quake3/linux/linuxq3ademo-1.11-6.x86.gz.sh}"
PAK0Q3_SHA256="${PAK0Q3_SHA256-e77abad2466f45a0a7ea018445528f9b95a0fe7789fa1abc1a7718bbf0754b08}"

# SuperTuxKart pins. The APK URL/sha are the ones recorded in phoenix-rtos-ports
# commit 7ac46d8 ("supertuxkart: gitignore the 1.4 APK asset source").
STK_CODE_TARBALL="${STK_CODE_TARBALL:-${ports_stk}/stk-code-1.4.tar.gz}"
# ...and where to fetch it when absent. sources/phoenix-rtos-ports gitignores *.tar.gz,
# so a FRESH CLONE (the Docker build, and the owner's clean-build determinism chain)
# has no local copy — and this script runs before any ports stage could fetch one.
# Same URL the port's own `source`/`archive_filename` resolve to; the sha256 is read
# out of the port def below, so the pin cannot drift from the port's.
STK_CODE_URL="${STK_CODE_URL-https://github.com/supertuxkart/stk-code/archive/refs/tags/1.4.tar.gz}"
STK_ASSETS_APK="${STK_ASSETS_APK:-${ports_stk}/stk-1.4.apk}"
STK_ASSETS_URL="${STK_ASSETS_URL-https://github.com/supertuxkart/stk-code/releases/download/1.4/SuperTuxKart-1.4.apk}"
STK_ASSETS_SHA256="${STK_ASSETS_SHA256-29bded241025b4cca59e9cf6f7c2736002179c9c5e018fddaf747e1a4e08d454}"
# The art roots inside the APK's assets/data/ that make up stk-assets/. Everything
# else under assets/data/ duplicates the source tarball's data/, so it is skipped.
STK_ASSET_DIRS=(karts library models music sfx textures tracks)

force=0
games=()
while [ "$#" -gt 0 ]; do
	case "$1" in
		--force) force=1 ;;
		all) games+=(q1 q2 q3 stk) ;;
		q1|q2|q3|stk) games+=("$1") ;;
		-h|--help) sed -n '2,70p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) printf 'error: unknown argument: %s (want all|q1|q2|q3|stk|--force)\n' "$1" >&2; exit 2 ;;
	esac
	shift
done
[ "${#games[@]}" -gt 0 ] || games=(q1 q2 q3 stk)

log()  { printf '\033[0;36m[game-data]\033[0m %s\n' "$*"; }
warn() { printf '\033[0;33m[game-data] WARN\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[0;31m[game-data] ERROR\033[0m %s\n' "$*" >&2; exit 1; }

# populated <dir> — true when <dir> exists and holds at least one regular file.
populated() { [ -d "$1" ] && [ -n "$(find "$1" -type f -print -quit 2>/dev/null)" ]; }

verify_sha() {
	local file="$1" want="$2" got
	[ -n "$want" ] || return 0
	got="$(sha256sum "$file" | cut -d' ' -f1)"
	[ "$got" = "$want" ] || die "sha256 mismatch for $file (got $got, expected $want)"
}

stage_quake() {
	# stage_quake <q1|q2|q3> <dest-subdir> <url> <sha>
	local game="$1" subdir="$2" url="$3" sha="$4" dst="${overlay_root}/$2"
	if [ -z "$url" ]; then
		log "[$game] URL empty — skipping (engine ships without data)"
		return 0
	fi
	if [ "$force" = 0 ] && populated "$dst"; then
		log "[$game] already staged in $dst — skipping (use --force to re-fetch)"
		return 0
	fi
	OVERLAY_ROOT="$overlay_root" "${repo_root}/scripts/fetch-quake-data.sh" "$game" "$url" "$sha" \
		|| die "[$game] fetch-quake-data.sh failed"
}

stage_stk_data() {
	local dst="${overlay_root}/usr/share/supertuxkart/data"
	if [ "$force" = 0 ] && populated "$dst"; then
		log "[stk] data/ already staged in $dst — skipping"
		return 0
	fi
	local tmp; tmp="$(mktemp -d)"
	# shellcheck disable=SC2064
	trap "rm -rf '$tmp'" RETURN
	local tarball="$STK_CODE_TARBALL"
	if [ ! -f "$tarball" ]; then
		[ -n "$STK_CODE_URL" ] \
			|| die "[stk] no local stk-code tarball ($STK_CODE_TARBALL) and STK_CODE_URL is empty"
		log "[stk] fetching the pinned stk-code source: $STK_CODE_URL"
		curl -fSL --retry 5 --retry-delay 5 -o "$tmp/stk-code.tar.gz" "$STK_CODE_URL" \
			|| die "[stk] download failed from $STK_CODE_URL"
		tarball="$tmp/stk-code.tar.gz"
	else
		log "[stk] using the pinned local stk-code source: $tarball"
	fi
	# Verify against the sha256 the port itself pins, so the two can never drift.
	local want
	want="$(grep -m1 -E '^[[:space:]]*sha256=' "${ports_stk}/port.def.sh" | sed -E 's/.*"([0-9a-f]{64})".*/\1/')"
	if [ ${#want} -eq 64 ]; then
		verify_sha "$tarball" "$want"
	else
		warn "[stk] could not read the pinned sha256 out of ${ports_stk}/port.def.sh — staging unverified"
	fi
	log "[stk] extracting data/ from $(basename "$tarball")"
	mkdir -p "$tmp/data"
	tar xzf "$tarball" -C "$tmp/data" --strip-components=2 'stk-code-1.4/data' \
		|| die "[stk] could not extract stk-code-1.4/data from $tarball"
	rm -rf "$dst"
	mkdir -p "$(dirname "$dst")"
	mv "$tmp/data" "$dst"
	# mktemp -d is 0700; the image must not ship a data root only root can traverse.
	chmod 755 "$dst"
	log "[stk] staged data/ -> $dst ($(du -sh "$dst" | cut -f1))"
}

stage_stk_assets() {
	local dst="${overlay_root}/usr/share/supertuxkart/stk-assets"
	if [ "$force" = 0 ] && populated "$dst"; then
		log "[stk] stk-assets/ already staged in $dst — skipping"
		return 0
	fi
	local apk="$STK_ASSETS_APK" tmp
	tmp="$(mktemp -d)"
	# shellcheck disable=SC2064
	trap "rm -rf '$tmp'" RETURN
	if [ ! -f "$apk" ]; then
		[ -n "$STK_ASSETS_URL" ] \
			|| die "[stk] no local APK ($STK_ASSETS_APK) and STK_ASSETS_URL is empty — cannot stage stk-assets"
		log "[stk] fetching the pinned 1.4 asset package: $STK_ASSETS_URL"
		curl -fSL --retry 5 --retry-delay 5 -o "$tmp/stk.apk" "$STK_ASSETS_URL" \
			|| die "[stk] download failed from $STK_ASSETS_URL"
		apk="$tmp/stk.apk"
	else
		log "[stk] using the pinned local asset package: $apk"
	fi
	verify_sha "$apk" "$STK_ASSETS_SHA256"

	command -v unzip >/dev/null 2>&1 || die "[stk] unzip is required to unpack the asset package"
	local d patterns=()
	for d in "${STK_ASSET_DIRS[@]}"; do patterns+=("assets/data/${d}/*"); done
	log "[stk] extracting the art roots (${STK_ASSET_DIRS[*]})"
	unzip -oq "$apk" "${patterns[@]}" -d "$tmp/ex" || die "[stk] unzip of the asset package failed"
	[ -d "$tmp/ex/assets/data" ] || die "[stk] asset package has no assets/data/ — is this the right APK?"
	for d in "${STK_ASSET_DIRS[@]}"; do
		[ -d "$tmp/ex/assets/data/$d" ] || die "[stk] asset package is missing assets/data/$d"
	done
	rm -rf "$dst"
	mkdir -p "$(dirname "$dst")"
	mv "$tmp/ex/assets/data" "$dst"
	chmod -R u+w "$dst"
	chmod 755 "$dst"
	log "[stk] staged stk-assets/ -> $dst ($(du -sh "$dst" | cut -f1))"
}

# QuakeSpasm's SDL2 video path defaults to 800x600, and it reads config.cfg
# BEFORE VID_Init, so the shipped image has to state the mode or the game comes
# up in a 800x600 window inside the 1920x1080 scanout -- the rest of the
# framebuffer keeps whatever the console last drew, which looks exactly like a
# torn/shredded frame and reads as a GPU bug. It only ever worked before because
# the hand-maintained NFS export happened to carry a config.cfg written by an
# earlier run; a pristine rootfs correctly has no such runtime state, so the
# mode has to be part of the image.
stage_q1_video_cfg() {
	local dst="${overlay_root}/usr/share/quake/id1/config.cfg"

	# Do not clobber a config the user (or the game) has written.
	if [ -f "$dst" ]; then
		log "q1: config.cfg already present — left alone"
		return 0
	fi

	cat >"$dst" <<'CFG'
// Shipped by scripts/stage-game-data.sh so the game is full-screen out of the
// box. QuakeSpasm's default is 800x600; the Pi 4 scanout is 1920x1080, and a
// smaller mode leaves stale console pixels around the frame.
vid_width "1920"
vid_height "1080"
vid_fullscreen "1"
CFG
	log "q1: staged config.cfg (1920x1080 fullscreen)"
}

log "overlay root: $overlay_root"
mkdir -p "$overlay_root"
for g in "${games[@]}"; do
	case "$g" in
		q1)  stage_quake q1 usr/share/quake/id1     "$PAK0_URL"   "$PAK0_SHA256"
		     stage_q1_video_cfg ;;
		q2)  stage_quake q2 usr/share/quake2/baseq2 "$PAK0Q2_URL" "$PAK0Q2_SHA256" ;;
		q3)  stage_quake q3 usr/share/quake3/demoq3 "$PAK0Q3_URL" "$PAK0Q3_SHA256" ;;
		stk) stage_stk_data; stage_stk_assets ;;
	esac
done

log "summary of staged game data:"
for p in usr/share/quake/id1 usr/share/quake2/baseq2 usr/share/quake3/demoq3 \
	usr/share/supertuxkart/data usr/share/supertuxkart/stk-assets; do
	if populated "${overlay_root}/${p}"; then
		printf '  OK    %-36s %s\n' "$p" "$(du -sh "${overlay_root}/${p}" | cut -f1)"
	else
		printf '  MISS  %-36s (not staged)\n' "$p"
	fi
done
log "done"
