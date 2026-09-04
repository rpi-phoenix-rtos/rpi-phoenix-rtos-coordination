#!/usr/bin/env bash

set -euo pipefail

usage() {
	cat <<'EOF'
Usage: rebuild-rpi4b-fast.sh [options]

Fast incremental Raspberry Pi 4 rebuild helper.

Default behavior:
- refresh the copied VM-local buildroot incrementally
- auto-select the narrowest safe Phoenix build phase
- rebuild the Pi 4 image
- assemble/export/verify the SD image

Options:
  --scope auto|project|core|full-clean
      auto:
        project/image when only phoenix-rtos-project or plo are dirty
        core/project/image when core repos are dirty
        clean/host/core/project/image when build-infra repos are dirty
      project:
        run build.sh project image
      core:
        run build.sh core project image
      full-clean:
        run build.sh clean host core project image
  --with-showcase
      build the showcase-app layer (GPU/GL/Vulkan stack, X11 server + apps,
      dillo/mc/nano) via scripts/build-showcase-apps.sh. Runs the GPU archive
      builds BEFORE build.sh — the five game ports link them — and stages the
      X11/ports app binaries into the rootfs after. Adds host deps
      (meson/ninja/mako/libdrm-dev/glslang) — install via
      scripts/bootstrap-linux-host.sh. The games themselves come from the ports
      stage, which --with-showcase forces into the stage list.
  --with-tests
      build phoenix-rtos-tests for aarch64 (incl. the libc Unity suite) via the
      build.sh `test` stage and stage the binaries into the rootfs so they can be
      run on the Pi (e.g. `test-libc-string`, `test-libc-stdlib`).
  --build-only
      skip bootfs/sdimg export and verification
  --ports-only
      build ONLY the phoenix-rtos-ports `ports` stage (implies --with-ports
      and --build-only). Ports stage writes straight into the rootfs tree
      _fs/<target>/root (PREFIX_ROOTFS); it does NOT rebuild loader.disk or
      any core/project artifact. Use when staging ports onto an external
      rootfs (e.g. the NFS export) without touching the boot image.
  --skip-prepare
      do not refresh the copied VM-local buildroot first.
      CAVEAT: the buildroot holds COPIES of phoenix-rtos-{build,ports} and of
      _projects/, and the build reads the copies. So with --skip-prepare an edit
      to a port.def.sh, a plo yaml or the port_manager has NO effect -- the same
      stale-copy trap as the documented stale-core hazard. Skip prepare only to
      re-run a build whose inputs have not changed (it also saves the _fs rsync).
  --qemu-sanity
      run the direct Pi 4 QEMU serial sanity lane after build
  --buildroot PATH
      override VM-local copied buildroot
  -h, --help
      show this help
EOF
}

die() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
sources_dir="${repo_root}/sources"

host_os="$(uname -s)"

# Host-OS-specific defaults. On macOS we shell into the phoenix-dev
# Lima VM (which has the toolchain pre-installed under
# /home/witoldbolt.guest/phoenix-toolchains/). On Linux we run the
# build directly on the host, expecting the toolchain to be on PATH
# (typically $HOME/phoenix-rpi/.toolchain/aarch64-phoenix/bin/ if
# built via phoenix-rtos-build/toolchain/build-toolchain.sh).
vm="${PHOENIX_VM:-phoenix-dev}"
if [ "$host_os" = "Darwin" ]; then
	buildroot="${RPI4B_BUILDROOT:-/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy}"
	toolchain_path="${PHOENIX_AARCH64_TOOLCHAIN:-/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix/bin}"
else
	buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
	toolchain_path="${PHOENIX_AARCH64_TOOLCHAIN:-${repo_root}/.toolchain/aarch64-phoenix/bin}"
fi
dtb_path="${RPI4B_DTB_PATH:-/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb}"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
scope="auto"
do_prepare=1
do_build_artifacts=1
do_qemu_sanity=0
with_ports=0
with_tests=0
ports_only=0
# --with-showcase: build the showcase-app layer (GPU/GL/Vulkan stack, X11 server
# + apps, dillo/mc/nano) and put it in the image. Two-phase: the GPU archives are
# built BEFORE build.sh (the five game ports in ports.yaml link them by absolute
# path); the X11/ports app binaries are staged into _fs/<target>/root AFTER
# build.sh, before the ext2 image is packed.
with_showcase=0
with_vkquake=0
# Build variant (selects the boot script in user.plo.yaml via the RPI4B_VARIANT
# env var):
#   nfsroot (default) - mount the NFS export as root over the network (#153 T3 /
#                       #44): the booted Pi gets a real "/" with /bin /usr /var
#                       /etc + /dev via devfs. Netboot-delivered (no ext2 needed).
#   netboot           - legacy probe-only SD, card-out safe, NFS at /mnt (no
#                       real root tree). Kept as the rollback fallback.
#   sd                - mount the ext2 partition on the SD card as root (#120)
variant="nfsroot"

