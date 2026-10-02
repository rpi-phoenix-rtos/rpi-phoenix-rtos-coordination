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
      build the showcase image: forces the ports stage (the GPU stack, games,
      X and Wayland desktops, apps are all ports) and, after build.sh, stages
      the in-repo helper programs (scripts/build-showcase-apps.sh ->
      build-rootfs-helpers.sh) into the rootfs.
  --with-tests
      build phoenix-rtos-tests for aarch64 (incl. the libc Unity suite) via the
      build.sh `test` stage and stage the binaries into the rootfs so they can be
      run on the Pi (e.g. `test-libc-string`, `test-libc-stdlib`). Also exports
      RPI4B_WITH_TESTS=1, which adds the test and diagnostic programs a release
      image does not ship: the _user demos (hello, hellocpp, ...), the GPU smoke
      tests (ports.yaml: drmprobe, kmscube, vkcube) and, with --with-showcase,
      the diagnostics of build-rootfs-helpers.sh (thermal-soak, mtstress, ...).
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
      CAVEAT: the buildroot holds COPIES of EVERY sibling repo -- not just
      phoenix-rtos-{build,ports} and _projects/, but the core repos too
      (.buildroot/libphoenix, .buildroot/phoenix-rtos-kernel, ...) -- and the
      build reads the copies. So with --skip-prepare, an edit to ANY sibling
      source has NO effect: not a port.def.sh, not a plo yaml, not the
      port_manager, and not a kernel or libphoenix .c file either.
      `--scope core --skip-prepare` after a libphoenix edit is therefore a
      SILENT NO-OP that still exits 0 and still exports an image. Measured
      2026-09-10: a libphoenix string/string.c fix was invisible because
      .buildroot/libphoenix was 97 minutes stale; the only hint was this
      script's own drift table printing `libphoenix.a  identical`.
      Skip prepare only to re-run a build whose inputs have not changed (it
      also saves the _fs rsync).
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
# --with-showcase: build the showcase image (the ports stage + the helper programs,
# staged into _fs/<target>/root AFTER build.sh, before the ext2 image is packed).
with_showcase=0
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
			# Retained for compatibility only: vkQuake is part of the showcase.
			with_showcase=1
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

# Never run alongside another heavy host build (WebKit, Mesa): two at once took the host out of
# memory on 2026-10-02 and systemd-oomd killed the whole terminal scope. scripts/heavy-build.sh
# holds the same lock; under it (HEAVY_BUILD_LOCKED=1) we already own it.
if [ -z "${HEAVY_BUILD_LOCKED:-}" ]; then
	exec 9>"${HEAVY_BUILD_LOCK:-/tmp/phoenix-heavy-build.lock}"
	if ! flock -n 9; then
		printf 'rebuild-rpi4b-fast: waiting for another heavy build to finish (scripts/heavy-build.sh lock)\n' >&2
		flock 9
	fi
fi

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

# WiFi firmware (BCM43455; Cypress licence, never in git): fetched from
# linux-firmware at a pinned commit, sha256-verified, cached in .firmware/ (so
# later builds are offline) and staged into the rpi4b rootfs overlay as
# /lib/firmware, where the rpi4-wifi daemon reads it at boot. No network and no
# cache: a warning, and the image builds without WiFi. A checksum mismatch stops
# the build. (This replaces gen-wifi-fw-c.sh here: the driver no longer compiles
# the firmware in, and nothing else in the image build used the arrays.)
if [ "${target}" = "aarch64a72-generic-rpi4b" ]; then
	if ! "${repo_root}/scripts/fetch-wifi-firmware.sh"; then
		die "WiFi firmware failed verification (see [wifi-fw] above); refusing to build an image with it"
	fi
	# The overlay reaches the rootfs only in the `fs` stage. A stage list without
	# it (the `auto` fast path: project image) keeps whatever _fs/ already holds.
	if [[ " ${build_args[*]} " != *" fs "* ]] &&
	   [ -f "${repo_root}/sources/phoenix-rtos-project/_projects/${target}/rootfs-overlay/lib/firmware/brcm/brcmfmac43455-sdio.bin" ] &&
	   ! run_build_shell "[ -f '${buildroot}/_fs/${target}/root/lib/firmware/brcm/brcmfmac43455-sdio.bin' ]"; then
		printf 'warning: the WiFi firmware is staged in the overlay but not in %s/_fs/%s/root,
