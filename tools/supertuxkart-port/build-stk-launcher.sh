#!/usr/bin/env bash
# Build the SuperTuxKart launcher (stk-launcher.c) into a static aarch64-phoenix
# ELF and install it as /bin/stk in the base rootfs (_fs) + the netboot NFS
# export. The SuperTuxKart *engine* is built by the ports framework
# (scripts/build-port.sh supertuxkart → /usr/bin/supertuxkart); this launcher is
# a separate runtime concern (env-var wiring), exactly like the Quake launchers.
#
# The engine ELF is built with the .toolchain gcc16 cross-compiler; this launcher
# uses the same toolchain so the two are ABI-consistent. Static link (no PT_INTERP)
# — Phoenix has no dynamic loader for ordinary programs.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
toolchain="${PHOENIX_AARCH64_TOOLCHAIN:-${repo_root}/.toolchain/aarch64-phoenix/bin}"
cc="${toolchain}/aarch64-phoenix-gcc"

src="${repo_root}/tools/supertuxkart-port/stk-launcher.c"
out="$(mktemp -d)/stk"

fs_bin="${repo_root}/.buildroot/_fs/${target}/root/bin"

# Netboot NFS export served as "/" (fsid=0 in /etc/exports); detect it rather
# than hardcode, matching sync-netboot-tree.sh.
fsid0_export="$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// { print $1; exit }' /etc/exports 2>/dev/null || true)"
export_dir="${RPI4B_NFS_EXPORT:-${fsid0_export:-/srv/phoenix-rpi4-nfs}}"

echo "stk-launcher: cc = ${cc}"
"${cc}" -O2 -static -Wall -Wextra -o "${out}" "${src}"

echo "stk-launcher: verifying artifact"
file "${out}"
if readelf -l "${out}" 2>/dev/null | grep -q INTERP; then
	echo "stk-launcher: ERROR — PT_INTERP present (not a static ELF)" >&2
	exit 1
fi

install -Dm755 "${out}" "${fs_bin}/stk"
echo "stk-launcher: installed -> ${fs_bin}/stk"

if [ -d "${export_dir}" ]; then
	install -Dm755 "${out}" "${export_dir}/bin/stk"
	echo "stk-launcher: installed -> ${export_dir}/bin/stk"
else
	echo "stk-launcher: NFS export ${export_dir} not present — skipped export install" >&2
fi
