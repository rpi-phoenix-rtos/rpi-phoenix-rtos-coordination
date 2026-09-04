#!/usr/bin/env bash
#
# check-no-stale-binaries.sh — find ELF binaries in a target root that were linked
# against an OLDER libphoenix than the one currently built.
#
# WHY: some changes make every binary's ABI contract shift at once. The syscall-table
# revert (TD-21) renumbers ~93 syscalls, so a binary left over from before it does not
# fail to load -- it calls the WRONG syscall and misbehaves silently. The same applies to
# any libphoenix struct/ABI change. TD-21's own resolution requirement is "rebuild
# EVERYTHING ... a partial rebuild would leave stale binaries calling the wrong syscall",
# and this script is how that is checked instead of assumed.
#
# The reference is libphoenix.a itself, because that is what defines the contract: any
# ELF older than the current libphoenix.a was linked against a previous one.
#
#   ./scripts/check-no-stale-binaries.sh                       # live NFS export
#   ./scripts/check-no-stale-binaries.sh --root .buildroot/_fs/<target>/root
#   ./scripts/check-no-stale-binaries.sh --ref <file>           # custom reference
#
# Exit 0 when every ELF is at least as new as the reference; 1 when any is older; 2 on
# a usage/setup problem.
#
# TWO CAVEATS, both learned the hard way on 2026-09-04:
#
#  * DO NOT run this mid-build. Binaries built in the PROJECT stage (nfs-fs -> /sbin/nfs
#    is the one that matters) do not exist until late, so a mid-flight run reports them
#    as unproduced and you conclude something alarming and wrong. Wait for the build.
#  * A flagged file is not automatically a defect: it is a binary nothing in the build
#    produced, i.e. hand-staged. Those are real (the radio tools and X11 helpers are
#    built by their own scripts against the toolchain bundle, not the sysroot) and they
#    are exactly what has to be rebuilt -- but check WHERE each comes from before
#    calling it stale.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

ROOT_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"

# Default root: the export the Pi actually mounts (fsid=0), not a hardcoded name --
# see feedback on staging into a dead export.
fsid0="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports /etc/exports.d/*.exports 2>/dev/null || true)"
root="${fsid0:-/srv/phoenix-rpi4-nfs}"
ref="${ROOT_REPO}/.buildroot/_build/${TARGET}/sysroot/lib/libphoenix.a"

while [ "$#" -gt 0 ]; do
	case "$1" in
	--root) root="${2:?}"; shift 2 ;;
	--ref)  ref="${2:?}"; shift 2 ;;
	-h|--help) sed -n '2,32p' "${BASH_SOURCE[0]}"; exit 0 ;;
	*) printf 'check-no-stale-binaries: unknown argument %s\n' "$1" >&2; exit 2 ;;
	esac
done

[ -d "${root}" ] || { printf 'check-no-stale-binaries: no such root: %s\n' "${root}" >&2; exit 2; }
[ -f "${ref}" ] || { printf 'check-no-stale-binaries: no reference libphoenix.a at %s\n       (run the core stage first, or pass --ref)\n' "${ref}" >&2; exit 2; }

printf '== ELF sweep of %s ==\n   reference: %s (%s)\n\n' "${root}" "${ref}" "$(date -r "${ref}" '+%Y-%m-%d %H:%M:%S')"

# Judge by ELF magic, not by path or permission bits: scripts and data must not be
# flagged, and `cp -a` legitimately preserves an old mtime on non-ELF payload (game
# data, fonts, configs), which is why only ELFs are considered here.
python3 - "$root" "$ref" <<'PY'
import os, sys, datetime
root, ref = sys.argv[1], sys.argv[2]
cutoff = os.path.getmtime(ref)
total = 0
stale = []
for dp, _, fs in os.walk(root):
    for f in fs:
        p = os.path.join(dp, f)
        try:
            if not os.path.isfile(p) or os.path.islink(p):
                continue
            with open(p, 'rb') as fh:
                if fh.read(4) != b'\x7fELF':
                    continue
            total += 1
            if os.path.getmtime(p) < cutoff:
                stale.append(p)
        except OSError:
            pass
print(f'ELF binaries: {total}')
print(f'older than the reference: {len(stale)}')
for p in sorted(stale):
    ts = datetime.datetime.fromtimestamp(os.path.getmtime(p)).strftime('%Y-%m-%d %H:%M')
    print(f'  {ts}  {p[len(root):]}')
sys.exit(1 if stale else 0)
PY
rc=$?

printf '\n'
if [ "${rc}" -eq 0 ]; then
	printf 'RESULT: every ELF is at least as new as libphoenix.a — no stale-ABI binary\n'
else
	printf 'RESULT: the binaries listed above predate the current libphoenix and were linked\n'
	printf '        against an older ABI. After a syscall renumber they invoke the WRONG\n'
	printf '        syscall, silently. Rebuild each one (the radio tools and X11 helpers\n'
	printf '        have their own build scripts) and re-run.\n'
fi
exit "${rc}"
