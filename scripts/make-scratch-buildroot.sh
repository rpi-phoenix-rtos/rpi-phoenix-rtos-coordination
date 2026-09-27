#!/usr/bin/env bash
#
# make-scratch-buildroot.sh — create a SCRATCH buildroot for building ports outside
# the image's own .buildroot, e.g. to verify a new or migrated recipe with
# scripts/build-port.sh without touching the image build's shared ports prefix
# (_build/<target>/{lib,include}), its port-sources, its .port_state files or the
# staged rootfs (_fs/<target>/root):
#
#   scripts/make-scratch-buildroot.sh .buildroot-newlane
#   RPI4B_BUILDROOT=$PWD/.buildroot-newlane scripts/build-port.sh dbus
#
# The scratch tree links the build scripts, project and target definitions of the
# real buildroot (read-only use) and holds a COPY of the libphoenix sysroot, so a
# core rebuild running meanwhile cannot change the scratch build's libc halfway. The
# ports prefix starts empty: port_manager builds the whole dependency closure of the
# requested ports from scratch (tarballs come from the ports tree / distfiles cache).
#
# Re-running on an existing scratch tree refreshes only the sysroot copy. Delete the
# directory to drop it.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
live="${RPI4B_LIVE_BUILDROOT:-${repo_root}/.buildroot}"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"

if [ "$#" -ne 1 ]; then
	echo "usage: $0 <scratch-buildroot-dir>" >&2
	exit 2
fi
scratch="$1"
case "${scratch}" in /*) ;; *) scratch="${PWD}/${scratch}" ;; esac
[ "$(realpath -m "${scratch}")" != "$(realpath "${live}")" ] || { echo "$0: refusing to use the live buildroot" >&2; exit 1; }
[ -f "${live}/build.project" ] && [ -d "${live}/_build/${target}/sysroot" ] ||
	{ echo "$0: ${live} is not a built buildroot for ${target}" >&2; exit 1; }

mkdir -p "${scratch}/_build/${target}" "${scratch}/_fs/${target}/root" "${scratch}/_boot/${target}"
for f in phoenix-rtos-build build.project _projects _targets; do
	ln -sfn "${live}/${f}" "${scratch}/${f}"
done
rm -rf "${scratch}/_build/${target}/sysroot"
cp -a "${live}/_build/${target}/sysroot" "${scratch}/_build/${target}/sysroot"
echo "scratch buildroot: ${scratch}"
echo "  sysroot copy: libphoenix.a $(sha256sum "${scratch}/_build/${target}/sysroot/lib/libphoenix.a" | cut -c1-16)"
