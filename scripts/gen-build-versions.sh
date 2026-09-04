#!/usr/bin/env bash
#
# gen-build-versions.sh — record WHICH COMMIT of every Phoenix repo went into
# this build, as a file the target prints at boot.
#
# Owner request 2026-09-05: "a simple program that would report during boot which
# exact git commit ids were used to build specific phoenix components". Without
# it, a UART log says what the system DID but not what it WAS, and every triage
# starts by guessing which tree produced the binaries.
#
# The siblings under sources/ are SEPARATE repos, not submodules, so
# `git submodule status` (what build.sh records in /etc/git-version) does not
# describe them. This walks each sibling plus the coordination repo, the way
# snapshot-integration-state.sh does for manifests, and writes one line per repo:
#
#     <repo> <short-sha><+dirty> <committer-date>
#
# The dirty marker matters more than the sha: a build from a modified tree is
# exactly the case where a bare commit id misleads.
#
#   ./scripts/gen-build-versions.sh [output-path]
#       default output: .buildroot/_fs/<target>/root/etc/build-versions
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
out="${1:-${buildroot}/_fs/${target}/root/etc/build-versions}"

repo_line() {
	local path="$1" name="$2" sha date dirty=""
	[ -d "${path}/.git" ] || return 0
	sha="$(git -C "${path}" rev-parse --short=12 HEAD 2>/dev/null)" || return 0
	date="$(git -C "${path}" log -1 --format=%cs 2>/dev/null)"
	# A modified working tree means the binaries do NOT correspond to the sha.
	[ -n "$(git -C "${path}" status --porcelain 2>/dev/null)" ] && dirty="+dirty"
	printf '%-26s %s%-6s %s\n' "${name}" "${sha}" "${dirty}" "${date}"
}

mkdir -p "$(dirname "${out}")" || {
	printf 'gen-build-versions: cannot create %s\n' "$(dirname "${out}")" >&2
	exit 1
}

{
	printf '# Phoenix-RTOS RPi4 build components — generated %s for %s\n' \
		"$(date '+%Y-%m-%d %H:%M:%S %Z')" "${target}"
	repo_line "${repo_root}" "coordination"
	for d in "${repo_root}"/sources/*/; do
		repo_line "${d%/}" "$(basename "${d%/}")"
	done
} >"${out}"

printf 'gen-build-versions: wrote %s (%s repos)\n' "${out}" \
	"$(grep -avc '^#' "${out}")"
