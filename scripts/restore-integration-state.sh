#!/usr/bin/env bash
#
# Restore every sibling repository under sources/ to the SHAs recorded in an
# integration-state manifest written by snapshot-integration-state.sh.
#
# Usage:
#   scripts/restore-integration-state.sh <manifest.md> [--dry-run] [--force]
#
# By default the script refuses to touch a repo that has uncommitted changes.
# Use --force to override; the override is loud and may destroy local edits.

set -euo pipefail

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
warn() { printf 'warn: %s\n' "$*" >&2; }
info() { printf '%s\n' "$*"; }

manifest="${1:-}"
[ -n "$manifest" ] || die "usage: $0 <manifest.md> [--dry-run] [--force]"
[ -f "$manifest" ] || die "manifest not found: $manifest"
shift

dry_run=0
force=0
while [ $# -gt 0 ]; do
	case "$1" in
		--dry-run) dry_run=1; shift ;;
		--force) force=1; shift ;;
		*) die "unknown option: $1" ;;
	esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
coord_root="${PHOENIX_COORD_ROOT:-$(cd "${script_dir}/.." && pwd)}"
sources_dir="${PHOENIX_SOURCES_DIR:-${coord_root}/sources}"
[ -d "$sources_dir" ] || die "sources directory not found: $sources_dir"

# Extract the machine-parseable block. Uses awk so the script stays dependency-free.
block="$(awk '
	/^```integration-state-v1/ { in_block=1; next }
	/^```/ && in_block { in_block=0; exit }
	in_block { print }
' "$manifest")"

[ -n "$block" ] || die "manifest has no integration-state-v1 block: $manifest"

info "Parsed manifest: $manifest"
printf -- '----\n'

# Pass 1: dirty-check every repo before we touch anything.
dirty_repos=()
while IFS=$'\t' read -r repo sha branch; do
	[ -n "$repo" ] || continue
	repo_dir="${sources_dir}/${repo}"
	if [ ! -d "$repo_dir" ]; then
		die "repo missing on disk: $repo_dir"
	fi
	pushd "$repo_dir" >/dev/null
	n="$(git status --porcelain 2>/dev/null | wc -l | tr -d ' ')"
	popd >/dev/null
	[ "$n" != "0" ] && dirty_repos+=("$repo ($n dirty entries)")
done <<< "$block"

if [ "${#dirty_repos[@]}" -gt 0 ] && [ "$force" -ne 1 ]; then
	printf 'refusing to restore: the following repos are dirty:\n' >&2
	printf '  - %s\n' "${dirty_repos[@]}" >&2
	printf 'commit, stash, or re-run with --force to override.\n' >&2
	exit 1
fi

# Pass 2: perform the checkouts.
while IFS=$'\t' read -r repo sha branch; do
	[ -n "$repo" ] || continue
	repo_dir="${sources_dir}/${repo}"
	pushd "$repo_dir" >/dev/null
	current="$(git rev-parse HEAD)"
	if [ "$current" = "$sha" ]; then
		info "$repo: already at $sha"
	else
		if [ "$dry_run" -eq 1 ]; then
			info "$repo: would checkout $sha (currently $current)"
		else
			info "$repo: checkout $sha (from $current)"
			git checkout --quiet "$sha"
		fi
	fi
	popd >/dev/null
done <<< "$block"

printf -- '----\n'
if [ "$dry_run" -eq 1 ]; then
	info "dry run complete; no repos were modified."
else
	info "restore complete."
	info "note: sibling repos are now in detached-HEAD state at the recorded SHAs."
	info "      create a working branch in each repo before resuming edits."
fi
