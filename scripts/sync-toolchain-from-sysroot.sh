#!/usr/bin/env bash
#
# sync-toolchain-from-sysroot.sh — refresh the toolchain's bundled libc from the
# sysroot the build just produced.
#
# WHY THIS EXISTS
#
# Two kinds of binary in this project do NOT compile against
# .buildroot/_build/<target>/sysroot:
#
#   * the GPU archives — sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/
#     build-{v3d,gl,v3dv}-phoenix.py invoke the toolchain gcc with no --sysroot;
#   * the standalone driver/probe tools — rpi4-wifi, rpi4-hci, wifi-probe,
#     bt-probe, built by their own scripts "with the toolchain's default
#     libphoenix + CRT" (tools/wifi-probe/build.sh:7).
#
# Both therefore resolve libc out of .toolchain/aarch64-phoenix/aarch64-phoenix/
# {lib,usr/include}, a HAND-maintained bundle. Hand maintenance is exactly why it
# goes stale, and staleness here is silent:
#
#   * libs: the recorded "stale .toolchain libphoenix.a" footgun, observed as
#     CPython's `create_gil PyCOND_INIT failed`;
#   * headers: measured 2026-09-04 (docs/misc/2026-09-04-toolchain-header-skew.md)
#     — stdio.h was a 2026-07-23 snapshot, and five macro VALUES disagreed with
#     live libphoenix, two pairs SWAPPED (PTHREAD_PROCESS_PRIVATE/SHARED and
#     PTHREAD_PRIO_NONE/INHERIT). Code built against the snapshot asking for
#     priority inheritance would have silently got NOINHERIT;
#   * syscall numbers: TD-21 renumbers ~93 syscalls. Anything still linked
#     against the old bundled libphoenix.a invokes the WRONG syscall — which is
#     why TD-21 cannot be considered done until this sync has run and every
#     standalone tool has been rebuilt.
#
# The existing sync_toolchain_libc() in tools/x11-port/build-x11-phoenix.sh:317
# copies the three archives only, never the headers. This script does both, and
# is meant to be run right after the core stage of a build, so the bundle is a
# copy of a generated artifact rather than something a human curates.
#
# It does NOT make the bare-toolchain builds hermetic — pointing each of them at
# --sysroot is the real end-state (see the header-skew doc). It removes the
# staleness, which is the part that bites.
#
#   ./scripts/sync-toolchain-from-sysroot.sh            # sync libs + headers
#   ./scripts/sync-toolchain-from-sysroot.sh --check    # report drift, change nothing
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
SYSROOT="${PHOENIX_SYSROOT:-${ROOT}/.buildroot/_build/${TARGET}/sysroot}"
BUNDLE="${ROOT}/.toolchain/aarch64-phoenix/aarch64-phoenix"

check_only=0
[ "${1:-}" = "--check" ] && check_only=1

die() { printf 'sync-toolchain: %s\n' "$1" >&2; exit 2; }

[ -d "${SYSROOT}" ] || die "no sysroot at ${SYSROOT} — run the core stage first (--scope core)"
[ -d "${BUNDLE}" ] || die "no toolchain bundle at ${BUNDLE}"

# Judge by content, not by timestamp: a copy is fresh iff it is identical.
drift=0
report() { printf '  %-28s %s\n' "$1" "$2"; }

printf '== toolchain bundle vs %s ==\n' "${SYSROOT}"

for lib in libphoenix.a libc.a libm.a; do
	src="${SYSROOT}/lib/${lib}"
	dst="${BUNDLE}/lib/${lib}"
	if [ ! -f "${src}" ]; then
		report "${lib}" "absent from sysroot — skipped"
		continue
	fi
	if [ -f "${dst}" ] && cmp -s "${src}" "${dst}"; then
		report "${lib}" "identical"
	else
		report "${lib}" "DRIFTED"
		drift=1
		[ "${check_only}" -eq 1 ] || cp "${src}" "${dst}"
	fi
done

# Headers: the sysroot's include tree is the generated one. Copy the whole tree so
# a header DELETED upstream also disappears from the bundle -- a header that only
# still exists in the bundle is the same hazard as a stale one.
src_inc="${SYSROOT}/usr/include"
[ -d "${src_inc}" ] || src_inc="${SYSROOT}/include"
dst_inc="${BUNDLE}/usr/include"

if [ ! -d "${src_inc}" ]; then
	report "headers" "no include tree in sysroot — skipped"
else
	if diff -rq "${src_inc}" "${dst_inc}" >/dev/null 2>&1; then
		report "headers" "identical"
	else
		n=$(diff -rq "${src_inc}" "${dst_inc}" 2>/dev/null | wc -l)
		report "headers" "DRIFTED (${n} differing paths)"
		drift=1
		if [ "${check_only}" -eq 0 ]; then
			mkdir -p "${dst_inc}"
			# --delete so removals propagate; rsync keeps it a mirror of the artifact.
			rsync -a --delete "${src_inc}/" "${dst_inc}/" ||
				die "rsync of the header tree failed"
		fi
	fi
fi

printf '\n'
if [ "${check_only}" -eq 1 ]; then
	if [ "${drift}" -eq 0 ]; then
		printf 'RESULT: bundle matches the sysroot — bare-toolchain builds see current libc\n'
	else
		printf 'RESULT: bundle is STALE. Anything built without --sysroot (GPU archives,\n'
		printf '        standalone driver/probe tools) is compiling against old headers and\n'
		printf '        linking an old libphoenix.a. Re-run without --check.\n'
	fi
	exit "${drift}"
fi

if [ "${drift}" -eq 0 ]; then
	printf 'RESULT: already in sync; nothing copied\n'
else
	printf 'RESULT: bundle refreshed from the sysroot.\n'
	printf '        NOW REBUILD every bare-toolchain consumer, or it still holds the old\n'
	printf '        copy: the three GPU archives and rpi4-wifi / rpi4-hci / wifi-probe /\n'
	printf '        bt-probe. After a syscall renumber (TD-21) this is not optional.\n'
fi