' "${buildroot}" "${target}" >&2
		printf '         and this stage list (%s) has no `fs` stage to copy it: this image has
' "${build_args[*]}" >&2
		printf '         no WiFi. Rebuild once with --scope project (or core) to apply the overlay.
' >&2
	fi
fi

# Desktop fonts (DejaVu TTF + /etc/fonts/fonts.conf + the fontconfig cache) into the
# rpi4b rootfs overlay, so EVERY image carries them -- the SD card image included. Until
# 2026-09-30 only sync-netboot-tree.sh staged them, into the NFS export, so a card image
# had no scalable font at all (Xft/GTK text on X and XFCE). The host's fonts-dejavu is
# the source (bootstrap installs it); no binaries in git.
if [ "${target}" = "aarch64a72-generic-rpi4b" ]; then
	overlay="${repo_root}/sources/phoenix-rtos-project/_projects/${target}/rootfs-overlay"
	RPI4B_NFS_EXPORT="${overlay}" "${repo_root}/scripts/stage-desktop-fonts.sh" \
		|| die "desktop fonts could not be staged into ${overlay} (see above)"
fi

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

	# Host-side /tmp intermediates. NONE of these has a freshness check: every
	# tools/ports and tools/x11-port script skips its build when the output is
	# already present in its /tmp prefix, so a library built against last month's
	# libphoenix is reused indefinitely. /tmp/wmaker-deps is the worst of them — it
	# snapshots /tmp/x11-phoenix once and then refuses to refresh (cp -an).
	rm -rf /tmp/x11-phoenix /tmp/wmaker-deps \
	       /tmp/phoenix-iconv /tmp/phoenix-ffi /tmp/phoenix-ncurses \
	       /tmp/phoenix-glib /tmp/phoenix-mc /tmp/fltk-phoenix /tmp/dillo-phoenix \
	       /tmp/python-port-build \
	       /tmp/qsobj /tmp/qsobj-det /tmp/qsobj-sdl /tmp/vkqobj \
	       /tmp/sdl2test-obj /tmp/sdl2audio-obj /tmp/gl-smoke-build

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
	# the X server linked against them.
	# Only the extracted DIRECTORIES go; the downloaded tarballs sitting next to
	# them are kept, so this costs a re-extract, not a re-download (an x.org CDN
	# outage killed a full clean build once already, session ~206).
	for src_root in "${repo_root}/tools/ports/src" "${repo_root}/tools/x11-port/src"; do
		[ -d "${src_root}" ] || continue
		for tree in "${src_root}"/*/; do
			[ -d "${tree}" ] && rm -rf "${tree}"
		done
	done

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

	printf 'Full-clean: wiped _boot/host-generic-pc, the /tmp build\n'
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

# Task #31: pass RPI4_LOG_TO_FILE into the build env ONLY when the board macro is
# set, so the plo render (image_builder.py reads os.environ) gates the rpi4-klogd
# launch. In a DEBUG build the var stays unset and user.plo.yaml's
# `env.RPI4_LOG_TO_FILE | default('0')` resolves to '0' -> not launched.
# --with-tests -> RPI4B_WITH_TESTS=1: ports.yaml (jinja-rendered by port_manager) and
# build-rootfs-helpers.sh add the test/diagnostic programs only then. Always passed
# explicitly (0 otherwise), so a value left in the caller's environment cannot put them
# into a release build.
with_tests_env="RPI4B_WITH_TESTS='${with_tests}' "

log_to_file_env=""
if [ "${log_to_file}" = 1 ]; then
	log_to_file_env="RPI4_LOG_TO_FILE='1' "
fi

# libphoenix's pre-main startup trace (LIBC_STARTUP_TRACE=y -> -DLIBC_STARTUP_TRACE
# in libphoenix/Makefile) is a DIAGNOSTIC knob for the pre-main hang.
#
# The forwarding below is belt-and-braces: `env VAR=... build.sh` keeps the
# inherited environment and `bash -lc` does not scrub it, so the bare
# `LIBC_STARTUP_TRACE=y ./scripts/rebuild-rpi4b-fast.sh ...` already reached make.
# Naming it in the same allowlist as the other build knobs just makes that
# explicit rather than incidental.
#
# What this block is really for:
#   1. `touch misc/init.c` -- a -D change does NOT invalidate cached objects, so
#      without this the knob flips and the build reuses an init.o compiled the
#      other way, exiting 0. That trap is on record (and it bit in reverse when
#      the trace was first REMOVED).
#   2. The banner -- this build prints per-process trace lines and must not ship.
# Confirm with `strings <binary> | grep libc-init`, never the exit code, and
# mind the path: `strings` on a MISSING file also greps clean, which cost three
# bogus "0" readings on 2026-09-11 (libphoenix.a lives in _build/<t>/lib/ and
# _build/<t>/sysroot/lib/, NOT _build/<t>/libphoenix/).
libc_trace_env=""
# NOT under ${buildroot}: prepare-buildroot.sh runs BEFORE this block and wipes the
# buildroot root, taking the stamp with it -- so `cat` fell back to "n", matched want="n",
# and a y->n transition was never detected. That is how a traced build survived an
# un-tracing rebuild on 2026-09-12 (the third distinct way this net has failed).
libc_trace_stamp="${repo_root}/artifacts/.libc-startup-trace-state"
libc_trace_want="${LIBC_STARTUP_TRACE:-n}"
if [ "${libc_trace_want}" = "y" ]; then
	libc_trace_env="LIBC_STARTUP_TRACE='y' "
	printf 'Diagnostic: LIBC_STARTUP_TRACE=y (all 8 pre-main markers ON -- do not ship this build)\n'
elif [ "${libc_trace_want}" = "min" ]; then
	# One marker instead of eight: same answer to "did it reach _libc_init at all?"
	# with an eighth of the timing perturbation.
	libc_trace_env="LIBC_STARTUP_TRACE_MIN='y' "
	printf 'Diagnostic: LIBC_STARTUP_TRACE=min (entry marker only -- do not ship this build)\n'
fi
# Touch the guarded source whenever the knob CHANGES STATE, in either direction. Turning a
# -D off does not invalidate the objects it changed any more than turning it on does, so
# without this a build that drops the flag happily relinks the traced misc/init.o and exits
# 0 -- i.e. silently SHIPS A DIAGNOSTIC BUILD. That is the more dangerous direction, and the
# earlier version of this block only handled the "on" case.
if [ "$(cat "${libc_trace_stamp}" 2>/dev/null || echo n)" != "${libc_trace_want}" ]; then
	printf 'LIBC_STARTUP_TRACE changed (%s -> %s): deleting init.o + libphoenix.a to force a rebuild\n' \
		"$(cat "${libc_trace_stamp}" 2>/dev/null || echo n)" "${libc_trace_want}"
	# DELETE the artefacts, do not touch the source. Measured 2026-09-12: a touch is not
	# enough -- the buildroot's init.c was already NEWER than init.o (00:26 vs 21:54) and the
	# core stage still did not recompile it, so the knob flipped with no effect while this
	# block printed a reassuring message. Removing the object and the archive leaves make no
	# choice. (Touching the sibling source is doubly useless here: prepare-buildroot has
	# already copied it by the time this block runs.)
	# ONLY the object. Deleting libphoenix.a itself breaks the build: libc.a, libm.a and
	# libpthread.a are SYMLINKS to it, so removing it leaves dangling links and every port
	# link then fails with "cannot find .../sysroot/lib/libpthread.a" (done 2026-09-12,
	# it took a --scope core run to repair). Removing the object is enough -- make
	# recompiles it and re-archives libphoenix.a in place, symlinks intact.
	rm -f "${buildroot}/_build/${target}/libphoenix/misc/init.o" 2>/dev/null || true
	mkdir -p "${repo_root}/artifacts" 2>/dev/null || true
	printf '%s' "${libc_trace_want}" > "${libc_trace_stamp}" 2>/dev/null || true
fi

# LIBC_DIAG: diagnostic -D flags for libphoenix only (see its Makefile). Same
# knob-change hazard and the same remedy as the kernel's KERNEL_DIAG below:
# turning a -D off does not invalidate the objects it changed, so the whole
# libphoenix object tree is deleted whenever the flag changes state.
libc_diag_env=""
libc_diag_stamp="${repo_root}/artifacts/.libc-diag-state"
libc_diag_want="${LIBC_DIAG:-}"
if [ -n "${libc_diag_want}" ]; then
	libc_diag_env="LIBC_DIAG='${libc_diag_want}' "
	printf 'Diagnostic: LIBC_DIAG=%s (do not ship this build)\n' "${libc_diag_want}"
fi
if [ "$(cat "${libc_diag_stamp}" 2>/dev/null || true)" != "${libc_diag_want}" ]; then
	printf 'LIBC_DIAG changed (%s -> %s): deleting libphoenix objects to force a rebuild\n' \
		"$(cat "${libc_diag_stamp}" 2>/dev/null || echo '<none>')" "${libc_diag_want:-<none>}"
	# Objects only -- libc.a/libm.a/libpthread.a are SYMLINKS to libphoenix.a and
	# deleting the archive leaves dangling links that break every port link.
	find "${buildroot}/_build/${target}/libphoenix" -name '*.o' -delete 2>/dev/null || true
	mkdir -p "${repo_root}/artifacts" 2>/dev/null || true
	printf '%s' "${libc_diag_want}" > "${libc_diag_stamp}" 2>/dev/null || true
fi

# FS_DIAG: diagnostic -D flags for the filesystem servers only. Same knob-change
# hazard and remedy as the others: turning a -D off does not invalidate the
# objects it changed, so the nfs objects are deleted on any change of state.
fs_diag_env=""
fs_diag_stamp="${repo_root}/artifacts/.fs-diag-state"
fs_diag_want="${FS_DIAG:-}"
if [ -n "${fs_diag_want}" ]; then
	fs_diag_env="FS_DIAG='${fs_diag_want}' "
	printf 'Diagnostic: FS_DIAG=%s (do not ship this build)\n' "${fs_diag_want}"
fi
if [ "$(cat "${fs_diag_stamp}" 2>/dev/null || true)" != "${fs_diag_want}" ]; then
	printf 'FS_DIAG changed (%s -> %s): deleting filesystem objects to force a rebuild\n' \
		"$(cat "${fs_diag_stamp}" 2>/dev/null || echo '<none>')" "${fs_diag_want:-<none>}"
	rm -rf "${buildroot}/_build/${target}/phoenix-rtos-filesystems" 2>/dev/null || true
	mkdir -p "${repo_root}/artifacts" 2>/dev/null || true
	printf '%s' "${fs_diag_want}" > "${fs_diag_stamp}" 2>/dev/null || true
fi

# KERNEL_DIAG: diagnostic -D flags for the kernel only (see the kernel Makefile).
# Same knob-change hazard as LIBC_STARTUP_TRACE above and the same remedy: turning a
# -D OFF does not invalidate the objects it changed, so without this a build that
# drops the flag happily relinks the instrumented objects and ships a diagnostic
# kernel while printing nothing. Delete the kernel objects on ANY change of state,
# in either direction.
kernel_diag_env=""
kernel_diag_stamp="${repo_root}/artifacts/.kernel-diag-state"
kernel_diag_want="${KERNEL_DIAG:-}"
if [ -n "${kernel_diag_want}" ]; then
	kernel_diag_env="KERNEL_DIAG='${kernel_diag_want}' "
	printf 'Diagnostic: KERNEL_DIAG=%s (do not ship this build)\n' "${kernel_diag_want}"
fi
if [ "$(cat "${kernel_diag_stamp}" 2>/dev/null || true)" != "${kernel_diag_want}" ]; then
	printf 'KERNEL_DIAG changed (%s -> %s): deleting kernel objects to force a rebuild\n' \
		"$(cat "${kernel_diag_stamp}" 2>/dev/null || echo '<none>')" \
		"${kernel_diag_want:-<none>}"
	rm -rf "${buildroot}/_build/${target}/phoenix-rtos-kernel" 2>/dev/null || true
	mkdir -p "${repo_root}/artifacts" 2>/dev/null || true
	printf '%s' "${kernel_diag_want}" > "${kernel_diag_stamp}" 2>/dev/null || true
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
# Check what actually came out, instead of trusting the forcing logic above. That logic has
# now failed THREE distinct ways (a touch that did not invalidate the object; deleting
# libphoenix.a and breaking the libc/libm/libpthread symlinks; a stamp under the buildroot
# that prepare-buildroot wiped). A build that silently keeps the pre-main trace ships
# per-process diagnostics, so verify the artifact and say so loudly either way.
verify_libc_trace_state() {
	local lib="${buildroot}/_build/${target}/sysroot/lib/libphoenix.a"
	local found

	[ -f "${lib}" ] || return 0
	found=$(strings "${lib}" 2>/dev/null | grep -c 'libc-init' || true)

	# ⚠ Both branches were WARNINGs until 2026-09-17: the build printed
	# "DO NOT SHIP THIS BUILD" and then went on to cut the image and exit 0.
	# A check that can only warn cannot fail, and this one guards what the
	# artifact actually contains. Both conditions mean the same thing -- the
	# knob did not take -- so both now stop the build.
	if [ "${libc_trace_want}" = "y" ] && [ "${found}" = "0" ]; then
		printf '\n*** FAIL: LIBC_STARTUP_TRACE=y but libphoenix.a carries NO trace markers.\n'
		printf '***       The knob did not take, so the trace you asked for is not in this\n'
		printf '***       build. Delete _build/%s/libphoenix/misc/init.o and rebuild.\n\n' "${target}"
		exit 1
	elif [ "${libc_trace_want}" != "y" ] && [ "${found}" != "0" ]; then
		printf '\n*** FAIL: the pre-main startup trace is STILL COMPILED IN (%s markers) with the\n' "${found}"
		printf '***       knob OFF. DO NOT SHIP THIS BUILD. Delete _build/%s/libphoenix/misc/init.o\n' "${target}"
		printf '***       and rebuild --scope core, then re-check.\n\n'
		exit 1
	fi
}

run_build_stages() {
	local stages="$*"
	printf 'Build:     ./phoenix-rtos-build/build.sh %s\n' "${stages}"
	run_build_shell \
		"set -euo pipefail; export PATH='${repo_root}/.venv/bin':'${toolchain_path}':\$PATH; cd '${buildroot}'; env ${with_tests_env}${log_to_file_env}${libc_trace_env}${libc_diag_env}${fs_diag_env}${kernel_diag_env}RPI4B_DTB_PATH='${dtb_path}' RPI4B_VARIANT='${variant}' TARGET='${target}' ./phoenix-rtos-build/build.sh ${stages}"
}

# The CORE stage regenerates the sysroot, so this is the one moment where the
# toolchain's BUNDLED libc copy can be refreshed from a generated artifact
# instead of by hand. It matters because some things still compile with no
# --sysroot and therefore resolve libc out of that bundle: the standalone
# radio/probe tools (tools/wifi-probe, tools/bt-probe) and the ports whose own
# build systems replace the framework's flags (python3, redis: the 2026-10-01
# build 9 shipped both with the PREVIOUS libphoenix -- the fork fix in every other
# binary, not in the one it was written for -- because the sync then ran after
# the whole build). A hand-maintained copy goes stale silently, and the measured
# consequence was five macro VALUES disagreeing with live libphoenix, two pairs
# swapped (docs/misc/2026-09-04-toolchain-header-skew.md).
#
# Not fatal on failure: a stale bundle is a hazard, not a broken build, and
# the check is also available standalone (--check reports drift only).
sync_toolchain_bundle() {
	"${repo_root}/scripts/sync-toolchain-from-sysroot.sh" ||
		printf 'WARNING: toolchain bundle sync failed; bare-toolchain builds may see a stale libc\n' >&2
}

# build.sh runs its stages in a fixed order, so a stage list containing `core`
# is split in two: everything up to and including `core`, the bundle sync, then
# the rest (ports, test, project, image) -- which then link the libc just built.
run_phoenix_build() {
	local first=() rest=() arg
	for arg in "$@"; do
		case "${arg}" in
		clean | host | fs | core) first+=("${arg}") ;;
		*) rest+=("${arg}") ;;
		esac
	done

	case " ${first[*]} " in
	*" core "*)
		run_build_stages "${first[@]}"
		sync_toolchain_bundle
		[ "${#rest[@]}" -eq 0 ] || run_build_stages "${rest[@]}"
		;;
	*)
		run_build_stages "$@"
		;;
	esac

	verify_libc_trace_state
}

run_phoenix_build "${build_args[@]}"

# Record WHICH COMMIT of every Phoenix repo produced these binaries, into the
# staged rootfs, so rpi4-sysinfo can print it at boot (owner request 2026-09-05:
# a UART log should say what the system IS, not only what it did). Written after
# the build so the `fs` stage cannot overwrite it, and before the SD/ext2 image
# is assembled from the same tree in the artifact tail. Non-fatal: a missing
# component list is a diagnostic gap, not a broken build.
"${repo_root}/scripts/gen-build-versions.sh" ||
	printf 'WARNING: could not record component commit ids; the boot banner will say so\n' >&2

# Report ABI staleness in the staged rootfs while the build log is still in front
# of you. It stays green by construction now that binary.mk relinks every program
# when libphoenix changes -- but that rule is exactly the kind of thing a future
# refactor drops silently, and on 2026-09-04 this gate was what caught 119 of 336
# ELFs still linked against the previous libc after a full `--scope core` build.
#
# Reported, NOT enforced: a flagged file is not automatically a defect. The sd
# variant legitimately keeps a /sbin/nfs from an earlier nfsroot cut (build.project
# builds the NFS server only for nfsroot/netboot), and the hand-built radio/X11
# helpers have their own build scripts. Print the verdict and let the operator
# judge -- the check's own header explains how.
if [ -x "${repo_root}/scripts/check-no-stale-binaries.sh" ]; then
	printf '\n== ABI staleness of the staged rootfs ==\n'
	"${repo_root}/scripts/check-no-stale-binaries.sh" \
		--root "${buildroot}/_fs/${target}/root" 2>&1 | tail -n 12 || true
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
	printf 'Showcase:  staging the helper programs into the rootfs (phase stage)\n'
	SHOWCASE_STAGE_DIR="${buildroot}/_fs/${target}/root" \
		RPI4B_BUILDROOT="${buildroot}" RPI4B_WITH_TESTS="${with_tests}" \
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
	# this only enlarges partition 2.
	#
	# The size is NO LONGER keyed off --with-showcase. It used to be
	# (262144 blocks, or 1572864 with the flag), which tied the volume size to a
	# build flag while the CONTENT came from whatever was already staged in
	# _fs/<target>/root. Re-cutting an image with `--scope project --variant sd`
	# -- reasonable when nothing needs rebuilding -- therefore picked 256 MiB for
	# a 682 MB showcase rootfs and mke2fs failed with "Could not allocate block
	# in ext2 filesystem". build-rpi4b-rootfs-ext2.sh now measures the staged
	# tree itself; pass RPI4B_ROOTFS_BLOCKS to override.
	env RPI4B_BUILDROOT="${buildroot}" \
		${RPI4B_ROOTFS_BLOCKS:+RPI4B_ROOTFS_BLOCKS="${RPI4B_ROOTFS_BLOCKS}"} \
		"${repo_root}/scripts/build-rpi4b-rootfs-ext2.sh"
	RPI4B_REMOTE_SDIMG="${two_part_img}" \
		RPI4B_EXPORT_SDIMG_PATH="${exported_two_part}" \
		"${repo_root}/scripts/export-rpi4b-sdimg.sh"
	exported_sha="$(shasum -a 256 "${exported_two_part}" | awk '{print $1}')"
	RPI4B_SDIMG_PATH="${exported_two_part}" \
		"${repo_root}/scripts/verify-rpi4b-sdimg.sh"

	# ...and gate the CONTENTS, not just the integrity. verify-rpi4b-sdimg.sh above
	# only answers "did this image copy correctly"; it cannot see that the FAT boot
	# partition is missing kernel8.img, or that config.txt names a file that is not
	# there. That matters more for `sd` than for any other variant, because SD boot
	# is the one path this bench cannot test (no card in the host reader, none in
	# the Pi) -- the owner is the first person to boot it. An uninvoked gate would
	# have been uninvoked on the day it mattered, so it runs here, in the build.
	#
	# The showcase expectation comes from the STAGED TREE, deliberately not from the
	# image: deriving it from the image would let a staging failure (tree has
	# Xorg-drm, image does not) silently downgrade itself to a SKIP. Two independent
	# sources means a disagreement still fails. Note this is NOT `${with_showcase}`
	# -- a `--scope project --variant sd` re-cut has that flag at 0 while re-packing
	# a fully staged showcase rootfs, which is exactly how the demo image is re-cut.
	# The X server marks a showcase tree.
	if [ -e "${buildroot}/_fs/aarch64a72-generic-rpi4b/root/bin/Xorg-drm" ]; then
		contents_expect=showcase
	else
		contents_expect=base
	fi
	"${repo_root}/scripts/verify-sd-image-contents.sh" \
		--expect "${contents_expect}" "${exported_two_part}"

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
