#!/usr/bin/env bash
#
# Generate a manifests/ entry that records the current SHA of every sibling
# repository under sources/. The generated manifest is machine-parseable by
# restore-integration-state.sh: the `integration-state-v1` fenced block lists
# one `<repo>\t<sha>` line per sibling.
#
# Usage:
#   scripts/snapshot-integration-state.sh <slug> [--note "short description"]
#
# Example:
#   scripts/snapshot-integration-state.sh pi4-program-reloc-entry \
#     --note "Map relocation complete, stuck at marker o"

set -euo pipefail

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

slug="${1:-}"
[ -n "$slug" ] || die "usage: $0 <slug> [--note \"description\"]"
shift

note=""
while [ $# -gt 0 ]; do
	case "$1" in
		--note) note="${2:?--note requires a value}"; shift 2 ;;
		*) die "unknown option: $1" ;;
	esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
# Resolve the coordination repo (the worktree may be under .claude/worktrees/)
# Manifests must always land in the non-worktree coordination tree so they are
# visible to every active session.
coord_root="${PHOENIX_COORD_ROOT:-${repo_root}}"
sources_dir="${PHOENIX_SOURCES_DIR:-${coord_root}/sources}"
manifests_dir="${coord_root}/manifests"

[ -d "$sources_dir" ] || die "sources directory not found: $sources_dir"
[ -d "$manifests_dir" ] || die "manifests directory not found: $manifests_dir"

date_iso="$(date -u +%Y-%m-%d)"
# If the slug already starts with a YYYY-MM-DD- prefix, don't re-prepend it.
case "$slug" in
	[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]-*) out="${manifests_dir}/${slug}.md" ;;
	*) out="${manifests_dir}/${date_iso}-${slug}.md" ;;
esac
[ -e "$out" ] && die "manifest already exists: $out"

{
	printf '# Integration State: %s\n\n' "$slug"
	printf '## Summary\n\n'
	printf -- '- Date: %s\n' "$date_iso"
	printf -- '- Note: %s\n' "${note:-(none)}"
	printf -- '- Generator: scripts/snapshot-integration-state.sh\n\n'

	printf '## Repositories\n\n'
	printf '| Repository | Branch | Commit SHA | Remote |\n'
	printf '| --- | --- | --- | --- |\n'

	# Machine-parseable block accumulates in a tempfile so we can emit the
	# human table above it and the parseable block after it in one pass.
	tmp="$(mktemp)"
	trap 'rm -f "$tmp"' EXIT

	for d in "$sources_dir"/*/; do
		name="$(basename "$d")"
		pushd "$d" >/dev/null
		branch="$(git branch --show-current 2>/dev/null || true)"
		[ -n "$branch" ] || branch="(detached)"
		sha="$(git rev-parse HEAD 2>/dev/null || echo UNKNOWN)"
		short="$(git rev-parse --short HEAD 2>/dev/null || echo UNKNOWN)"
		remote="$(git remote get-url origin 2>/dev/null || echo '(no origin)')"
		dirty="$(git status --porcelain 2>/dev/null | wc -l | tr -d ' ')"
		popd >/dev/null

		status="clean"
		[ "$dirty" != "0" ] && status="dirty($dirty)"

		printf '| %s | %s | %s | %s |\n' "$name" "$branch" "$short ($status)" "$remote"
		printf '%s\t%s\t%s\n' "$name" "$sha" "$branch" >> "$tmp"
	done

	printf '\n## Machine-Parseable State\n\n'
	printf 'Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\\t<sha>\\t<branch>`.\n\n'
	printf '```integration-state-v1\n'
	cat "$tmp"
	printf '```\n'
} > "$out"

printf 'Wrote %s\n' "$out"
