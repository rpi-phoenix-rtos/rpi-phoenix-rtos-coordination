#!/usr/bin/env bash
#
# git-pull-upstream-all.sh
#
# Track canonical upstream (origin = phoenix-rtos) across ALL sibling repos.
# For each sources/<repo> that has an `origin` remote, runs
# git-consolidate-repo.sh, which fetches origin and merges origin/master into
# the local master (and, the first time, also folds the old Pi4 work branch in).
#
# Now that every sibling is on `master`, the steady-state behaviour is simply:
# fetch origin + merge origin/master -> master. Any repo whose merge conflicts
# is left untouched (the helper aborts non-destructively) and reported at the
# end for manual resolution. Nothing is pushed; nothing is force-updated.
#
# Intended cadence: run periodically to pull upstream as it lands. Resolve any
# reported conflicts by hand, then rebuild + boot-test before relying on it.
#
# Usage: ./scripts/git-pull-upstream-all.sh
set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

ok=()
conflict=()
skipped=()

for d in sources/*/; do
	sub="$(basename "$d")"
	if [ ! -e "$d/.git" ]; then
		continue
	fi
	if ! git -C "$d" remote | grep -qx origin; then
		skipped+=("$sub (no origin remote)")
		continue
	fi
	echo "############################################################"
	if ./scripts/git-consolidate-repo.sh "$sub"; then
		ok+=("$sub")
	else
		conflict+=("$sub")
	fi
done

echo ""
echo "################## upstream-pull summary ###################"
echo "OK (${#ok[@]}): ${ok[*]:-none}"
echo "SKIPPED (${#skipped[@]}): ${skipped[*]:-none}"
echo "CONFLICTS (${#conflict[@]}): ${conflict[*]:-none}"
if [ "${#conflict[@]}" -gt 0 ]; then
	echo ""
	echo "Resolve each conflicted repo by hand:"
	echo "  git -C sources/<repo> checkout master"
	echo "  git -C sources/<repo> merge origin/master   # then fix conflicts, commit"
	echo "  git -C sources/<repo> merge <work-branch>   # if first-time consolidation"
	echo "Then: ./scripts/rebuild-rpi4b-fast.sh && ./scripts/test-cycle-netboot.sh --label upstream-pull"
	exit 3
fi
echo ""
echo "All repos tracked clean. Next: rebuild + boot-test before relying on it:"
echo "  ./scripts/rebuild-rpi4b-fast.sh && ./scripts/test-cycle-netboot.sh --timestamp --label upstream-pull"