while [ "$#" -gt 0 ]; do
	case "$1" in
		--scope)
			shift
			[ "$#" -gt 0 ] || die "missing value for --scope"
			scope="$1"
			;;
		--with-ports)
			# Insert the build.sh `ports` stage (builds phoenix-rtos-ports
			# entries listed in the project/target ports.yaml, e.g. busybox).
			# Off by default because the ports compile is slow and most
			# iterations don't touch ports.
			with_ports=1
			;;
		--with-tests)
			# Insert the build.sh `test` stage (builds phoenix-rtos-tests, incl.
			# the libc Unity suite, for aarch64 and stages the binaries into the
			# rootfs so they can be run on the Pi). Off by default: the whole
			# suite is ~60 binaries and most iterations don't need them.
			with_tests=1
			;;
		--variant)
			shift
			[ "$#" -gt 0 ] || die "missing value for --variant"
			case "$1" in
				netboot|sd|nfsroot) variant="$1" ;;
				*) die "unknown variant: $1 (use netboot|sd|nfsroot)" ;;
			esac
			;;
		--with-showcase)
			with_showcase=1
			;;
		--with-vkquake)
			# Retained for compatibility only: the V3DV/Vulkan stack is part of the
			# default showcase since 2026-09-03 (the vkquake port is `if: true` and
			# links libv3dv-phoenix.a, so it is not optional). This now just implies
			# --with-showcase.
			with_showcase=1
			with_vkquake=1
			;;
		--build-only)
			do_build_artifacts=0
			;;
		--ports-only)
			ports_only=1
			with_ports=1
			do_build_artifacts=0
			;;
		--skip-prepare)
			do_prepare=0
			;;
		--qemu-sanity)
			do_qemu_sanity=1
			;;
		--buildroot)
			shift
			[ "$#" -gt 0 ] || die "missing value for --buildroot"
			buildroot="$1"
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			die "unknown option: $1"
			;;
	esac
	shift
done

project_repos=(
	phoenix-rtos-project
	plo
)

core_repos=(
	libphoenix
	phoenix-rtos-corelibs
	phoenix-rtos-devices
	phoenix-rtos-filesystems
	phoenix-rtos-kernel
	phoenix-rtos-lwip
	phoenix-rtos-posixsrv
	phoenix-rtos-usb
	phoenix-rtos-utils
)

full_repos=(
	phoenix-rtos-build
	phoenix-rtos-hostutils
	phoenix-rtos-ports
	phoenix-rtos-tests
)

repo_is_dirty() {
	local repo="$1"
	local path="${sources_dir}/${repo}"

	[ -d "${path}" ] || return 1
	[ -n "$(git -C "${path}" status --short 2>/dev/null || true)" ]
}

collect_dirty() {
	local repo
	local dirty=()

	for repo in "$@"; do
		if repo_is_dirty "${repo}"; then
			dirty+=("${repo}")
		fi
	done

	if [ "${#dirty[@]}" -gt 0 ]; then
		printf '%s\n' "${dirty[@]}"
	fi
}

dirty_project=()
while IFS= read -r line; do
	[ -n "${line}" ] && dirty_project+=("${line}")
done <<EOF
$(collect_dirty "${project_repos[@]}")
EOF

dirty_core=()
while IFS= read -r line; do
	[ -n "${line}" ] && dirty_core+=("${line}")
done <<EOF
$(collect_dirty "${core_repos[@]}")
EOF

dirty_full=()
while IFS= read -r line; do
	[ -n "${line}" ] && dirty_full+=("${line}")
done <<EOF
$(collect_dirty "${full_repos[@]}")
EOF

build_args=()
scope_reason=

