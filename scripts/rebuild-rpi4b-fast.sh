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
      build + bundle the showcase-app layer (GPU/GL/Vulkan stack, Quake, X11
      server + apps, dillo/mc/nano) via scripts/build-showcase-apps.sh. Only
      meaningful with --variant sd (the apps live on the ext2 root). Runs the
      GPU archive builds before build.sh and stages X11/ports into the rootfs
      after. Adds host deps (meson/ninja/mako/libdrm-dev/glslang) — install via
      scripts/bootstrap-linux-host.sh.
  --build-only
      skip bootfs/sdimg export and verification
  --ports-only
      build ONLY the phoenix-rtos-ports `ports` stage (implies --with-ports
      and --build-only). Ports stage writes straight into the rootfs tree
      _fs/<target>/root (PREFIX_ROOTFS); it does NOT rebuild loader.disk or
      any core/project artifact. Use when staging ports onto an external
      rootfs (e.g. the NFS export) without touching the boot image.
  --skip-prepare
      do not refresh the copied VM-local buildroot first
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
ports_only=0
# --with-showcase: build the showcase-app layer (GPU/GL/Vulkan + Quake, X11
# server + apps, dillo/mc/nano) and bundle it into the image. Only meaningful
# for --variant sd (the ext2 root is where the apps live). Two-phase: the GPU
# archives are built BEFORE build.sh (the rpi4-quake/-vkquake _user components
# link them); the X11/ports binaries are staged into _fs/<target>/root AFTER
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
			# opt-in the WIP Vulkan/vkQuake path (implies --with-showcase); NOT in the
			# default showcase since vkQuake isn't functional yet.
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
		build_args=(project image)
		scope_reason="forced project scope"
		;;
	core)
		build_args=(core project image)
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
		build_args=(clean host core ports project image)
		scope_reason="forced full-clean scope"
		;;
	auto)
		if [ "${#dirty_full[@]}" -gt 0 ]; then
			build_args=(clean host core project image)
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
if [ "${variant}" = "sd" ] && [ "${ports_only}" = 0 ] && [ "${scope}" = "auto" ]; then
	# `host` builds the hostutils (metaelf/syspagen/mkrofs/...) that the image
	# stage needs; include it so the command is self-sufficient from a cold
	# buildroot (build.sh's clean wipes the host prefix too).
	build_args=(host fs core ports project image)
	scope_reason="sd variant: full stage list (host fs core ports project image) for a complete 2-part SD image"
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

if [ "${do_prepare}" -eq 1 ]; then
	run_build_shell "cd '${repo_root}' && ./scripts/prepare-buildroot.sh --copy-components '${buildroot}'"
fi

# --with-showcase, phase 1 (GPU): build the GPU/GL/Vulkan + Quake archives into
# tools/.gpu-libs BEFORE build.sh, so the _user rpi4-quake/-vkquake components
# link them and are staged into the image by the project stage. Skipped unless
# --with-showcase; only meaningful for --variant sd but harmless otherwise.
if [ "${with_showcase}" = 1 ]; then
	printf 'Showcase:  building GPU/Quake archives (phase gpu) before build.sh%s\n' \
		"$( [ "${with_vkquake}" = 1 ] && printf ' (+vkQuake WIP)' )"
	"${repo_root}/scripts/build-showcase-apps.sh" --phase gpu \
		$( [ "${with_vkquake}" = 1 ] && printf -- '--with-vkquake' )
fi

# GPU_LIBS: the rpi4-quake/-vkquake Makefiles compute this by climbing 4 levels
# from their own dir, which is correct for the sources/... tree but off-by-one
# for a repo-root-level .buildroot (the VM/publication layout). Pass it
# explicitly so the archives are always found when present. Harmless when the
# archives are absent (the Makefiles still skip the component gracefully).
gpu_libs_env="GPU_LIBS='${repo_root}/tools/.gpu-libs' "

