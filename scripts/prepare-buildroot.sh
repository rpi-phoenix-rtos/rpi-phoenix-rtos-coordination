#!/usr/bin/env bash
#
# Prepare a disposable phoenix-rtos-project buildroot backed by sibling repos.
#

set -euo pipefail

usage() {
	cat <<EOF
Usage: $(basename "$0") [--link-components|--copy-components] [buildroot-dir]

Prepare a disposable local Phoenix buildroot that:
- copies the phoenix-rtos-project source tree
- keeps build artifacts out of the upstream working copy
- either links or copies the project component paths from sibling repos under sources/

Default buildroot:
  --link-components:
    writable repo checkout: <repo>/buildroots/phoenix-rtos-project
    read-only shared checkout: ~/phoenix-buildroots/phoenix-rtos-project
  --copy-components:
    writable repo checkout: <repo>/buildroots/phoenix-rtos-project-copy
    read-only shared checkout: ~/phoenix-buildroots/phoenix-rtos-project-copy
EOF
}

die() {
	printf "error: %s\n" "$*" >&2
	exit 1
}

components_mode="link"
buildroot=
using_default_buildroot=0

while [ "$#" -gt 0 ]; do
	case "$1" in
		-h|--help)
			usage
			exit 0
			;;
		--link-components)
			components_mode="link"
			;;
		--copy-components)
			components_mode="copy"
			;;
		-*)
			die "unknown option: $1"
			;;
		*)
			[ -z "${buildroot}" ] || die "too many positional arguments"
			buildroot="$1"
			;;
	esac
	shift
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
sources_dir="${repo_root}/sources"
project_src="${sources_dir}/phoenix-rtos-project"

if [ -n "${buildroot}" ]; then
	:
elif [ -w "${repo_root}" ]; then
	buildroot="${repo_root}/buildroots/phoenix-rtos-project"
	using_default_buildroot=1
else
	buildroot="${HOME}/phoenix-buildroots/phoenix-rtos-project"
	using_default_buildroot=1
fi

if [ "${components_mode}" = "copy" ] && [ "${using_default_buildroot}" -eq 1 ]; then
	buildroot="${buildroot%/}-copy"
fi

[ -d "${sources_dir}" ] || die "missing sources directory: ${sources_dir}"
[ -d "${project_src}" ] || die "missing phoenix-rtos-project clone: ${project_src}"
[ -f "${project_src}/.gitmodules" ] || die "missing .gitmodules in ${project_src}"

submodule_paths=()
while IFS= read -r path; do
	submodule_paths+=("${path}")
done < <(
	git -C "${project_src}" config -f .gitmodules --get-regexp '^submodule\..*\.path$' | awk '{print $2}' | sort
)

[ "${#submodule_paths[@]}" -gt 0 ] || die "no submodule paths found in ${project_src}/.gitmodules"

for path in "${submodule_paths[@]}"; do
	[ -d "${sources_dir}/${path}" ] || die "missing sibling repository for ${path}: ${sources_dir}/${path}"
done

mkdir -p "${buildroot}"

# _fs holds BOTH tracked source and build output, so it needs a filter, not a
# blanket rule:
#
#  * _fs/root-skel/ is SOURCE (22 tracked files -- /etc/{passwd,group,inittab,
#    lighttpd.conf,...}). It MUST be copied: PREFIX_ROOTSKEL points at
#    $PREFIX_PROJECT/_fs/root-skel/, and build.sh's fs stage is silently SKIPPED
#    when that directory is absent (`[ -d "${PREFIX_ROOTSKEL}" ]`). Excluding all
#    of _fs therefore broke every COLD build: the clean-room Docker build failed at
#    `lighttpd: lighttpd.conf not found in rootfs or root-skel`, while warm local
#    buildroots kept working because they already had the skeleton.
#  * _fs/<target>/root is the ~650 MB staged rootfs (core binaries, every port, all
#    five games + their data) -- build output like _build and _boot. The project
#    source has none, so a blanket --delete WIPED it on every prepare and only a
#    full ports+project rebuild could put it back. That is the recurring "the build
#    tree no longer holds the ports/games" state (weekly log 2026-09-04).
#
# So: include _fs itself and root-skel recursively, exclude everything else under
# it. Excluded paths are also protected from --delete (rsync does not delete
# excluded files without --delete-excluded), which is what preserves the staged
# rootfs. The include rules must precede the exclude that would otherwise swallow
# them -- rsync applies the first matching filter rule.
rsync_args=(
	-a
	--delete
	--exclude
	.git
	--exclude
	_build
	--exclude
	_boot
	--include
	/_fs/
	--include
	/_fs/root-skel/***
	--exclude
	/_fs/*
)

for path in "${submodule_paths[@]}"; do
	rsync_args+=(--exclude "${path}")
done

rsync "${rsync_args[@]}" "${project_src}/" "${buildroot}/"

for path in "${submodule_paths[@]}"; do
	dst="${buildroot}/${path}"
	rm -rf "${dst}"
	mkdir -p "$(dirname "${dst}")"
	if [ "${components_mode}" = "copy" ]; then
		rsync -a --delete --exclude .git --exclude _build --exclude _boot \
			"${sources_dir}/${path}/" "${dst}/"
	else
		ln -sfn "${sources_dir}/${path}" "${dst}"
	fi
done

mkdir -p "${buildroot}/_build" "${buildroot}/_boot"

printf "Prepared buildroot: %s\n" "${buildroot}"
printf "Project source: %s\n" "${project_src}"
if [ "${components_mode}" = "copy" ]; then
	printf "Copied component paths:\n"
else
	printf "Linked component paths:\n"
fi
for path in "${submodule_paths[@]}"; do
	if [ "${components_mode}" = "copy" ]; then
		printf "  %s <= %s\n" "${path}" "${sources_dir}/${path}"
	else
		printf "  %s -> %s\n" "${path}" "$(readlink "${buildroot}/${path}")"
	fi
done
