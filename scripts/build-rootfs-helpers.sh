#!/usr/bin/env bash
#
# build-rootfs-helpers.sh — build the small static helper binaries the rootfs needs
# that no framework port produces, into the rootfs staging tree.
#
# Owner directive 2026-09-03: "Why are you hand copying old binaries. This makes
# zero sense! Never do this!" Everything executable on the Pi must be produced by
# the build. Ports cover the engines and the big userland; these four (soon five)
# tiny C programs live in the coordination repo's tools/ and had no build home, so
# they were being carried forward by hand from the previous NFS export. This script
# is their build home.
#
# The games are framework ports (the *_drm ports, ports.yaml), which also build and
# install their own launchers (quake2, quake3, stk, qs-drm, vkq-drm). psh cannot set
# environment variables and cannot chain commands, so the remaining glue lives in tiny
# static C programs under tools/*. This is the single place that builds them:
#
#   /bin/ram-stage-play  tools/ram-stage/ram-stage-play.c
#                        copy an asset tree into the /tmp RAM disk, then exec the
#                        engine against the RAM copy (NFS reads are latency-bound;
#                        ~20x per scattered read). The quake2 and quake3 launchers exec it.
#   /bin/game-res        tools/gpu-lane/m9-res/game-res.c
#                        start a game in a lower fullscreen mode that rpi4-kms scales
#                        to the screen: `game-res stk|qs|q2|q3|vkq [WxH] [args...]`
#                        (docs/gpu-new-lane/M9-scaled-fullscreen.md). It execs the
#                        ports' -drm programs.
#   /usr/bin/pty-run     tools/pty-run/pty-run.c
#                        getty-style /dev/ptmx forwarder, for programs that want
#                        their own controlling terminal.
#   + the diagnostics listed in helpers=() below.
#
# All are static aarch64-phoenix ELFs built with the same toolchain and sysroot as
# the engines, so they are ABI-consistent with them. Nothing here touches the NFS
# export: the staging tree is the same _fs/<target>/root that the ext2 packer and
# sync-netboot-tree.sh both consume, so one build reaches both variants.
#
# Usage:
#   scripts/build-rootfs-helpers.sh [--stage-dir DIR]
#
# Env:
#   SHOWCASE_STAGE_DIR / --stage-dir   rootfs staging tree
#                                      (default $RPI4B_BUILDROOT/_fs/<target>/root)
#   RPI4B_BUILDROOT, RPI4B_TARGET, PHOENIX_AARCH64_TOOLCHAIN
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
toolchain="${PHOENIX_AARCH64_TOOLCHAIN:-${repo_root}/.toolchain/aarch64-phoenix/bin}"
cc="${toolchain}/aarch64-phoenix-gcc"

# Build against the SYSROOT the rest of the build just produced, never the
# toolchain's own bundled copy of libphoenix/libc/libm + headers.
#
# Why this is not optional: .toolchain/aarch64-phoenix/aarch64-phoenix/{lib,include}
# is a HAND-MAINTAINED bundle (tools/x11-port/build-x11-phoenix.sh's
# sync_toolchain_libc cp's libphoenix.a in; its include/ holds only the two headers
# somebody copied by hand). Compiling with no --sysroot silently links these helpers
# against whatever libphoenix happened to be copied there last — the documented
# "stale .toolchain libphoenix.a" footgun (observed as CPython's
# `create_gil PyCOND_INIT failed`). These launchers exec the game engines, which ARE
# built against the fresh sysroot, so an ABI skew here is a runtime crash nobody
# traces back to the build. Point at _build/<target>/sysroot and fail if it is absent.
#
# The three flags mirror phoenix-rtos-build/makes/setup-sysroot.mk:17-23 so the
# search order matches what every other Phoenix component compiles with.
sysroot="${PHOENIX_SYSROOT:-${buildroot}/_build/${target}/sysroot}"

stage_dir="${SHOWCASE_STAGE_DIR:-${buildroot}/_fs/${target}/root}"
while [ "$#" -gt 0 ]; do
	case "$1" in
		--stage-dir) shift; stage_dir="${1:?--stage-dir needs a value}" ;;
		-h|--help) sed -n '2,46p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) printf 'error: unknown option: %s\n' "$1" >&2; exit 2 ;;
	esac
	shift
done

log()  { printf '\033[0;36m[helpers]\033[0m %s\n' "$*"; }
die()  { printf '\033[0;31m[helpers] ERROR\033[0m %s\n' "$*" >&2; exit 1; }

[ -x "$cc" ] || die "cross compiler not found: $cc (run scripts/bootstrap-linux-host.sh)"
[ -d "$stage_dir" ] || die "staging tree does not exist: $stage_dir (run a build first)"
[ -f "$sysroot/lib/libphoenix.a" ] \
	|| die "no built sysroot at $sysroot (expected lib/libphoenix.a). Run the core build first — refusing to fall back to the toolchain's hand-copied libphoenix bundle."

