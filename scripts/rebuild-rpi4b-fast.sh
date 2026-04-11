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
  --build-only
      skip bootfs/sdimg export and verification
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

vm="${PHOENIX_VM:-phoenix-dev}"
buildroot="${RPI4B_BUILDROOT:-/home/witoldbolt.guest/phoenix-buildroots/phoenix-rtos-project-copy}"
toolchain_path="${PHOENIX_AARCH64_TOOLCHAIN:-/home/witoldbolt.guest/phoenix-toolchains/aarch64-phoenix/bin}"
dtb_path="${RPI4B_DTB_PATH:-/tmp/rpi4b-dtb/bcm2711-rpi-4-b.dtb}"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
scope="auto"
do_prepare=1
do_build_artifacts=1
do_qemu_sanity=0

while [ "$#" -gt 0 ]; do
	case "$1" in
		--scope)
			shift
			[ "$#" -gt 0 ] || die "missing value for --scope"
			scope="$1"
			;;
		--build-only)
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
		build_args=(clean host core project image)
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

printf 'VM: %s\n' "${vm}"
printf 'Buildroot: %s\n' "${buildroot}"
printf 'Target: %s\n' "${target}"
printf 'Scope: %s\n' "${scope}"
printf 'Build args: %s\n' "${build_args[*]}"
printf 'Reason: %s\n' "${scope_reason}"

if [ "${do_prepare}" -eq 1 ]; then
	limactl shell -y "${vm}" -- /bin/bash -lc \
		"cd '${repo_root}' && ./scripts/prepare-buildroot.sh --copy-components '${buildroot}'"
fi

build_args_str="${build_args[*]}"
limactl shell -y "${vm}" -- /bin/bash -lc \
	"set -euo pipefail; export PATH='${toolchain_path}':\$PATH; cd '${buildroot}'; env RPI4B_DTB_PATH='${dtb_path}' TARGET='${target}' ./phoenix-rtos-build/build.sh ${build_args_str}"

if [ "${do_qemu_sanity}" -eq 1 ]; then
	limactl shell -y "${vm}" -- /bin/bash -lc \
		"set -euo pipefail; cd '${buildroot}'; log=/tmp/pi4-direct-fast-helper.log; timeout 25s /home/witoldbolt.guest/tools/qemu-10.2.2/bin/qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -smp 4 -m 2G -nographic -monitor none -kernel _boot/${target}/plo.elf -device loader,file=_boot/${target}/rpi4b/loader.disk,addr=0x48000000,force-raw=on >\"\$log\" 2>&1 || true; grep -En 'call: exec go!|go: enter|hal: jump exit el1|A3|KLM|Exception #37' \"\$log\" || true"
fi

if [ "${do_build_artifacts}" -eq 0 ]; then
	exit 0
fi

"${repo_root}/scripts/assemble-rpi4b-bootfs.sh"
"${repo_root}/scripts/assemble-rpi4b-bootfs-img.sh"
"${repo_root}/scripts/assemble-rpi4b-sdimg.sh"
"${repo_root}/scripts/export-rpi4b-sdimg.sh"

exported_sha="$(shasum -a 256 "${repo_root}/artifacts/rpi4b/rpi4b-sd.img" | awk '{print $1}')"
"${repo_root}/scripts/verify-rpi4b-sdimg.sh"

printf 'Exported SHA256: %s\n' "${exported_sha}"