case "${scope}" in
	project)
		# `fs` belongs here for the same reason it does under `core`, plus a worse
		# one: prepare-buildroot.sh rsyncs _fs/<target>/root with --delete on EVERY
		# run, so by the time this stage list is chosen the staged rootfs is already
		# gone. Without `fs` nothing re-applies root-skel or the rootfs-overlay, and
		# the build completes with a rootfs missing /etc and all ~300 MB of game
		# data -- silently, because no stage failed. Observed 2026-09-03: after a
		# `--scope project` run, _fs/.../root/usr/share had no game data at all
		# while the overlay held pak0.pk3 + pak1.pk3 + q3key correctly. `fs` is a
		# cheap idempotent copy, and build.sh orders stages itself.
		build_args=(fs project image)
		scope_reason="forced project scope (+fs: prepare deletes the staged rootfs)"
		;;
	core)
		# `fs` (root-skel -> _fs/<target>/root) must precede ports/project on a
		# COLD buildroot: some ports read config out of $PREFIX_ROOTFS/etc during
		# their prepare step (e.g. lighttpd greps /etc/lighttpd.conf to generate
		# its static plugin-init list). Without it that dir is absent and the port
		# prepare aborts. `fs` is a cheap, idempotent cp -a. build.sh runs stages in
		# a fixed order (fs -> core -> ports -> project), so the listing order here
		# does not matter, only presence.
		build_args=(fs core project image)
		scope_reason="forced core scope"
		;;
	full-clean)
		# `ports` MUST be between core and project: the nfsroot variant's nfs-fs
		# (filesystems/nfs) links the libnfs port and is built in the project stage
		# (see build.project b_build_project), so libnfs.a + <nfsc/libnfs.h> must
		# already exist. Omitting `ports` makes a clean build fail with
		# "fatal error: nfsc/libnfs.h: No such file" and — worse on a warm sysroot —
		# silently reuse a STALE nfs-fs that is ABI-mismatched against the freshly
		# rebuilt libphoenix/kernel (observed: every NFS read returns ERANGE, exec
		# from NFS fails -12). A full clean must rebuild the ports too.
		# `fs` re-applies the root-skel AFTER `clean` wipes _fs (build.sh runs
		# clean -> fs -> core -> ports -> project): ports such as lighttpd read
		# $PREFIX_ROOTFS/etc during prepare, so the skeleton must exist first.
		build_args=(clean host fs core ports project image)
		scope_reason="forced full-clean scope"
		;;
	auto)
		if [ ! -d "${buildroot}/_build/${target}" ]; then
			# COLD buildroot. `auto` keys purely off sibling-repo dirt, so a fresh
			# checkout with clean repos (the Docker build at Dockerfile:113, or any
			# first run after a full-clean nuke) resolved to `project image` — which
			# reuses core objects that do not exist yet and produces a broken or
			# empty image with no error naming the cause. Presence of the target
			# build dir is the honest test for "is there anything to reuse".
			build_args=(host fs core ports project image)
			scope_reason="cold buildroot (${buildroot}/_build/${target} absent): full stage list"
		elif [ "${#dirty_full[@]}" -gt 0 ]; then
			# A dirty build-infra repo forces a `clean`; that clean wipes _fs and
			# every staged port, so — exactly like `full-clean` above — `fs` and
			# `ports` MUST be rebuilt too. Omitting `ports` strands libnfs and makes
			# the nfsroot nfs-fs fail to compile ("fatal error: nfsc/libnfs.h: No
			# such file"); omitting `fs` breaks port prepare steps that read
			# $PREFIX_ROOTFS/etc. build.sh runs stages in fixed order regardless.
			build_args=(clean host fs core ports project image)
			scope_reason="build-infra repos dirty: ${dirty_full[*]}"
		elif [ "${#dirty_core[@]}" -gt 0 ]; then
			build_args=(core project image)
			scope_reason="core repos dirty: ${dirty_core[*]}"
		else
			build_args=(project image)
			if [ "${#dirty_project[@]}" -gt 0 ]; then
				scope_reason="project-only repos dirty: ${dirty_project[*]}"
			else
				scope_reason="no source repo dirt detected; defaulting to fast project/image rebuild"
			fi
		fi
		;;
	*)
		die "unknown scope: ${scope}"
		;;
esac

# --with-ports: insert the build.sh `ports` stage just before `project`
# (ports must be built before the project stages them into the image).
if [ "${with_ports}" = 1 ]; then
	new_args=()
	for arg in "${build_args[@]}"; do
		if [ "${arg}" = "project" ]; then
			new_args+=(ports)
		fi
		new_args+=("${arg}")
	done
	build_args=("${new_args[@]}")
	scope_reason="${scope_reason}; +ports (busybox etc.)"
fi

# --variant sd: produce the COMPLETE bootable 2-partition SD image (FAT boot +
# ext2 root) in one command. The ext2 root is populated from the staged rootfs
# tree (_fs/<target>/root), which needs the `fs` skeleton, the `core`/`project`
# binaries (psh + servers) and the `ports` (busybox etc.) all present — the
# `auto` scope alone gives just `project image`, so on a clean tree the rootfs
# would be missing core binaries and every port. Force the full, deterministic
# sd stage list so the single command works from a cold buildroot. (`--scope`
# other than auto is an explicit developer override and is left untouched; the
# ext2 rootfs step at the tail still runs for sd regardless.) The ext2 image is
# assembled after `image` by build-rpi4b-rootfs-ext2.sh in the artifact tail.
if [ "${ports_only}" = 0 ] && [ "${scope}" = "auto" ] && { [ "${variant}" = "sd" ] || [ "${with_showcase}" = 1 ]; }; then
	# `host` builds the hostutils (metaelf/syspagen/mkrofs/...) that the image
	# stage needs; include it so the command is self-sufficient from a cold
	# buildroot (build.sh's clean wipes the host prefix too). `fs` applies the
	# rootfs-overlay (e.g. the Quake pak0.pak). Forced for the sd variant (which
	# packs the ext2 root) AND for any --with-showcase build (nfsroot needs the
	# apps + overlay staged into _fs/root before it is served over NFS).
	build_args=(host fs core ports project image)
	scope_reason="full stage list (host fs core ports project image): sd variant and/or --with-showcase"
