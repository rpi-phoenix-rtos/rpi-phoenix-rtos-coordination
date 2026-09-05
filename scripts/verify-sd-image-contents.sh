#!/usr/bin/env bash
# verify-sd-image-contents.sh <image.img> — gate a built SD image's CONTENTS
# before anyone flashes it.
#
# Distinct from scripts/verify-rpi4b-sdimg.sh, which checks image INTEGRITY
# (sha256 + size against the .meta.txt, i.e. "did it copy correctly"). This one
# asks "is the right CODE inside", which integrity cannot tell you: a perfectly
# intact image can be built from a commit that was reverted an hour later.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
#
# Answers three questions that have each already shipped a bad artifact:
#   1. is every binary and every game's DATA actually in the rootfs?
#   2. does it contain the fixes it is supposed to (positive markers)?
#   3. does it contain something we deliberately reverted (negative markers)?
#
# (3) is the one that matters most: the 2026-09-03 clean image was green on (1)
# and (2) and still shipped a yQuake2 that wedged the GPU on nearly every frame,
# because it cloned a fork commit that was reverted an hour later.
set -uo pipefail
IMG="${1:?usage: verify-sd-image.sh <image.img>}"
[ -f "$IMG" ] || { echo "no such image: $IMG" >&2; exit 2; }
command -v debugfs >/dev/null || { echo "need e2fsprogs (debugfs)" >&2; exit 2; }

start=$(partx -g -o START -n 2 "$IMG" 2>/dev/null | tr -d ' ')
[ -n "$start" ] || { echo "cannot read partition 2 start" >&2; exit 2; }
E2="$IMG?offset=$((start * 512))"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
rc=0

dump() { rm -f "$TMP/x"; debugfs -R "dump /$1 $TMP/x" "$E2" >/dev/null 2>&1; [ -s "$TMP/x" ]; }

# Count occurrences of a string in the last dumped file.
#
# Deliberately grep -c, never grep -q: under `set -o pipefail` a `grep -q` exits
# on its FIRST match, `strings` then dies of SIGPIPE (141), and pipefail makes the
# pipeline fail -- so a successful match reads as "absent". That inverted BOTH
# marker checks in the first version of this script and made a bad image look
# clean. grep -c drains the pipe, so the pipeline exits 0.
marker_count() { strings "$TMP/x" | grep -c -- "$1" || true; }

echo "== required paths =="
# bin/wmsetbg: wmaker EXECS it to paint the root window (src/misc.c:953). Without
#   it the GPU desktop is a black screen with a live cursor, which was reported as
#   "no wmaker running" on 2026-09-04 when in fact the session was healthy.
# bin/fbprobe: the framebuffer channel-order probe. Cheap to ship, and the one
#   tool that settles an RGB-vs-BGR argument by looking at the screen.
for p in usr/bin/quakespasm usr/bin/yquake2 usr/bin/quake3e usr/bin/vkquake \
         usr/bin/supertuxkart bin/psh bin/python3 bin/bash bin/nano bin/mc \
         usr/bin/Xphoenix usr/share/quake/id1/pak0.pak usr/share/quake2/baseq2/pak0.pak \
         usr/share/quake3/demoq3/pak0.pk3 usr/share/quake3/demoq3/pak1.pk3 \
         usr/share/quake3/demoq3/q3key \
         bin/wmsetbg bin/fbprobe; do
	if dump "$p"; then printf '  OK   %-40s %s\n' "$p" "$(stat -c%s "$TMP/x")"
	else printf '  MISS %s\n' "$p"; rc=1; fi
done

echo "== build provenance =="
# /etc/build-versions names the exact commit of every Phoenix repo that went into
# this image (coordination scripts/gen-build-versions.sh -> printed at boot by
# rpi4-sysinfo). An image without it can still boot, but every log it produces is
# ambiguous about what it was built from -- which is the problem the owner asked
# us to remove on 2026-09-05. Require a plausible list, not merely the file.
if dump etc/build-versions; then
	bv_repos="$(grep -avc '^#' "$TMP/x" || true)"
	if [ "${bv_repos:-0}" -ge 10 ]; then
		printf '  OK   %-40s %s repos\n' "etc/build-versions" "${bv_repos}"
		# A build from a modified tree is exactly where a bare sha misleads, so
		# say it out loud rather than letting it pass silently.
		bv_dirty="$(grep -ac '+dirty' "$TMP/x" || true)"
		[ "${bv_dirty:-0}" -eq 0 ] ||
			printf '  WARN %s repo(s) were DIRTY at build time (see etc/build-versions)\n' "${bv_dirty}"
	else
		printf '  FAIL etc/build-versions lists only %s repos (expected >= 10)\n' "${bv_repos}"; rc=1
	fi
else
	echo "  MISS etc/build-versions — the image cannot say which commits built it"; rc=1
fi

echo "== positive markers (fixes that must be present) =="
# The V3D submit mutex: its failure fprintf string is unique to the fixed driver.
if dump usr/bin/vkquake && [ "$(marker_count 'submits UNSERIALIZED')" -gt 0 ]; then
	echo "  OK   v3d submit mutex present in vkquake"
else
	echo "  MISS v3d submit mutex marker absent from vkquake"; rc=1
fi

echo "== negative markers (reverted code that must be ABSENT) =="
# gl3_discardfb: the pre-swap Z/S discard, reverted 2026-09-03 (fork d5413235).
# The cvar NAME string only exists in the binary if the code does.
if dump usr/bin/yquake2; then
	if [ "$(marker_count gl3_discardfb)" -gt 0 ]; then
		echo "  FAIL yquake2 still contains gl3_discardfb (the reverted GPU-wedging discard)"; rc=1
	else
		echo "  OK   yquake2 free of gl3_discardfb"
	fi
else
	echo "  MISS yquake2 not in image"; rc=1
fi

echo
[ "$rc" -eq 0 ] && echo "RESULT: image PASSES — safe to flash" || echo "RESULT: image FAILS — do not flash"
exit "$rc"
