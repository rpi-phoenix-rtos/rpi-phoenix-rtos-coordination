#!/usr/bin/env bash
#
# check-rootfs-complete.sh — assert a staged rootfs contains everything the
# image is supposed to ship, and FAIL if it does not.
#
# Why this exists (2026-09-03): the exported SD image was complete in every
# binary and still shipped a broken Quake III, because
# usr/share/quake3/demoq3/pak1.pk3 (our ioquake3 VM pak, UI API 6) was absent.
# Without it the engine dies at startup with
#   ERROR: User Interface is version 3, expected 6
# The pak had been hand-staged into the live NFS export that morning, never into
# the rootfs-overlay, so recreating the export from the build tree correctly
# discarded it -- and nothing checked. The old completeness check printed "MISS"
# and exited 0, which is indistinguishable from success in a build log.
#
# One list, called from both the export path and the ext2 image path, because a
# list maintained in two places is how this class of bug survives.
#
#   ./scripts/check-rootfs-complete.sh <rootfs-dir>
#
# Exit 0 only when every required path is present and non-empty.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

root="${1:-}"
[ -n "${root}" ] && [ -d "${root}" ] || {
	printf 'check-rootfs-complete: usage: %s <rootfs-dir>\n' "$0" >&2
	exit 2
}

# Required = the image is broken or a headline feature is missing without it.
# Game DATA belongs here as much as the binaries: an engine with no data is not
# a shipped game, and that is precisely the failure this script was written for.
REQUIRED=(
	bin/psh
	bin/busybox
	usr/bin/Xphoenix
	usr/bin/quakespasm
	usr/bin/vkquake
	usr/bin/yquake2
	usr/bin/quake3e
	usr/bin/supertuxkart
	usr/share/quake/id1/pak0.pak
	usr/share/quake/id1/config.cfg
	usr/share/quake2/baseq2/pak0.pak
	usr/share/quake3/demoq3/pak0.pk3
	usr/share/quake3/demoq3/pak1.pk3
	usr/share/quake3/demoq3/q3key
	usr/share/supertuxkart/data/stk_config.xml
)

# Expected but not fatal: launchers and conveniences. Reported, never silent.
OPTIONAL=(
	bin/xterm
	bin/wmaker
	bin/stk
	bin/ram-stage-play
	usr/bin/quake2
	usr/bin/quake3
	bin/python3
	bin/bash
	bin/nano
	bin/mc
)

missing_req=0
missing_opt=0

printf '== rootfs completeness: %s ==\n' "${root}"
for p in "${REQUIRED[@]}"; do
	if [ -s "${root}/${p}" ]; then
		printf '  OK    %s\n' "${p}"
	else
		printf '  ABSENT %s   <-- REQUIRED\n' "${p}"
		missing_req=$((missing_req + 1))
	fi
done
for p in "${OPTIONAL[@]}"; do
	if [ -s "${root}/${p}" ]; then
		printf '  ok    %s\n' "${p}"
	else
		printf '  absent %s   (optional)\n' "${p}"
		missing_opt=$((missing_opt + 1))
	fi
done

# stk-assets is a directory of ~149 MB, so check it is populated rather than
# naming one file inside it (which would rot).
if [ -d "${root}/usr/share/supertuxkart/stk-assets/karts" ] &&
	[ -n "$(ls -A "${root}/usr/share/supertuxkart/stk-assets/karts" 2>/dev/null)" ]; then
	printf '  OK    usr/share/supertuxkart/stk-assets/karts (populated)\n'
else
	printf '  ABSENT usr/share/supertuxkart/stk-assets/karts   <-- REQUIRED\n'
	missing_req=$((missing_req + 1))
fi

printf '\n'
if [ "${missing_req}" -gt 0 ]; then
	printf 'INCOMPLETE: %d required path(s) missing, %d optional.\n' "${missing_req}" "${missing_opt}"
	printf 'Game data missing? Run: ./scripts/stage-game-data.sh all   (local builds do NOT\n'
	printf 'run it -- only the Dockerfile does, so a stale rootfs-overlay persists silently.)\n'
	exit 1
fi
printf 'COMPLETE: all %d required paths present (%d optional missing).\n' "${#REQUIRED[@]}" "${missing_opt}"
exit 0