fi

# --ports-only: build the `ports` stage and nothing else. This stages port
# binaries into _fs/<target>/root without rebuilding loader.disk or any
# core/project artifact (used when populating an external NFS rootfs).
if [ "${ports_only}" = 1 ]; then
	# Stage the filesystem skeleton (root-skel -> _fs/<target>/root) before the
	# ports stage: some ports read config out of $PREFIX_ROOTFS/etc during their
	# prepare step (e.g. lighttpd greps /etc/lighttpd.conf to generate its static
	# plugin-init list). Without the fs stage that directory does not exist and
	# the port prepare fails. `fs` is cheap (a cp -a of root-skel) and idempotent.
	build_args=(fs ports)
	scope_reason="ports-only (stage fs skeleton + ports into _fs root; no image rebuild)"
fi

# --with-tests: insert the build.sh `test` stage before `project`. This runs AFTER
# the --variant/--ports-only blocks above so it survives their build_args rewrites
# (e.g. `--variant sd` rebuilds the stage list). Tests need the `core` sysroot;
# build.sh runs the test stage in its fixed core->test->project order regardless of
# position. No `project` in build_args (e.g. --ports-only) => nothing to insert.
if [ "${with_tests}" = 1 ]; then
	new_args=()
	for arg in "${build_args[@]}"; do
		if [ "${arg}" = "project" ]; then
			new_args+=(test)
		fi
		new_args+=("${arg}")
	done
	build_args=("${new_args[@]}")
	scope_reason="${scope_reason}; +tests (phoenix-rtos-tests)"
fi

# Task #31 logging build mode: the single source of truth is the
# RPI4_LOG_TO_FILE macro in the target's board_config.h. The compile-time
# console sinks (kernel log.c, pl011-tty) read the macro directly; the
# rpi4-klogd plo launch gate needs it as an env var, so derive it here (same
# pattern as --variant -> RPI4B_VARIANT). We export RPI4_LOG_TO_FILE=1 ONLY when
# the macro is set to 1, so a DEBUG build (the default) leaves it unset and the
# klogd launch in user.plo.yaml stays inert. Deriving the env from the macro
# every build keeps the two in lock-step (they cannot desync).
board_config="${sources_dir}/phoenix-rtos-project/_projects/${target}/board_config.h"
log_to_file=0
if [ -f "${board_config}" ] && grep -Eq '^[[:space:]]*#define[[:space:]]+RPI4_LOG_TO_FILE[[:space:]]+1\b' "${board_config}"; then
	log_to_file=1
fi

if [ "$host_os" = "Darwin" ]; then
	printf 'Host:     macOS (using Lima VM %s)\n' "${vm}"
else
	printf 'Host:     Linux (direct, no VM)\n'
fi
printf 'Toolchain: %s\n' "${toolchain_path}"
printf 'Buildroot: %s\n' "${buildroot}"
printf 'Target:    %s\n' "${target}"
printf 'Scope:     %s\n' "${scope}"
printf 'Variant:   %s\n' "${variant}"
if [ "${log_to_file}" = 1 ]; then
	printf 'Logging:   USER (klog -> /var/log/messages, console quiet; RPI4_LOG_TO_FILE=1)\n'
else
	printf 'Logging:   DEBUG (klog -> console, default; RPI4_LOG_TO_FILE=0)\n'
fi
printf 'Build args: %s\n' "${build_args[*]}"
printf 'Reason:    %s\n' "${scope_reason}"

# Helper to run a build-shell command on the right host. On macOS this
# is `limactl shell -y phoenix-dev -- bash -lc <cmd>`. On Linux we run
# it directly with `bash -lc`.
run_build_shell() {
	local cmd="$1"
	if [ "$host_os" = "Darwin" ]; then
		limactl shell -y "${vm}" -- /bin/bash -lc "${cmd}"
	else
		/bin/bash -lc "${cmd}"
	fi
}

if ! run_build_shell "[ -f '${dtb_path}' ]"; then
	printf 'info: missing Pi 4 DTB at %s; preparing it now\n' "${dtb_path}" >&2
	"${repo_root}/scripts/prepare-rpi4b-dtb.sh"
fi

# Regenerate the embedded WiFi firmware C array if missing or stale.
# Emits a zero-length stub if .firmware/ isn't populated, so the lwip
# build keeps working for non-WiFi developers.
"${repo_root}/scripts/gen-wifi-fw-c.sh"

