#!/usr/bin/env bash
#
# M9 (docs/gpu-new-lane/M9-scaled-fullscreen.md): build the `game-res` launcher
# (a lower fullscreen resolution for each new-lane game) for Phoenix, after a host
# dry-run test of the command line it builds for every game.
#
#   <out>/game-res        static aarch64-phoenix ELF (stage as /bin/game-res)
#   <out>/game-res-host   native build, GAME_RES_DRYRUN=1 checks only
#
# Writes only into <out> (default tools/gpu-lane/m9-res/out). No Pi, no /srv.
# Usage: tools/gpu-lane/m9-res/build.sh [--out <dir>]
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/out"
while [ $# -gt 0 ]; do
	case "$1" in
		--out) shift; out="${1:?--out needs a directory}" ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
mkdir -p "${out}"
S="${root}/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"

# --- host dry run: the exact argv per game ---
gcc -std=gnu11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -o "${out}/game-res-host" "${here}/game-res.c"
fails=0
check() {   # <expected line> <args...>
	local want="$1" got
	shift
	got="$(GAME_RES_DRYRUN=1 "${out}/game-res-host" "$@")"
	if [ "${got}" = "${want}" ]; then
		echo "GAMERES ok   $*"
	else
		echo "GAMERES FAIL $*"
		echo "   got:  ${got}"
		echo "   want: ${want}"
		fails=$((fails + 1))
	fi
}
check 'game-res: stk 1280x720 -> exec /bin/stk-drm --screensize=1280x720 --track=hacienda --numkarts=4 --profile-laps=2' \
	stk 1280x720 --track=hacienda --numkarts=4 --profile-laps=2
check 'game-res: qs 960x540 -> exec /usr/bin/quakespasm-drm -width 960 -height 540 -fullscreen' qs 960x540
check 'game-res: q2 1600x900 -> exec /usr/bin/quake2-drm +set vid_fullscreen 1 +set r_mode -1 +set r_customwidth 1600 +set r_customheight 900' \
	q2 1600x900
check 'game-res: q3 1024x768 -> exec /usr/bin/quake3-drm +set r_fullscreen 1 +set r_mode -1 +set r_modeFullscreen -1 +set r_customwidth 1024 +set r_customheight 768 +map q3dm1' \
	q3 1024x768 +map q3dm1
check 'game-res: vkq 1280x720 -> exec /usr/bin/vkquake-drm -basedir /usr/share/quake -width 1280 -height 720 -fullscreen +r_rtshadows 0 +map start' \
	vkq 1280x720
check 'game-res: qs 1920x1080 (default) -> exec /usr/bin/quakespasm-drm -width 1920 -height 1080 -fullscreen' qs
check 'game-res: stk 1366x768 (NOT a listed mode: the engine picks the closest or refuses) -> exec /bin/stk-drm --screensize=1366x768' \
	stk 1366x768
got="$(GAME_RES=800x600 GAME_RES_DRYRUN=1 "${out}/game-res-host" qs)"
if [ "${got}" = 'game-res: qs 800x600 -> exec /usr/bin/quakespasm-drm -width 800 -height 600 -fullscreen' ]; then
	echo "GAMERES ok   GAME_RES=800x600 qs"
else
	echo "GAMERES FAIL GAME_RES=800x600 qs: ${got}"
	fails=$((fails + 1))
fi
if GAME_RES_DRYRUN=1 "${out}/game-res-host" doom 1280x720 > /dev/null 2>&1; then
	echo "GAMERES FAIL an unknown game was accepted"
	fails=$((fails + 1))
else
	echo "GAMERES ok   unknown game refused"
fi
echo "GAMERES host result fails=${fails} verdict=$([ "${fails}" = 0 ] && echo PASS || echo FAIL)"
[ "${fails}" = 0 ] || exit 1

# --- the Phoenix build ---
"${TC}-gcc" -O2 -static -Wall -Wextra -Werror --sysroot="${S}/" -B"${S}/lib/" -iprefix "${S}/" \
	-o "${out}/game-res" "${here}/game-res.c"
if "${TC}-readelf" -l "${out}/game-res" 2>/dev/null | grep -q INTERP; then
	echo "build.sh: game-res has a PT_INTERP segment" >&2
	exit 1
fi
[ -z "$("${TC}-nm" -u "${out}/game-res" || true)" ] || { echo "build.sh: undefined symbols" >&2; exit 1; }
ls -l "${out}/game-res"
sha256sum "${out}/game-res"
echo "build.sh: OK"
