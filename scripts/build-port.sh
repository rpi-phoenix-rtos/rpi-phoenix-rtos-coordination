#!/usr/bin/env bash
#
# build-port.sh — build one or more phoenix-rtos-ports framework ports for
# aarch64a72-rpi4b via the real port_manager, standalone.
#
# Generic sibling of build-sdl2-port.sh / build-xorg-ports.sh: it reproduces the
# exact port-build environment build.sh sets up, then drives port_manager on a
# one-off ports.yaml listing the ports named on the command line, pointed at the
# canonical sources/phoenix-rtos-ports tree (where the ports + patches + overlays
# live and are edited). port_manager resolves the dependency order via resolvelib,
# so listing only the top-level target(s) is enough — declared `depends` are
# pulled and built first.
#
# Use it to build-verify a newly migrated tools/ -> ports/ port (the owner's
# "move ports tools/->ports project" finalization) before wiring it into any
# ports.yaml. Ports not yet listed in a target ports.yaml (`if: false` or absent)
# are skipped by the normal `build.sh ports` stage; this helper builds them
# regardless.
#
#   scripts/build-port.sh libpng libjpeg          # clean build (default)
#   scripts/build-port.sh --incremental ncurses   # skip the clean re-extract
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
toolchain_path="${PHOENIX_AARCH64_TOOLCHAIN:-${repo_root}/.toolchain/aarch64-phoenix/bin}"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
ports_dir="${repo_root}/sources/phoenix-rtos-ports"
venv_python="${repo_root}/.venv/bin/python3"

clean=1
if [ "${1:-}" = "--incremental" ]; then
	clean=0
	shift
fi

if [ "$#" -eq 0 ]; then
	echo "usage: $0 [--incremental] <port-name> [<port-name>...]" >&2
	exit 2
fi
ports=("$@")

export PATH="${toolchain_path}:${repo_root}/.venv/bin:${PATH}"

cd "${buildroot}"

# --- Replicate build.sh's port-build environment (build.sh lines ~19-83) ---
# shellcheck disable=SC1091
source ./phoenix-rtos-build/build.subr

export TARGET="${target}"
PREFIX_PROJECT="$(pwd)"
_TARGET_FOR_HOST_BUILD="host-generic-pc"
PREFIX_BUILD="${PREFIX_PROJECT}/_build/${TARGET}"
PREFIX_BUILD_HOST="${PREFIX_PROJECT}/_build/${_TARGET_FOR_HOST_BUILD}"
PREFIX_FS="${PREFIX_PROJECT}/_fs/${TARGET}"
PREFIX_BOOT="${PREFIX_PROJECT}/_boot/${TARGET}"
PREFIX_BUILD_VERSIONED="${PREFIX_BUILD}/versioned-ports/"
PREFIX_PROG="${PREFIX_BUILD}/prog/"
PREFIX_PROG_STRIPPED="${PREFIX_BUILD}/prog.stripped/"
PREFIX_A="${PREFIX_BUILD}/lib/"
PREFIX_H="${PREFIX_BUILD}/include/"
PREFIX_SYSROOT=""
PLO_SCRIPT_DIR="${PREFIX_BUILD}/plo-scripts"
PREFIX_ROOTFS="${PREFIX_FS}/root/"

export TARGET TARGET_FAMILY TARGET_SUBFAMILY TARGET_PROJECT PROJECT_PATH PREFIX_PROJECT PREFIX_BUILD \
	PREFIX_BUILD_HOST PREFIX_FS PREFIX_BOOT PREFIX_PROG PREFIX_PROG_STRIPPED PREFIX_A \
	PREFIX_H PREFIX_ROOTFS CROSS CFLAGS CXXFLAGS LDFLAGS CC LD AR AS MAKEFLAGS DEVICE_FLAGS PLO_SCRIPT_DIR \
	PREFIX_SYSROOT LIBPHOENIX_DEVEL_MODE PREFIX_BUILD_VERSIONED

# shellcheck disable=SC1091
source ./build.project

: "${LIBPHOENIX_DEVEL_MODE:=y}"
if [ "${LIBPHOENIX_DEVEL_MODE}" = "y" ]; then
	PREFIX_SYSROOT="${PREFIX_BUILD}/sysroot"
fi

CC=${CROSS}gcc
AS=${CROSS}as
LD=${CROSS}ld
AR=${CROSS}ar
MAKEFLAGS="--no-print-directory -j 9"

EXPORT_CFLAGS="$(make -f phoenix-rtos-build/Makefile.common export-cflags)"
EXPORT_CXXFLAGS="$(make -f phoenix-rtos-build/Makefile.common export-cxxflags)"
EXPORT_LDFLAGS="$(make -f phoenix-rtos-build/Makefile.common export-ldflags)"
EXPORT_STRIP="$(make -f phoenix-rtos-build/Makefile.common export-strip)"
export EXPORT_CFLAGS EXPORT_CXXFLAGS EXPORT_LDFLAGS EXPORT_STRIP

# --- Optional clean: force a full re-extract / re-patch / re-configure ---
if [ "${clean}" = 1 ]; then
	echo ">> clean: removing extracted port sources + build state for: ${ports[*]}"
	for p in "${ports[@]}"; do
		rm -rf "${PREFIX_BUILD}/port-sources/${p}-"* 2>/dev/null || true
		rm -f "${PREFIX_BUILD}/.port_state/${p}-"*.json 2>/dev/null || true
	done
fi

# --- One-off ports.yaml listing the requested ports (deps resolved by port_manager) ---
tmp_yaml="$(mktemp /tmp/build-port.XXXXXX.yaml)"
trap 'rm -f "${tmp_yaml}"' EXIT
{
	echo 'ports:'
	for p in "${ports[@]}"; do
		echo "  - name: ${p}"
	done
} > "${tmp_yaml}"

echo ">> building ports [${ports[*]}] (ports_dir=${ports_dir})"
GIT_DESC="$(cd ./phoenix-rtos-build && git describe --tags --abbrev=0 --match 'v[[:digit:]].[[:digit:]]*.[[:digit:]]*' 2>/dev/null || echo 'v3.3.1-0-g')"
cd "${PREFIX_PROJECT}/phoenix-rtos-build/"
PHOENIX_VER="${GIT_DESC}" "${venv_python}" ./port_manager.py build "${tmp_yaml}" "${ports_dir}"

echo ">> done."
