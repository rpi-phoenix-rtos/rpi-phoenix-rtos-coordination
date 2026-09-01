#!/usr/bin/env bash
#
# upstream-status.sh — READ-ONLY "how far behind upstream are we?" digest.
#
# Fetches `origin` (canonical phoenix-rtos) for every sibling repo and reports,
# per repo: commits we are BEHIND upstream, commits we are AHEAD (our Pi4 work),
# and the subjects of the new upstream commits. Merges nothing, pushes nothing.
#
# Companion to git-pull-upstream-all.sh (which does the actual merge). Run this
# FIRST so the weekly upstream sync can summarise what landed upstream before
# deciding what to merge.
#
# Usage: ./scripts/upstream-status.sh [--subjects N]   (default N=10)
set -uo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

nsub=10
[ "${1:-}" = "--subjects" ] && nsub="${2:-10}"

total_behind=0
behind_repos=()

printf '%-28s %8s %8s  %s\n' "REPO" "BEHIND" "AHEAD" "UPSTREAM BRANCH"
printf '%s\n' "----------------------------------------------------------------------"

for d in sources/*/; do
	sub="$(basename "$d")"
	[ -e "$d/.git" ] || continue
	git -C "$d" remote | grep -qx origin || { printf '%-28s %8s\n' "$sub" "(no origin)"; continue; }

	git -C "$d" fetch --quiet origin 2>/dev/null || { printf '%-28s %8s\n' "$sub" "(fetch failed)"; continue; }

	# pick upstream's default branch: master, else main
	up=""
	for b in master main; do
		git -C "$d" rev-parse --verify --quiet "origin/$b" >/dev/null && { up="origin/$b"; break; }
	done
	[ -n "$up" ] || { printf '%-28s %8s\n' "$sub" "(no master/main)"; continue; }

	behind=$(git -C "$d" rev-list --count "HEAD..$up" 2>/dev/null || echo '?')
	ahead=$(git -C "$d" rev-list --count "$up..HEAD" 2>/dev/null || echo '?')
	printf '%-28s %8s %8s  %s\n' "$sub" "$behind" "$ahead" "$up"

	if [ "$behind" != "0" ] && [ "$behind" != "?" ]; then
		behind_repos+=("$sub:$behind")
		total_behind=$((total_behind + behind))
		git -C "$d" log --no-merges --format='      + %h %s' -n "$nsub" "HEAD..$up" 2>/dev/null
		[ "$behind" -gt "$nsub" ] && echo "      ... $((behind - nsub)) more"
	fi
done

echo ""
echo "SUMMARY: $total_behind new upstream commit(s) across ${#behind_repos[@]} repo(s)"
[ ${#behind_repos[@]} -gt 0 ] && printf '  behind: %s\n' "${behind_repos[*]}"
echo ""
echo "To merge: ./scripts/git-pull-upstream-all.sh  (then REBUILD --scope core + boot-test"
echo "before pushing; lwip must use the scrubbed/filtered publish flow, never a plain push)."
