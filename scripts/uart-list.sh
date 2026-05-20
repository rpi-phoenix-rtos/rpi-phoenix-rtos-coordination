#!/usr/bin/env bash
# List recent Pi 4 UART logs (filename only, newest first), optionally
# filtered by label substring.
#
# Replaces ad-hoc pipelines like `ls -t .../*.log | head -10 | sed 's|^.*/||'`
# so the operation fits a single allowlist entry.
#
# Usage:
#   ./scripts/uart-list.sh              # 10 most recent, filename only
#   ./scripts/uart-list.sh 20           # 20 most recent, filename only
#   ./scripts/uart-list.sh 5 cache      # 5 most recent matching "cache"
#   ./scripts/uart-list.sh --paths      # full paths instead of basenames
#   ./scripts/uart-list.sh --paths 5    # full paths, 5 most recent

set -u
set -o pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
UART_DIR="${PHOENIX_UART_DIR:-${repo_root}/artifacts/rpi4b-uart}"

mode="basename"
if [ $# -ge 1 ] && [ "$1" = "--paths" ]; then
    mode="path"
    shift
fi

count="${1:-10}"
label="${2:-}"

if [ ! -d "$UART_DIR" ]; then
    echo "uart-list: UART log dir not found: $UART_DIR" >&2
    exit 1
fi

# Compatible with bash 3.2 (macOS default): no `mapfile`. Use a here-doc
# pipe replacement via process substitution and a while-read loop.
printed=0
while IFS= read -r full; do
    [ -z "$full" ] && continue
    case "$full" in
        *.meta.*) continue ;;
    esac
    base="${full##*/}"
    if [ -n "$label" ]; then
        case "$base" in
            *"$label"*) ;;
            *) continue ;;
        esac
    fi
    if [ "$mode" = "path" ]; then
        echo "$full"
    else
        echo "$base"
    fi
    printed=$((printed + 1))
    if [ "$printed" -ge "$count" ]; then
        break
    fi
done < <(ls -t "$UART_DIR"/*.log 2>/dev/null)