# --scope full-clean: wipe the caches that live OUTSIDE the buildroot.
#
# `build.sh clean` (phoenix-rtos-build/build.sh:186-189) removes exactly four
# paths: _build/<target>, _build/host-generic-pc, _fs/<target>, _boot/<target>.
# Everything below survives it, and each one has already shipped a stale artifact
# at least once. This block is what makes `--scope full-clean` mean what its name
# says: reuse NOTHING.
#
# Set RPI4B_KEEP_HOST_CACHES=1 to skip it (much faster, but then it is not a clean
# build — say so in whatever you report).
if [ "${scope}" = "full-clean" ] && [ "${RPI4B_KEEP_HOST_CACHES:-0}" != 1 ]; then
	printf 'Full-clean: wiping the caches build.sh clean does NOT touch\n'

	# _boot/host-generic-pc — clean only removes _boot/$TARGET, so the host
	# metaelf/syspagen/mkrofs copies here outlive a "clean" build forever.
	rm -rf "${buildroot}/_boot/host-generic-pc"

	# tools/.gpu-libs/*.a — the GPU archives the five game ports link by absolute
	# path. Nothing in build.sh knows they exist; only build-showcase-apps.sh's
	# mtime check gates them, and that check cannot see a libphoenix ABI change.
	# The dir also accumulates archives/binaries no longer produced by any script
	# (libquakespasm*.a, libvkquake.a, e4-x11-play, gl-x11-window as of
	# 2026-09-03) which look current to a human reading `ls`.
	rm -f "${repo_root}"/tools/.gpu-libs/*.a
	rm -f "${repo_root}"/tools/.gpu-libs/gl-x11-window \
	      "${repo_root}"/tools/.gpu-libs/gl-x11-window-daemon \
	      "${repo_root}"/tools/.gpu-libs/e4-x11-play

	# Host-side /tmp intermediates. NONE of these has a freshness check: every
	# tools/ports and tools/x11-port script skips its build when the output is
	# already present in its /tmp prefix, so a library built against last month's
	# libphoenix is reused indefinitely. /tmp/wmaker-deps is the worst of them — it
	# snapshots /tmp/x11-phoenix once and then refuses to refresh (cp -an).
	rm -rf /tmp/mesa-v3d-build /tmp/mesa-v3dv-build /tmp/mesa-pyenv \
	       /tmp/x11-phoenix /tmp/wmaker-deps \
	       /tmp/phoenix-iconv /tmp/phoenix-ffi /tmp/phoenix-ncurses \
	       /tmp/phoenix-glib /tmp/phoenix-mc /tmp/fltk-phoenix /tmp/dillo-phoenix \
	       /tmp/python-port-build \
	       /tmp/qsobj /tmp/qsobj-det /tmp/qsobj-sdl /tmp/vkqobj \
	       /tmp/sdl2test-obj /tmp/sdl2audio-obj /tmp/gl-smoke-build
	rm -f  /tmp/v3dphx-aux.txt /tmp/libv3d-phoenix-daemon.a /tmp/libv3d-client.a \
	       /tmp/libv3d-client.o /tmp/glamor_phoenix_ctx.o /tmp/epoxy_shim.o \
	       /tmp/gl_x11_window.o

	# The EXTRACTED port source trees (tools/{ports,x11-port}/src/<pkg>/) keep
	# their .o files and their config.status, and every tools/ports script skips
	# configure when config.status is present. So a full-clean still recompiles
	# those ports from objects built against an older libphoenix.
	#
	# Not wiped by default, deliberately: re-extracting ~40 packages re-runs every
	# configure (adds well over an hour), and the Docker --no-cache build already
	# proves the from-nothing path -- it clones fresh, so no extracted tree exists
	# at all. Set RPI4B_CLEAN_PORT_SOURCES=1 to close the hole here too; the
	# tarballs are kept, so this re-extracts and re-patches without re-downloading.
	if [ "${RPI4B_CLEAN_PORT_SOURCES:-0}" = 1 ]; then
		printf 'Full-clean: re-extracting port sources (RPI4B_CLEAN_PORT_SOURCES=1)\n'
		for srcdir in "${repo_root}"/tools/ports/src "${repo_root}"/tools/x11-port/src; do
			[ -d "${srcdir}" ] || continue
			find "${srcdir}" -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +
		done
	fi

	# The EXTRACTED, PATCHED and CONFIGURED port trees. Wiping the /tmp prefixes
	# alone is not enough and is the trap this block exists to avoid: every
	# config.status, every ".already patched" stamp (.dillo-tls-mode,
	# .mc-guard-configured, .phoenix-glamor-enabled) and — critically — the 25
	# xorg-server core archives live under tools/{ports,x11-port}/src/, not /tmp.
	# build-xserver-core.sh's core_built() checks those archives in the src tree, so
	# with /tmp cleared but src/ kept it early-returns on last month's archives and
	# Xphoenix links against them.
	# Only the extracted DIRECTORIES go; the downloaded tarballs sitting next to
	# them are kept, so this costs a re-extract, not a re-download (an x.org CDN
	# outage killed a full clean build once already, session ~206).
	for src_root in "${repo_root}/tools/ports/src" "${repo_root}/tools/x11-port/src"; do
		[ -d "${src_root}" ] || continue
		for tree in "${src_root}"/*/; do
			[ -d "${tree}" ] && rm -rf "${tree}"
		done
	done

	# Ad-hoc build dirs that live under tools/ rather than /tmp.
	rm -rf "${repo_root}/tools/v3d-driver-port/.build-csd-daemon"

	# The x.org distfile cache is KEPT (no re-download) but is unverified — no
	# checksums anywhere in build-x11-phoenix.sh. A truncated tarball cached during
	# a CDN outage would silently re-extract into a broken tree, so flag the
	# suspiciously small ones rather than trusting it blindly.
	distfiles="${PHOENIX_DISTFILES:-${HOME}/.phoenix-distfiles/x11}"
	if [ -d "${distfiles}" ]; then
		tiny="$(find "${distfiles}" -type f -size -10k -print 2>/dev/null)"
		if [ -n "${tiny}" ]; then
			printf 'Full-clean: WARNING suspiciously small files in the x.org distfile cache\n' >&2
			printf '            (a CDN outage serves 95-byte stubs). Delete these and re-run:\n' >&2
			printf '%s\n' "${tiny}" >&2
		fi
	fi

	printf 'Full-clean: wiped _boot/host-generic-pc, tools/.gpu-libs, the /tmp build\n'
	printf '            prefixes, and the extracted trees under tools/{ports,x11-port}/src\n'
	printf 'Full-clean: NOT wiped (deliberate): the ports tarball cache under\n'
	printf '            sources/phoenix-rtos-ports/*/ — every tarball is size+sha256\n'
	printf '            verified on cold extract (port_prepare.sh), and port-sources/\n'
	printf '            is gone with _build, so each port re-extracts and re-patches.\n'
	printf 'Full-clean: NOT wiped (needs sudo, do it by hand): the Mesa shader disk\n'
	printf '            cache on the NFS export — see docs/misc/2026-09-03-clean-rebuild-runbook.md\n'
