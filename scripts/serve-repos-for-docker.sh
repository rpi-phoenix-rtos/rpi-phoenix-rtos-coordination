#!/usr/bin/env bash
#
# serve-repos-for-docker.sh — expose the local phoenix-rpi git repositories over
# git:// (git-daemon) so the Docker build (Dockerfile) can `git clone` them AS IF
# from GitHub, before the up-to-date port has been pushed to public GitHub.
#
# Read-only. Serves the COMMITTED state of each repo (uncommitted working-tree
# edits are NOT served — commit them first if you want them in the image). Binds
# on all interfaces so the docker bridge (172.17.0.1) can reach it; keep it on a
# trusted host/LAN only.
#
# Exposes, as <name>.git:
#   phoenix-rpi            (this coordination repo)
#   <each sources/* sibling>
#   mesa quakespasm vkquake (external/ build deps)
# The Raspberry Pi firmware is cloned by bootstrap directly from public GitHub.
#
# Usage:
#   serve-repos-for-docker.sh            # run git-daemon in the foreground
#   GIT_SERVE_PORT=9418 ... &            # background it from the caller
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
SERVE="${GIT_SERVE_DIR:-/tmp/phoenix-git-serve}"
PORT="${GIT_SERVE_PORT:-9418}"

rm -rf "$SERVE"
mkdir -p "$SERVE"

link_repo() {
	local repo_root="$1" name="$2"
	[ -d "$repo_root/.git" ] || { echo "skip $name (no .git at $repo_root)"; return; }
	# Serve the .git dir directly so a clone gets the committed refs/objects.
	ln -sfn "$repo_root/.git" "$SERVE/$name.git"
	# git-daemon honours the export marker (belt-and-suspenders with --export-all).
	touch "$repo_root/.git/git-daemon-export-ok" 2>/dev/null || true
}

link_repo "$ROOT" phoenix-rpi
for d in "$ROOT"/sources/*/; do
	[ -d "${d%/}/.git" ] && link_repo "${d%/}" "$(basename "${d%/}")"
done
for e in mesa quakespasm vkquake; do
	[ -d "$ROOT/external/$e/.git" ] && link_repo "$ROOT/external/$e" "$e"
done

# Also serve non-git assets over HTTP: the Quake SHAREWARE pak0.pak (18 MB, freely
# redistributable) is NOT committed to any repo (licensing hygiene — full Quake data
# is the user's own), so the Docker build fetches it via PAK0_URL. For this local
# simulation we serve it from the host; for real GitHub distribution PAK0_URL points
# at a public shareware mirror. HTTP root = the coord repo so any tracked/working
# asset is reachable at http://<host>:$HTTPPORT/<path>.
HTTPPORT="${GIT_SERVE_HTTP_PORT:-9419}"
( cd "$ROOT" && exec python3 -m http.server "$HTTPPORT" --bind 0.0.0.0 ) \
	>/dev/null 2>&1 &
http_pid=$!
trap 'kill "$http_pid" 2>/dev/null || true' EXIT INT TERM

echo "== git-daemon: $(ls "$SERVE" | wc -l) repos from $SERVE on :$PORT ; http assets on :$HTTPPORT =="
ls "$SERVE"
# --base-path-relaxed lets the <name>.git symlinks resolve to their real .git dirs.
git daemon --reuseaddr --export-all --base-path-relaxed \
	--base-path="$SERVE" --listen=0.0.0.0 --port="$PORT" "$SERVE"