# --with-showcase: tell the plo render (image_builder.py reads os.environ) to
# bundle the rpi4-quake blob into loader.disk (launch-free `app` line gated on
# RPI4B_WITH_SHOWCASE in user.plo.yaml). Only set it if the GLQuake archive was
# actually produced by the gpu phase — otherwise the _user Makefile skips
# rpi4-quake, the binary would be absent, and the plo `app rpi4-quake` line
# would fail at boot (brick). Unset otherwise, so the base image and the
# netboot/nfsroot variants are unchanged.
showcase_env=""
if [ "${with_showcase}" = 1 ] && [ -f "${repo_root}/tools/.gpu-libs/libquakespasm.a" ] \
		&& [ -f "${repo_root}/tools/.gpu-libs/libGL-phoenix.a" ] \
		&& [ -f "${repo_root}/tools/.gpu-libs/libv3d-phoenix.a" ]; then
	showcase_env="RPI4B_WITH_SHOWCASE='1' "
	printf 'Showcase:  bundling rpi4-quake into loader.disk (run it from psh; not auto-launched)\n'
elif [ "${with_showcase}" = 1 ]; then
	printf 'Showcase:  GLQuake archives absent after gpu phase; NOT bundling rpi4-quake\n' >&2
fi

build_args_str="${build_args[*]}"
# Task #31: pass RPI4_LOG_TO_FILE into the build env ONLY when the board macro is
# set, so the plo render (image_builder.py reads os.environ) gates the rpi4-klogd
# launch. In a DEBUG build the var stays unset and user.plo.yaml's
# `env.RPI4_LOG_TO_FILE | default('0')` resolves to '0' -> not launched.
log_to_file_env=""
if [ "${log_to_file}" = 1 ]; then
	log_to_file_env="RPI4_LOG_TO_FILE='1' "
fi
# Prepend the repo's uv venv bin so the build's bare `python3` (used by
# phoenix-rtos-build/build-ports.sh -> port_manager) finds resolvelib/jinja2/
# PyYAML/rich from the venv rather than the PEP668-managed system Python. A
# non-existent PATH entry is harmless, so this is safe even without the venv.
run_build_shell \
	"set -euo pipefail; export PATH='${repo_root}/.venv/bin':'${toolchain_path}':\$PATH; cd '${buildroot}'; env ${log_to_file_env}${gpu_libs_env}${showcase_env}RPI4B_DTB_PATH='${dtb_path}' RPI4B_VARIANT='${variant}' TARGET='${target}' ./phoenix-rtos-build/build.sh ${build_args_str}"

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
	# --with-showcase, phase 2 (stage): stage the port + X11 app binaries into the
	# rootfs tree (_fs/<target>/root) NOW — after build.sh finished populating it
	# (fs/core/ports/project) and BEFORE build-rpi4b-rootfs-ext2.sh packs it into
	# the ext2 image. Running earlier would be clobbered by build.sh's fs stage.
	# The showcase apps (large static X11 binaries + fonts + WindowMaker share +
	# mc skins) do not fit the default 256 MiB ext2 root; grow it to 768 MiB when
	# --with-showcase. The downstream partition geometry is computed from the
	# actual image size, so this only enlarges partition 2. Override with
	# RPI4B_ROOTFS_BLOCKS. (env is used because a shell VAR=val prefix cannot be
	# built conditionally from an array in command position.)
	rootfs_blocks="${RPI4B_ROOTFS_BLOCKS:-262144}"
	if [ "${with_showcase}" = 1 ]; then
		printf 'Showcase:  staging X11/ports app binaries into rootfs (phase stage)\n'
		SHOWCASE_STAGE_DIR="${buildroot}/_fs/${target}/root" \
			RPI4B_BUILDROOT="${buildroot}" \
			"${repo_root}/scripts/build-showcase-apps.sh" --phase stage \
			--stage-dir "${buildroot}/_fs/${target}/root"
		rootfs_blocks="${RPI4B_ROOTFS_BLOCKS:-786432}"
	fi
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