fi

if [ "${do_prepare}" -eq 1 ]; then
	run_build_shell "cd '${repo_root}' && ./scripts/prepare-buildroot.sh --copy-components '${buildroot}'"
fi

# --with-showcase, phase gpu: build the GPU/GL/Vulkan + Quake archives into
# tools/.gpu-libs. What needs them is the PORTS stage (the five game ports link
# tools/.gpu-libs/lib{GL,v3d,v3dv}-phoenix.a by absolute path and b_die without
# them); nothing in core does. So this runs BETWEEN core and ports -- see
# run_phoenix_build + the stage split below -- because the Mesa objects must compile
# against the sysroot core produces, not against the hand-maintained toolchain
# bundle (docs/misc/2026-09-04-toolchain-header-skew.md).
run_gpu_phase() {
	[ "${with_showcase}" = 1 ] || return 0
	# --scope full-clean must force the GPU archives too. Without --force the gpu
	# phase falls back to archive_fresh()'s mtime comparison against the Mesa/port
	# SOURCES only — so on a full clean the archives look "fresh" and the five game
	# ports link last week's libGL/libv3d/libv3dv into a freshly built rootfs. The
	# wipe above already removed them; --force additionally re-runs `meson setup`
	# (build-showcase-apps.sh:225-233 rm -rf's the /tmp mesa trees) so no generated
	# Mesa source survives either.
	gpu_force_arg=""
	[ "${scope}" = "full-clean" ] && gpu_force_arg="--force"
	printf 'Showcase:  building GPU archives (phase gpu) before build.sh%s\n' \
		"$( [ -n "${gpu_force_arg}" ] && printf ' [--force: full-clean]' )"
	"${repo_root}/scripts/build-showcase-apps.sh" --phase gpu ${gpu_force_arg}
}

# GPU_LIBS: the rpi4-quake/-vkquake Makefiles compute this by climbing 4 levels
# from their own dir, which is correct for the sources/... tree but off-by-one
# for a repo-root-level .buildroot (the VM/publication layout). Pass it
# explicitly so the archives are always found when present. Harmless when the
# archives are absent (the Makefiles still skip the component gracefully).
gpu_libs_env="GPU_LIBS='${repo_root}/tools/.gpu-libs' "

