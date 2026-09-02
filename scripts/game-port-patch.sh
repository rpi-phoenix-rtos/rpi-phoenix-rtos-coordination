#!/usr/bin/env bash
#
# game-port-patch.sh — generate each game port's patch FROM its fork.
#
# Our engine changes used to exist twice: as commits on the fork
# (external/<game>, branch phoenix-rpi4-port, published under rpi-phoenix-rtos)
# and as a hand-written patch under sources/phoenix-rtos-ports/<port>/patches/.
# Two hand-maintained representations drift, and they did: yQuake2's fork and
# patch each carried a fix the other lacked, so neither tree was complete
# (docs/misc/2026-09-02-game-source-of-truth-audit.md).
#
# So the fork is the single source of truth and the patch is a build artifact
# generated from it: `git diff <pinned upstream commit>..HEAD`. The pin comes
# from the port's own port.def.sh, and it must be real history in the fork —
# that is what keeps "pristine upstream + a visible patch" true while leaving
# exactly one place to edit.
#
#   ./scripts/game-port-patch.sh --check   # fail if any patch is stale (CI/audit)
#   ./scripts/game-port-patch.sh --regen   # rewrite the patches from the forks
#   ./scripts/game-port-patch.sh --check quakespasm   # one game
#
# Deliberately NOT excluding anything from the delta: the visual-regression
# capture harness is inert unless enabled (scr_capture defaults to 0, the
# GL-blit path is #ifdef QSS_PHOENIX, vkQuake's trace needs VKQ_TEXDBG), and
# shipping the exact tree we HDMI-verify is worth more than a smaller patch.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORTS="$ROOT/sources/phoenix-rtos-ports"
EXT="$ROOT/external"

# port dir | fork clone under external/ | patch file name
GAMES=(
	"quakespasm|quakespasm|0001-quakespasm-phoenix-v3d-single-elf.patch"
	"vkquake|vkquake|0001-vkquake-phoenix-v3dv-single-elf.patch"
	"yquake2|yquake2|0001-single-elf-static-link.patch"
	"quake3|quake3e|0001-quake3e-phoenix-single-elf.patch"
)

mode="${1:---check}"
only="${2:-}"
rc=0
skipped=0

case "$mode" in
--check | --regen) ;;
*)
	printf 'usage: %s [--check|--regen] [game]\n' "$0" >&2
	exit 2
	;;
esac

for entry in "${GAMES[@]}"; do
	IFS='|' read -r port clone patch <<<"$entry"
	[ -n "$only" ] && [ "$only" != "$port" ] && [ "$only" != "$clone" ] && continue

	def="$PORTS/$port/port.def.sh"
	forkdir="$EXT/$clone"
	target="$PORTS/$port/patches/$patch"

	if [ ! -d "$forkdir/.git" ]; then
		# A fresh clone has no external/ yet. Not an error: the patch in the
		# tree is what builds, and --check cannot judge it without the fork.
		printf '%-12s SKIP  (no fork clone at external/%s)\n' "$port" "$clone"
		skipped=$((skipped + 1))
		continue
	fi
	if [ ! -f "$def" ]; then
		printf '%-12s FAIL  (no port.def.sh at %s)\n' "$port" "$def"
		rc=1
		continue
	fi

	pin="$(grep -m1 -oP '^\s*commit="\K[0-9a-f]{40}' "$def")"
	if [ -z "$pin" ]; then
		printf '%-12s FAIL  (no commit="<sha>" pin in port.def.sh)\n' "$port"
		rc=1
		continue
	fi

	# The pin must be an ancestor of the fork branch, or the patch would be a
	# diff between unrelated trees (a rewritten/force-pushed fork, say).
	if ! git -C "$forkdir" merge-base --is-ancestor "$pin" HEAD 2>/dev/null; then
		printf '%-12s FAIL  (pinned %s is not an ancestor of the fork HEAD)\n' "$port" "${pin:0:12}"
		rc=1
		continue
	fi

	gen="$(mktemp)"
	if ! git -C "$forkdir" diff --no-color --no-ext-diff --no-renames "$pin..HEAD" >"$gen"; then
		printf '%-12s FAIL  (git diff failed)\n' "$port"
		rm -f "$gen"
		rc=1
		continue
	fi

	# `patch -p1` (what b_port_apply_patches uses) cannot apply a git binary
	# diff, so refuse to generate one rather than ship a patch that fails late.
	if grep -q '^GIT binary patch' "$gen"; then
		printf '%-12s FAIL  (delta contains a binary change; patch -p1 cannot apply it)\n' "$port"
		rm -f "$gen"
		rc=1
		continue
	fi

	stat="$(git -C "$forkdir" diff --shortstat --no-renames "$pin..HEAD" | sed 's/^ *//')"

	if [ "$mode" = "--regen" ]; then
		mkdir -p "$(dirname "$target")"
		if cmp -s "$gen" "$target"; then
			printf '%-12s OK    (already current: %s)\n' "$port" "$stat"
		else
			cp "$gen" "$target"
			printf '%-12s REGEN (%s)\n' "$port" "$stat"
		fi
	else
		if cmp -s "$gen" "$target"; then
			printf '%-12s OK    (%s)\n' "$port" "$stat"
		else
			printf '%-12s STALE (fork delta: %s) -- run: %s --regen %s\n' \
				"$port" "$stat" "$0" "$port"
			rc=1
		fi
	fi
	rm -f "$gen"
done

if [ "$skipped" -gt 0 ]; then
	printf '(%d port(s) skipped: no fork clone here)\n' "$skipped"
fi
exit "$rc"