# "<source>|<install path under the staging tree>"
helpers=(
	# ram-stage-play: the quake2/quake3 launchers exec it.
	"tools/ram-stage/ram-stage-play.c|bin/ram-stage-play"
	"tools/pty-run/pty-run.c|usr/bin/pty-run"
	# Diagnostic: run a program and sample SoC temperature + the VideoCore throttle
	# bitmask while it runs. The stability evidence is a large sample of SHORT runs;
	# a presentation may run a game for tens of minutes, and throttling would show
	# up on stage as the frame rate quietly degrading rather than as a crash.
	"tools/thermal-soak/thermal-soak.c|bin/thermal-soak"
	# Diagnostic: multithreaded malloc/realloc/free churn. The heap containment
	# guards fired in four field runs (09-15..09-17) while the host harness --
	# which compiles the real malloc_dl.c -- stayed clean over ~2.4M
	# single-threaded ops. Concurrency is the axis that harness cannot reach, and
	# both apps that fired are heavily multithreaded, so this drives the same
	# allocator from several threads with a size mix that keeps heaps being
	# created and released under each other.
	"tools/malloc-mt-stress/mtstress.c|bin/mtstress"
	# Diagnostic: does the PWM block drop a back-to-back register write? The audio
	# DMA stall (captured 3x on 2026-09-18) leaves every programmed register
	# correct except the one nothing could print -- the period -- and
	# audio_pwmInit() writes five PWM registers with no pacing. This samples that
	# ~100k times in one run on the UNUSED PWM0 instance, instead of one sample
	# per 2.5-minute boot. `--start-test N` adds the PIO-only reproducer for the
	# stall's actual signature: does the driver's init sequence ever leave a
	# channel enabled, clocked and FIFO-fed that never transmits (no DMA)?
	"tools/pwm-write-probe/pwmwrite.c|bin/pwmwrite"
	# Diagnostic: the other half of the same hunt. pwmwrite retired the PWM side
	# (7000 PIO starts, 0 failures), which leaves the DMA->DREQ->FIFO handshake as
	# the branch the audio stall still lives on. This repeats audio_dmaArm()'s exact
	# shape -- PWM init, PWM_DMAC, channel RESET, CONBLK_AD, ACTIVE, measure
	# SOURCE_AD progress -- on the UNUSED PWM0 instance driven by a spare legacy DMA
	# channel (6; 5 is the driver's), hundreds of times per boot instead of ~7 times
	# in 100 boots. `--cycle-clock [--clock-gap-us N]` adds the one surviving
	# hypothesis: stop + restart the SHARED CPRMAN PWM generator before every trial,
	# so the clock-start -> PWEN proximity a real boot has is reproduced rather than
	# assumed. That mode DISTURBS rpi4-audio's stream -- dedicated probe boot only.
	# `--cb-race [--barrier]` tests the barrier hypothesis directly: two control
	# blocks whose SOURCE_AD/TXFR_LEN the channel exposes in registers, rewritten and
	# armed with no `dsb` between the Normal-NC stores and the Device MMIO writes, so
	# a fetch of not-yet-landed bytes is caught in the act. `--barrier` is the control
	# arm. PWM0 / channel 6 only -- it does not touch rpi4-audio.
	"tools/pwm-dma-probe/pwmdma.c|bin/pwmdma"
	"tools/audio-armtrials/armtrials.c|bin/armtrials"
)
# M9: a lower fullscreen mode for each game (execs the ports' -drm programs).
helpers+=("tools/gpu-lane/m9-res/game-res.c|bin/game-res")

# Data files copied verbatim (not compiled): "<source>|<install path>|<mode>".
# Kept in this script because it already owns "small in-repo things that belong in
# the rootfs" and has the staging tree resolved; a separate script for two file
# copies would just be another thing to remember to run.
data_files=(
	"tools/demo-apps/life.py|usr/share/demo/life.py|644"
)

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

log "cc         = $cc"
log "sysroot    = $sysroot"
log "stage tree = $stage_dir"
for entry in "${helpers[@]}"; do
	src="${repo_root}/${entry%%|*}"
	dst="${stage_dir}/${entry##*|}"
	name="$(basename "$dst")"
	[ -f "$src" ] || die "helper source missing: $src"
	# -I the devices sibling so a helper that speaks a DRIVER's ioctl ABI includes the
	# real header (<audio/rpi4-audio/rpi4-audio.h>) instead of re-declaring the struct
	# locally, where it would silently drift from the driver the next time either side
	# changes. Device headers are not installed into the sysroot.
	"$cc" -O2 -static -Wall -Wextra \
		--sysroot="${sysroot}/" -B"${sysroot}/lib/" -iprefix "${sysroot}/" \
		-I"${repo_root}/sources/phoenix-rtos-devices" \
		-o "${tmp}/${name}" "$src" \
		|| die "compile failed: $src"
	# Phoenix has no dynamic loader for ordinary programs, so a PT_INTERP here
	# would be a binary that cannot start at all — check rather than trust.
	if readelf -l "${tmp}/${name}" 2>/dev/null | grep -q INTERP; then
		die "$name has a PT_INTERP segment (not a static ELF)"
	fi
	install -Dm755 "${tmp}/${name}" "$dst"
	log "built $(printf '%-14s' "$name") -> ${dst#"${stage_dir}"/} ($(stat -c%s "$dst") bytes)"
done

for entry in "${data_files[@]}"; do
	IFS='|' read -r rel dst mode <<< "$entry"
	src="${repo_root}/${rel}"
	[ -f "$src" ] || die "data file missing: $src"
	install -Dm"$mode" "$src" "${stage_dir}/${dst}"
	log "staged $(printf '%-14s' "$(basename "$dst")") -> ${dst} ($(stat -c%s "${stage_dir}/${dst}") bytes)"
done

log "done"