# --with-showcase: no game binary is bundled into loader.disk any more (2026-09-03).
# RPI4B_WITH_SHOWCASE used to gate an `app ... rpi4-quake` line in user.plo.yaml; that
# line is gone, so the variable is no longer exported. What IS load-bearing now is a
# precondition check: the five game ports (ports.yaml if:true) link
# tools/.gpu-libs/lib{GL,v3d,v3dv}-phoenix.a by absolute path and b_die without them,
# so verify the gpu phase actually produced them and say so plainly here rather than
# letting the ports stage fail deep inside port_manager.
showcase_env=""
check_gpu_archives() {
	[ "${with_showcase}" = 1 ] || return 0
	local missing_gpu=() gpu_archive
	for gpu_archive in libGL-phoenix.a libv3d-phoenix.a libv3dv-phoenix.a; do
		[ -f "${repo_root}/tools/.gpu-libs/${gpu_archive}" ] || missing_gpu+=("${gpu_archive}")
	done
	if [ "${#missing_gpu[@]}" -eq 0 ]; then
		printf 'Showcase:  GPU archives present; the five game ports will build into the rootfs (/usr/bin)\n'
	else
		printf 'Showcase:  MISSING GPU archives after the gpu phase: %s\n' "${missing_gpu[*]}" >&2
		printf '           The game ports link these by absolute path and will fail the ports stage.\n' >&2
	fi
}

# Task #31: pass RPI4_LOG_TO_FILE into the build env ONLY when the board macro is
# set, so the plo render (image_builder.py reads os.environ) gates the rpi4-klogd
# launch. In a DEBUG build the var stays unset and user.plo.yaml's
# `env.RPI4_LOG_TO_FILE | default('0')` resolves to '0' -> not launched.
log_to_file_env=""
if [ "${log_to_file}" = 1 ]; then
	log_to_file_env="RPI4_LOG_TO_FILE='1' "
fi

# One build.sh invocation with the given stage list. build.sh runs stages in its
# own fixed order (clean -> fs -> host -> core -> test -> ports -> project ->
# image), so a stage list is a SET; splitting the set across two invocations is
# how an external phase gets sequenced in between.
#
# Prepend the repo's uv venv bin so the build's bare `python3` (used by
# phoenix-rtos-build/build-ports.sh -> port_manager) finds resolvelib/jinja2/
# PyYAML/rich from the venv rather than the PEP668-managed system Python. A
# non-existent PATH entry is harmless, so this is safe even without the venv.
run_phoenix_build() {
	local stages="$*"
	printf 'Build:     ./phoenix-rtos-build/build.sh %s\n' "${stages}"
	run_build_shell \
		"set -euo pipefail; export PATH='${repo_root}/.venv/bin':'${toolchain_path}':\$PATH; cd '${buildroot}'; env ${log_to_file_env}${gpu_libs_env}${showcase_env}RPI4B_DTB_PATH='${dtb_path}' RPI4B_VARIANT='${variant}' TARGET='${target}' ./phoenix-rtos-build/build.sh ${stages}"
}

# core -> gpu -> ports. The GPU archives must compile against the sysroot the
# CORE stage produces (_build/<target>/sysroot); the ports stage is the only
# consumer of the archives. When both stages are in this build's stage list,
# split build.sh at `ports` and run the gpu phase in the gap. `fs` deliberately
# stays in the FIRST invocation only: re-running it after core would re-apply
# root-skel over core's output, which today's fs-before-core order never does.
#
# When the list has no `core` (a warm `project image` rebuild) or no `ports`,
# there is nothing to sequence: the gpu phase runs first, as before, against
# whatever sysroot the previous core build left behind.
pre_stages=()
post_stages=()
if [ "${with_showcase}" = 1 ] &&
	printf '%s\n' "${build_args[@]}" | grep -qx core &&
	printf '%s\n' "${build_args[@]}" | grep -qx ports; then
	for arg in "${build_args[@]}"; do
		if [ "${#post_stages[@]}" -gt 0 ] || [ "${arg}" = "ports" ]; then
			post_stages+=("${arg}")
		else
			pre_stages+=("${arg}")
		fi
	done
fi

if [ "${#post_stages[@]}" -gt 0 ]; then
	printf 'Order:     core -> gpu -> ports (build.sh split: [%s] then [%s])\n' \
		"${pre_stages[*]}" "${post_stages[*]}"
	run_phoenix_build "${pre_stages[@]}"
	run_gpu_phase
	check_gpu_archives
	run_phoenix_build "${post_stages[@]}"
else
	run_gpu_phase
	check_gpu_archives
	run_phoenix_build "${build_args[@]}"
fi

if [ "${do_qemu_sanity}" -eq 1 ]; then
	# QEMU path differs between hosts. On Darwin we use the in-VM
	# QEMU 10.2; on Linux we use /opt/qemu-11 (Ubuntu host install).
	if [ "$host_os" = "Darwin" ]; then
		qemu_bin="/home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64"
	else
		qemu_bin="${QEMU_AARCH64_BIN:-/opt/qemu-11/bin/qemu-system-aarch64}"
	fi
	run_build_shell \
		"set -euo pipefail; cd '${buildroot}'; log=/tmp/pi4-direct-fast-helper.log; timeout 25s '${qemu_bin}' -M raspi4b -cpu cortex-a72 -smp 4 -m 2G -nographic -monitor none -kernel _boot/${target}/plo.elf -device loader,file=_boot/${target}/rpi4b/loader.disk,addr=0x08000000,force-raw=on >\"\$log\" 2>&1 || true; grep -En 'call: exec go!|go: enter|hal: jump exit el1|A3|KLM|Exception #37' \"\$log\" || true"
