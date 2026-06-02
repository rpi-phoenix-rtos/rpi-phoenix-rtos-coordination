#!/usr/bin/env bash
#
# git-consolidate-repo.sh <repo-subdir>
#
# Consolidate ONE Phoenix sibling repo's Pi4 work onto a local `master` that
# tracks the canonical upstream (origin = phoenix-rtos):
#   1. fetch origin
#   2. position on a local `master` (create from origin/master if absent)
#   3. merge LATEST origin/master into master (pull upstream)
#   4. merge the work branch into master (consolidate the Pi4 work)
#
# SAFE: any merge conflict triggers `git merge --abort` and a checkout back to
# the original work branch, then exits non-zero. Nothing is force-updated, no
# branch is deleted, nothing is pushed. The work branch is never modified, so a
# bad result is fully recoverable (the work branch still has everything).
#
# Usage: ./scripts/git-consolidate-repo.sh phoenix-rtos-kernel
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sub="${1:?usage: git-consolidate-repo.sh <repo-subdir>}"
d="$repo_root/sources/$sub"

if [ ! -d "$d/.git" ] && [ ! -f "$d/.git" ]; then
	echo "ERR: $d is not a git repo"
	exit 2
fi

g() { git -C "$d" "$@"; }

work="$(g rev-parse --abbrev-ref HEAD)"
echo "=== consolidate $sub (work branch: $work) ==="

if ! g remote | grep -qx origin; then
	echo "ERR: $sub has no 'origin' remote; skipping"
	exit 2
fi

echo "--- fetching origin ---"
g fetch origin --quiet

# Position on local master (create from origin/master if it doesn't exist).
if g show-ref --verify --quiet refs/heads/master; then
	g checkout --quiet master
else
	echo "--- creating local master from origin/master ---"
	g checkout --quiet -b master origin/master
fi

# Pull latest upstream into master.
echo "--- merge origin/master -> master ---"
if ! g merge --no-edit origin/master; then
	echo "CONFLICT: origin/master -> master. Aborting, restoring $work."
	g merge --abort || true
	g checkout --quiet "$work"
	exit 3
fi

# Merge the Pi4 work branch into master (unless work already IS master).
if [ "$work" != "master" ]; then
	echo "--- merge $work -> master ---"
	if ! g merge --no-edit "$work"; then
		echo "CONFLICT: $work -> master. Aborting, restoring $work."
		g merge --abort || true
		g checkout --quiet "$work"
		exit 4
	fi
fi

echo "=== $sub consolidated: master now at $(g rev-parse --short HEAD) ==="
echo "    (work branch '$work' kept intact)"
g --no-pager log --oneline -3
