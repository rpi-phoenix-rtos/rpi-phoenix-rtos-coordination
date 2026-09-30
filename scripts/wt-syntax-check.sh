#!/usr/bin/env bash
# Like scripts/syntax-check.sh, but compiles a WORKTREE copy of the file instead of
# sources/<repo>/<rel>, without staging anything into .buildroot. The worktree file's
# own directory and repo root go first on the include path so its headers win.
# usage: wt-syntax-check.sh <repo> <worktree-root> <rel> [target]
set -euo pipefail
root=/home/houp/phoenix-rpi
repo=$1 wt=$2 rel=$3 target=${4:-aarch64a72-generic-rpi4b}
bdir=$root/.buildroot/$repo
obj=$root/.buildroot/_build/$target/$repo/${rel%.c}.o
proj=$root/.buildroot/_projects/$target
tc=$root/.toolchain/aarch64-phoenix/bin
if [ -f "$proj/build.project" ]; then
  while IFS= read -r kv; do export "${kv?}"; done < <(grep -E '^export [A-Z_][A-Z0-9_]*=[^$" ]*$' "$proj/build.project" | sed -e 's/^export //')
fi
cmd=$(cd "$bdir" && PATH="$tc:$PATH" TARGET="$target" make -n -W "$rel" "$obj" 2>/dev/null | grep -- '-phoenix-gcc ' | grep -F -- "$(basename "$rel")" || true)
[ "$(printf '%s' "$cmd" | grep -c .)" -eq 1 ] || { echo "no unique compile cmd for $rel" >&2; printf '%s\n' "$cmd" >&2; exit 2; }
cmd=$(printf '%s' "$cmd" | sed -E 's/ -c / /; s/ -o "[^"]*"//g; s/ -o [^ ]+//g; s/ -M(M)?D\b//g; s/ -MF [^ ]+//g; s/ -MT [^ ]+//g; s/ -MP\b//g')
# swap the source path for the worktree copy
src_b="$bdir/$rel"; src_w="$wt/$rel"
cmd=${cmd//\"$src_b\"/\"$src_w\"}
cmd=${cmd// $src_b / $src_w }
printf '%s' "$cmd" | grep -qF "$src_w" || { echo "could not substitute worktree source" >&2; exit 2; }
cmd=$(printf '%s' "$cmd" | sed -E "s#-phoenix-gcc #-phoenix-gcc -I$wt/$(dirname "$rel") -I$wt #")
echo "cmd: $cmd" | cut -c1-400
(cd "$bdir" && PATH="$tc:$PATH" eval "$cmd -c -o /dev/null -I$proj ${EXTRA:-}") && echo "WT-SYNTAX: CLEAN $repo/$rel"
