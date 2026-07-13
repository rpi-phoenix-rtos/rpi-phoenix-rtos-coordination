#!/usr/bin/env bash
#
# build-sd-in-docker.sh — build the Pi 4 SD image with Docker from THIS local
# checkout (before the up-to-date port is on public GitHub). Starts a local
# git+http server exposing the repos (serve-repos-for-docker.sh), then runs the
# self-contained Dockerfile against it and exports the image to ./docker-out/.
#
# This is the LOCAL-SIMULATION wrapper. The real distribution path is just the
# Dockerfile against public GitHub (see README.md "Build with Docker"); this script
# only substitutes REPO_BASE/PAK0_URL to point at the host so no push is required.
#
# Serves the COMMITTED state of every repo — commit your work first.
#
# Usage: build-sd-in-docker.sh [OUTDIR]     (default OUTDIR: ./docker-out)
# Env:   DOCKER (default "sudo docker"), GIT_SERVE_PORT (9418), GIT_SERVE_HTTP_PORT (9419)
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
OUT="${1:-$ROOT/docker-out}"
PORT="${GIT_SERVE_PORT:-9418}"
HTTPPORT="${GIT_SERVE_HTTP_PORT:-9419}"
DOCKER="${DOCKER:-sudo docker}"
PAK0_REL="sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/rootfs-overlay/usr/share/quake/id1/pak0.pak"

mkdir -p "$OUT"

echo "== starting local repo server (git :$PORT + http :$HTTPPORT) =="
GIT_SERVE_PORT="$PORT" GIT_SERVE_HTTP_PORT="$HTTPPORT" \
	setsid bash "$ROOT/scripts/serve-repos-for-docker.sh" >/tmp/phoenix-docker-serve.log 2>&1 &
disown
cleanup() {
	pkill -f 'git daemon.*phoenix-git-serve' 2>/dev/null || true
	pkill -f "http.server $HTTPPORT" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
sleep 3
grep -q 'git-daemon' /tmp/phoenix-docker-serve.log 2>/dev/null && echo "server up" || { echo "server failed:"; cat /tmp/phoenix-docker-serve.log; exit 1; }

PAK0_URL="http://127.0.0.1:$HTTPPORT/$PAK0_REL"

echo "== docker build (host network → reaches the local servers; empty context, Dockerfile from stdin) =="
# --network=host so RUN's git clone/wget reach 127.0.0.1:$PORT/$HTTPPORT.
# Dockerfile piped on stdin => empty build context (nothing copied from host).
# --no-cache by default: a reproducible build must re-clone + rebuild from scratch
# (a cached layer would pin a stale clone/apt state). Set DOCKER_NO_CACHE="" for
# faster dev iteration (reuses cached toolchain etc.).
$DOCKER build ${DOCKER_NO_CACHE:---no-cache} --network=host \
	--build-arg REPO_BASE="git://127.0.0.1:$PORT" \
	--build-arg UPSTREAM_BASE="git://127.0.0.1:$PORT" \
	--build-arg PAK0_URL="$PAK0_URL" \
	-t phoenix-rpi - < "$ROOT/Dockerfile"

echo "== exporting SD image to $OUT =="
$DOCKER run --rm -v "$OUT":/out phoenix-rpi

echo "== done: $(ls -l "$OUT"/*.img 2>/dev/null) =="