fi

if [ "${do_build_artifacts}" -eq 0 ]; then
	exit 0
fi

"${repo_root}/scripts/assemble-rpi4b-bootfs.sh"
"${repo_root}/scripts/assemble-rpi4b-bootfs-img.sh"

# --with-showcase (both variants): stage the port + X11 app binaries into the
# rootfs tree (_fs/<target>/root) NOW — after build.sh finished populating it
# (fs/core/ports/project) and BEFORE the image assembly. Running earlier would be
# clobbered by build.sh's fs stage. The sd variant then packs this tree into the
# ext2 root; the nfsroot/netboot variant serves it over NFS (recreated from
# _fs/<target>/root). Staging for both is why this now lives outside the sd block.
if [ "${with_showcase}" = 1 ]; then
	printf 'Showcase:  staging X11/ports app binaries into rootfs (phase stage)\n'
	SHOWCASE_STAGE_DIR="${buildroot}/_fs/${target}/root" \
		RPI4B_BUILDROOT="${buildroot}" \
		"${repo_root}/scripts/build-showcase-apps.sh" --phase stage \
		--stage-dir "${buildroot}/_fs/${target}/root"
fi

if [ "${variant}" = "sd" ]; then
	# sd variant: build the 2-partition SD image (FAT boot + ext2 root). The FAT
	# bootfs image assembled above is consumed by build-rpi4b-rootfs-ext2.sh,
	# which populates the ext2 root from the staged rootfs tree (_fs/<target>/root)
	# and emits _boot/<target>/rpi4b-sd-2part.img. This is the real bootable card
	# image for the sd variant (the 1-part rpi4b-sd.img below is FAT-boot-only and
	# has no root filesystem, so it is skipped here). Export + verify target the
	# 2-part image.
	two_part_img="${buildroot}/_boot/${target}/rpi4b-sd-2part.img"
	exported_two_part="${repo_root}/artifacts/rpi4b/rpi4b-sd-2part.img"
	# The showcase root has to hold a lot more now, so --with-showcase grows it to
	# 1.5 GiB (was 768 MiB, which no longer fits). Arithmetic, measured 2026-09-03:
	#   base rootfs (psh/servers/ports/X11)   ~55 MiB
	#   five game engines in /usr/bin        ~108 MiB  (quakespasm 18.5 + yquake2
	#                                        19.1 + quake3e 19.2 + vkquake 12.8 +
	#                                        supertuxkart 38.0)
	#   game data in /usr/share              ~308 MiB  (quake/id1 18 + quake2 50 +
	#                                        quake3 46 + supertuxkart 194)
	# => ~470 MiB of content, and mke2fs -b 1024 -i 2048 spends roughly an eighth of
	# the volume on the inode table, so 768 MiB left almost no slack (STK's asset
	# tree is tens of thousands of small files, each rounded up to a 1 KiB block).
	# The downstream partition geometry is computed from the actual image size, so
	# this only enlarges partition 2. Override with RPI4B_ROOTFS_BLOCKS.
	rootfs_blocks="${RPI4B_ROOTFS_BLOCKS:-262144}"
	[ "${with_showcase}" = 1 ] && rootfs_blocks="${RPI4B_ROOTFS_BLOCKS:-1572864}"
	env RPI4B_BUILDROOT="${buildroot}" RPI4B_ROOTFS_BLOCKS="${rootfs_blocks}" \
		"${repo_root}/scripts/build-rpi4b-rootfs-ext2.sh"
	RPI4B_REMOTE_SDIMG="${two_part_img}" \
		RPI4B_EXPORT_SDIMG_PATH="${exported_two_part}" \
		"${repo_root}/scripts/export-rpi4b-sdimg.sh"
	exported_sha="$(shasum -a 256 "${exported_two_part}" | awk '{print $1}')"
	RPI4B_SDIMG_PATH="${exported_two_part}" \
		"${repo_root}/scripts/verify-rpi4b-sdimg.sh"
	printf 'Exported 2-partition SD image: %s\n' "${exported_two_part}"
	printf 'Exported SHA256: %s\n' "${exported_sha}"
else
	# netboot / nfsroot: 1-partition FAT-only image (root comes from the network).
	"${repo_root}/scripts/assemble-rpi4b-sdimg.sh"
	"${repo_root}/scripts/export-rpi4b-sdimg.sh"

	exported_sha="$(shasum -a 256 "${repo_root}/artifacts/rpi4b/rpi4b-sd.img" | awk '{print $1}')"
	"${repo_root}/scripts/verify-rpi4b-sdimg.sh"

	printf 'Exported SHA256: %s\n' "${exported_sha}"
fi
