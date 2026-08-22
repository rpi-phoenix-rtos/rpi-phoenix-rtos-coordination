#!/usr/bin/env bash
#
# fetch-quake-data.sh — stage freely-redistributable Quake game data into the
# rpi4b rootfs overlay, for the showcase Quake engines (GLQuake / Quake II /
# Quake III). One shared path used by the SD, Docker, and netboot builds.
#
# Usage:
#   scripts/fetch-quake-data.sh <game> <url> [sha256]
#     <game>   q1 | q2 | q3
#     <url>    download URL, or "" to skip (engine still builds, no data).
#              Accepted forms:
#                q1: quake106.zip-style (pak0.pak inside an LHA resource.1),
#                    a plain *.zip containing id1/pak0.pak, or a direct *.pak
#                q2: a *.zip containing baseq2/pak0.pak, or a direct *.pak
#                q3: a *.zip/*.pk3 containing demoq3/pak0.pk3, or a direct *.pk3
#     [sha256] expected sha256 of the resulting pak; mismatch fails.
#
#   Env overrides:
#     OVERLAY_ROOT  rootfs-overlay dir (default: the rpi4b project overlay).
#
# A non-empty URL that fails to download / extract / verify FAILS (exit 1) — we
# never ship a half-baked image on a broken download.
#
# LICENSING: only freely-redistributable demo/shareware data — NEVER full retail
# paks in a public build. Q1 shareware (quake106.zip) is redistributable. The Q2
# demo and Q3 demo paks have their OWN terms — supply a URL only after verifying
# redistribution rights for your distribution (that is why q2/q3 have no default
# URL in the Docker build).
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -eu

game="${1:?usage: fetch-quake-data.sh <q1|q2|q3> <url> [sha256]}"
url="${2-}"
want_sha="${3-}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
overlay_root="${OVERLAY_ROOT:-${repo_root}/sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/rootfs-overlay}"

case "$game" in
  q1) subdir="usr/share/quake/id1";     pak="pak0.pak" ;;
  q2) subdir="usr/share/quake2/baseq2"; pak="pak0.pak" ;;
  q3) subdir="usr/share/quake3/demoq3"; pak="pak0.pk3" ;;
  *)  echo "ERROR: unknown game '$game' (want q1|q2|q3)"; exit 2 ;;
esac

if [ -z "$url" ]; then
  echo "[$game] URL empty — skipping game data (engine builds without it)"
  exit 0
fi

dst="${overlay_root}/${subdir}"
mkdir -p "$dst"
tmp="$(mktemp -d)"
# makeself demo trees (Q3) unpack read-only files — chmod before rm, and never let
# cleanup failure poison the exit code (a real fetch/verify failure must still fail).
trap 'chmod -R u+w "$tmp" 2>/dev/null || true; rm -rf "$tmp" 2>/dev/null || true' EXIT

echo "[$game] fetching ${url}"
case "$url" in
  *.pak|*.pk3)
    curl -fSL --retry 5 --retry-delay 5 -o "$dst/$pak" "$url" \
      || { echo "ERROR: [$game] direct pak download failed from $url"; exit 1; } ;;
  *)
    curl -fSL --retry 5 --retry-delay 5 -o "$tmp/dl" "$url" \
      || { echo "ERROR: [$game] download failed from $url"; exit 1; }
    mkdir -p "$tmp/ex"
    case "$url" in
      *.exe)
        # Quake II demo: InstallShield/zip self-extractor (baseq2/pak0.pak inside).
        sz="$(command -v 7z || command -v 7za || true)"
        [ -n "$sz" ] \
          || { echo "ERROR: [$game] '$url' needs 7z to extract — install p7zip-full"; exit 1; }
        "$sz" x -y -o"$tmp/ex" "$tmp/dl" >/dev/null \
          || { echo "ERROR: [$game] 7z extraction failed for $url"; exit 1; } ;;
      *.gz.sh|*.sh|*.run)
        # Quake III demo: makeself gzip+shell self-extractor. The installer's own
        # --target/--noexec are unreliable; skip the shell header and gunzip+untar
        # the payload directly (skip= line self-declares the header length).
        skip="$(grep -am1 '^skip=' "$tmp/dl" | cut -d= -f2)"
        [ -n "$skip" ] \
          || { echo "ERROR: [$game] could not find makeself 'skip=' header in $url"; exit 1; }
        tail -n +"$skip" "$tmp/dl" | gzip -cd | tar xof - -C "$tmp/ex" \
          || { echo "ERROR: [$game] makeself payload extraction failed for $url"; exit 1; } ;;
      *)
        unzip -oq "$tmp/dl" -d "$tmp/ex" \
          || { echo "ERROR: [$game] could not unzip the archive from $url"; exit 1; }
        # Q1 shareware: pak0.pak is packed inside an LHA-compressed resource.1.
        if [ "$game" = q1 ] && [ -f "$tmp/ex/resource.1" ]; then
          ( cd "$tmp/ex" && lha xf resource.1 ) \
            || { echo "ERROR: [$game] could not LHA-extract resource.1"; exit 1; }
        fi ;;
    esac
    found="$(find "$tmp/ex" -iname "$pak" | head -1)"
    [ -n "$found" ] \
      || { echo "ERROR: [$game] $pak not found inside the archive from $url"; exit 1; }
    cp "$found" "$dst/$pak"; chmod u+w "$dst/$pak" ;;   # demo paks are often read-only
esac

got="$(sha256sum "$dst/$pak" | cut -d' ' -f1)"
sz="$(stat -c%s "$dst/$pak")"
echo "[$game] staged $pak: size=$sz sha256=$got -> $dst"
if [ -n "$want_sha" ] && [ "$got" != "$want_sha" ]; then
  echo "ERROR: [$game] $pak sha256 mismatch (got $got, expected $want_sha) — refusing to ship a bad image"
  exit 1
fi
echo "[$game] OK"
