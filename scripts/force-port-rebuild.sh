#!/usr/bin/env bash
#
# force-port-rebuild.sh — mark named phoenix-rtos-ports entries stale so the next
# `ports` stage CLEANS and genuinely recompiles them.
#
# Why this exists. port_manager's per-port build state
# (_build/<target>/.port_state/<name>-<ver>.json) tracks {use_flags, tests, deps,
# recipe-digest}. It does NOT track the libphoenix sysroot headers, so a libc
# header change (a new POSIX option macro, a new typedef, a changed struct) leaves
# every port's state valid and nothing rebuilds.
#
# ⚠ THE TRAP THIS SCRIPT EXISTS FOR: deleting the state json does NOT work, and
# fails *quietly*. port_manager only runs its clean step when the state has
# CHANGED ("Build state changed for X, cleaning"); with the state file simply
# ABSENT it goes straight to the build, `make` finds every .o newer than its
# source, and the port is merely RE-LINKED from stale objects. The log looks like
# a successful rebuild -- a "BUILD: python-3.14.4" line and a fresh binary
# timestamp -- while the change under test is not in it. Measured 2026-09-10:
# python relinked in 4 s with Python/thread.o eight hours old, and openssl never
# compiled rand_unix.c at all.
#
# So: keep the state file and POISON its recipe digest. port_manager then sees a
# real change, cleans the workdir, re-extracts, and rebuilds for real -- after
# which it writes the correct digest back, so this is self-healing and leaves no
# permanent staleness behind.
#
#   scripts/force-port-rebuild.sh openssl micropython redis python
#   ./scripts/rebuild-rpi4b-fast.sh --ports-only
#
# Names may be given with or without a version ("openssl" or "openssl-1.1.1w").
# A bare name that matches several versions poisons every match, which is what you
# want for a libc-contract change.
#
# HOW TO VERIFY IT WORKED, always: the rebuild log must contain a
# "Build state changed for <port>, cleaning" line for each port named here, AND a
# compile line for a file you expect to change. A "BUILD:" line alone is not
# evidence -- that is the trap above.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
state_dir="${buildroot}/_build/${target}/.port_state"

if [ "$#" -eq 0 ]; then
	echo "usage: $0 <port-name>[-<version>] [...]" >&2
	echo "       then: ./scripts/rebuild-rpi4b-fast.sh --ports-only" >&2
	exit 2
fi

[ -d "${state_dir}" ] || { echo "no port state dir: ${state_dir}" >&2; exit 1; }

poisoned=0
for want in "$@"; do
	found=0
	for f in "${state_dir}/${want}".json "${state_dir}/${want}"-*.json; do
		[ -f "$f" ] || continue
		# Rewrite only the recipe digest; every other key stays truthful so that a
		# genuine use-flag or dependency change is still detected independently.
		python3 - "$f" <<-'PY'
			import json, sys
			p = sys.argv[1]
			with open(p) as fh:
			    st = json.load(fh)
			st["recipe"] = "forced-rebuild-" + str(st.get("recipe", ""))[:16]
			with open(p, "w") as fh:
			    json.dump(st, fh)
		PY
		echo "poisoned $(basename "$f")"
		found=1
		poisoned=$((poisoned + 1))
	done
	[ "$found" = 1 ] || echo "WARNING: no state file matched '${want}' in ${state_dir}" >&2
done

if [ "${poisoned}" -eq 0 ]; then
	echo "nothing poisoned -- check the port names against: ls ${state_dir}" >&2
	exit 1
fi

echo
echo "${poisoned} port(s) marked stale. Now run:"
echo "    ./scripts/rebuild-rpi4b-fast.sh --ports-only"
echo "and CONFIRM a 'Build state changed for <port>, cleaning' line per port."
