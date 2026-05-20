#!/usr/bin/env bash
# Read-only git operations across Phoenix-RTOS coord + sibling repos.
#
# Uses `git -C <dir>` (no `cd`, no hook injection risk) so the entire
# script is statically allowlistable.
#
# Sibling roots:
#   coordination: the repo containing this script (override with $PHOENIX_COORD)
#   sources/*:    auto-discovered from <coord>/sources/
#
# Cross-repo iteration subcommands:
#   ./scripts/git-siblings.sh status               # one line per repo: status + ahead/behind
#   ./scripts/git-siblings.sh log [N]              # one-line log of last N commits, per repo (default 10)
#   ./scripts/git-siblings.sh branch               # branch + tracking, per repo
#   ./scripts/git-siblings.sh head                 # latest commit oneline + date, per repo
#   ./scripts/git-siblings.sh diff [REF]           # diff --stat vs REF (default HEAD), per repo
#   ./scripts/git-siblings.sh show <SHA>           # find which repo has <SHA>, show it
#
# Single-repo passthrough (read-only git subcommands only):
#   ./scripts/git-siblings.sh in <repo> log <args...>
#   ./scripts/git-siblings.sh in <repo> show <args...>
#   ./scripts/git-siblings.sh in <repo> diff <args...>
#   ./scripts/git-siblings.sh in <repo> blame <args...>
#   ./scripts/git-siblings.sh in <repo> status <args...>
#   ./scripts/git-siblings.sh in <repo> branch <args...>
#   ./scripts/git-siblings.sh in <repo> tag <args...>
#   ./scripts/git-siblings.sh in <repo> remote <args...>
#   ./scripts/git-siblings.sh in <repo> ls-files <args...>
#   ./scripts/git-siblings.sh in <repo> ls-remote <args...>
#   ./scripts/git-siblings.sh in <repo> rev-parse <args...>
#   ./scripts/git-siblings.sh in <repo> describe <args...>
#   ./scripts/git-siblings.sh in <repo> reflog <args...>
#   ./scripts/git-siblings.sh in <repo> shortlog <args...>
#   ./scripts/git-siblings.sh in <repo> cat-file <args...>
#   ./scripts/git-siblings.sh in <repo> for-each-ref <args...>
#   ./scripts/git-siblings.sh in <repo> worktree list
#   ./scripts/git-siblings.sh in <repo> stash list
#
# Examples:
#   git-siblings.sh in phoenix-rtos-kernel log --oneline -20
#   git-siblings.sh in phoenix-rtos-kernel log --oneline 2df7ac16..f2b7c62f
#   git-siblings.sh in phoenix-rtos-kernel show 3d5c9574 -- proc/process.c
#   git-siblings.sh in phoenix-rtos-kernel show --stat 3d5c9574 3b63677f f2b7c62f
#   git-siblings.sh in plo diff master..HEAD -- hal/
#
# <repo> can be:
#   coord (or .)                     for the coordination repo at $COORD
#   <name>                           short name under sources/ (e.g. phoenix-rtos-kernel)
#   sources/<name>                   full sources-relative path

set -u
set -o pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COORD="${PHOENIX_COORD:-$(cd "${script_dir}/.." && pwd)}"
SRC_ROOT="$COORD/sources"

usage() {
    cat <<EOF >&2
Usage: $0 <subcommand> [args]
  status | log [N] | branch | head | diff [REF] | show <SHA>
  in <repo> <safe-git-subcmd> [git-args...]
EOF
    exit 1
}

[ $# -ge 1 ] || usage
sub="$1"
shift || true

# Resolve list of all repos for cross-repo subcommands.
all_repos=()
if [ -d "$COORD/.git" ] || [ -f "$COORD/.git" ]; then
    all_repos+=("$COORD")
fi
if [ -d "$SRC_ROOT" ]; then
    for d in "$SRC_ROOT"/*; do
        if [ -d "$d/.git" ] || [ -f "$d/.git" ]; then
            all_repos+=("$d")
        fi
    done
fi

if [ ${#all_repos[@]} -eq 0 ]; then
    echo "git-siblings: no git repos under $COORD or $SRC_ROOT" >&2
    exit 2
fi

# Resolve a <repo> argument to an absolute path.
resolve_repo() {
    local name="$1"
    case "$name" in
        coord|.|"")
            echo "$COORD"
            return 0
            ;;
        /*)
            if [ -d "$name/.git" ] || [ -f "$name/.git" ]; then
                echo "$name"
                return 0
            fi
            ;;
        sources/*)
            local p="$COORD/$name"
            if [ -d "$p/.git" ] || [ -f "$p/.git" ]; then
                echo "$p"
                return 0
            fi
            ;;
        *)
            local p="$SRC_ROOT/$name"
            if [ -d "$p/.git" ] || [ -f "$p/.git" ]; then
                echo "$p"
                return 0
            fi
            ;;
    esac
    echo "git-siblings: unknown repo: $name" >&2
    return 2
}

case "$sub" in
    status)
        for r in "${all_repos[@]}"; do
            short=$(printf '%s' "$r" | sed -E "s|^$COORD/?||")
            [ -z "$short" ] && short="(coord)"
            br=$(git -C "$r" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "?")
            s=$(git -C "$r" status --short 2>/dev/null | wc -l | tr -d ' ')
            ahead=$(git -C "$r" rev-list --count "@{upstream}..HEAD" 2>/dev/null || echo "?")
            behind=$(git -C "$r" rev-list --count "HEAD..@{upstream}" 2>/dev/null || echo "?")
            printf '%-40s br=%-40s changes=%s ahead=%s behind=%s\n' "$short" "$br" "$s" "$ahead" "$behind"
        done
        ;;
    log)
        n="${1:-10}"
        for r in "${all_repos[@]}"; do
            short=$(printf '%s' "$r" | sed -E "s|^$COORD/?||")
            [ -z "$short" ] && short="(coord)"
            echo "=== $short ==="
            git -C "$r" log --oneline -n "$n" 2>/dev/null || echo "(no history)"
            echo
        done
        ;;
    branch)
        for r in "${all_repos[@]}"; do
            short=$(printf '%s' "$r" | sed -E "s|^$COORD/?||")
            [ -z "$short" ] && short="(coord)"
            br=$(git -C "$r" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "?")
            up=$(git -C "$r" rev-parse --abbrev-ref --symbolic-full-name "@{upstream}" 2>/dev/null || echo "(no upstream)")
            printf '%-40s br=%-40s upstream=%s\n' "$short" "$br" "$up"
        done
        ;;
    head)
        for r in "${all_repos[@]}"; do
            short=$(printf '%s' "$r" | sed -E "s|^$COORD/?||")
            [ -z "$short" ] && short="(coord)"
            line=$(git -C "$r" log -1 --pretty='%h %ad %s' --date=short 2>/dev/null || echo "(no history)")
            printf '%-40s %s\n' "$short" "$line"
        done
        ;;
    diff)
        ref="${1:-HEAD}"
        for r in "${all_repos[@]}"; do
            short=$(printf '%s' "$r" | sed -E "s|^$COORD/?||")
            [ -z "$short" ] && short="(coord)"
            stat=$(git -C "$r" diff --stat "$ref" 2>/dev/null)
            if [ -n "$stat" ]; then
                echo "=== $short ==="
                echo "$stat"
                echo
            fi
        done
        ;;
    show)
        sha="${1:-}"
        [ -n "$sha" ] || { echo "git-siblings: show requires <SHA>" >&2; exit 1; }
        for r in "${all_repos[@]}"; do
            if git -C "$r" cat-file -e "$sha^{commit}" 2>/dev/null; then
                short=$(printf '%s' "$r" | sed -E "s|^$COORD/?||")
                [ -z "$short" ] && short="(coord)"
                echo "=== $short ==="
                git -C "$r" show --stat "$sha"
                exit 0
            fi
        done
        echo "git-siblings: SHA $sha not found in any repo" >&2
        exit 2
        ;;
    in)
        # Single-repo passthrough, restricted to read-only git subcommands.
        [ $# -ge 2 ] || { echo "git-siblings in: usage: in <repo> <git-subcmd> [args...]" >&2; exit 1; }
        repo_name="$1"
        git_sub="$2"
        shift 2

        case "$git_sub" in
            log|show|diff|blame|status|branch|tag|remote|\
            ls-files|ls-remote|rev-parse|describe|reflog|shortlog|\
            cat-file|for-each-ref|stash|worktree|config|grep)
                ;;
            *)
                echo "git-siblings in: refusing non-readonly subcommand: $git_sub" >&2
                exit 1
                ;;
        esac

        # For stash, worktree, config: only allow their list-style invocations.
        case "$git_sub" in
            stash)
                if [ $# -lt 1 ] || [ "$1" != "list" ]; then
                    echo "git-siblings in: only 'stash list' is permitted" >&2
                    exit 1
                fi
                ;;
            worktree)
                if [ $# -lt 1 ] || [ "$1" != "list" ]; then
                    echo "git-siblings in: only 'worktree list' is permitted" >&2
                    exit 1
                fi
                ;;
            config)
                # Only allow --get/--list reads.
                ok=0
                for a in "$@"; do
                    case "$a" in
                        --get|--get-all|--list|-l) ok=1 ;;
                    esac
                done
                if [ "$ok" -ne 1 ]; then
                    echo "git-siblings in: config requires --get/--get-all/--list" >&2
                    exit 1
                fi
                ;;
        esac

        repo_path=$(resolve_repo "$repo_name") || exit 2
        exec git -C "$repo_path" "$git_sub" "$@"
        ;;
    *)
        usage
        ;;
esac
