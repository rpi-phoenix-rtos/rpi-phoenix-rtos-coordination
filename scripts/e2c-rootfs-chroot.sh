#!/usr/bin/env bash
#
# e2c-rootfs-chroot.sh — run a command inside the Raspberry Pi OS netboot rootfs
# (artifacts/linux-netboot/rootfs) on the HOST, through qemu-aarch64 binfmt.
#
# Used to install packages into the Linux reference rootfs for experiment E2c
# (docs/gpu-new-lane/E2c-pios-baseline.md) without booting the Pi.
#
# What it does around the command, and undoes on every exit path:
#   * binds /proc /sys /dev /dev/pts into the rootfs;
#   * binds the HOST's /etc/resolv.conf over the rootfs's (the rootfs file points
#     at the WiFi-lane gateway 10.43.0.1, unreachable from the host) — a bind, so
#     the rootfs file itself is never edited;
#   * drops usr/sbin/policy-rc.d (exit 101) so no postinst starts a service.
#
# Needs: passwordless sudo, qemu-user-binfmt with the F (fix-binary) flag for
# aarch64 (check: cat /proc/sys/fs/binfmt_misc/qemu-aarch64 | grep flags).
#
# Usage: ./scripts/e2c-rootfs-chroot.sh <cmd> [args...]
#   e.g. ./scripts/e2c-rootfs-chroot.sh apt-get update
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
root="${E2C_ROOTFS:-$repo/artifacts/linux-netboot/rootfs}"

[ "$#" -gt 0 ] || { echo "usage: $0 <cmd> [args...]" >&2; exit 2; }
[ -x "$root/usr/bin/dpkg" ] || { echo "no rootfs at $root" >&2; exit 2; }
grep -q '^flags:.*F' /proc/sys/fs/binfmt_misc/qemu-aarch64 2>/dev/null \
	|| { echo "qemu-aarch64 binfmt with the F flag is not registered (install qemu-user-binfmt)" >&2; exit 2; }

mounted=()
cleanup() {
	local i
	sudo -n rm -f "$root/usr/sbin/policy-rc.d"
	for (( i=${#mounted[@]}-1; i>=0; i-- )); do
		sudo -n umount -l "${mounted[$i]}" 2>/dev/null || true
	done
}
trap cleanup EXIT INT TERM

bind() {
	local src="$1" dst="$root$2"
	if mountpoint -q "$dst"; then
		echo "refusing: $dst is already a mount point (a previous run left it mounted?)" >&2
		exit 3
	fi
	sudo -n mount --bind "$src" "$dst"
	mounted+=("$dst")
}

bind /proc /proc
bind /sys /sys
bind /dev /dev
bind /dev/pts /dev/pts
bind /etc/resolv.conf /etc/resolv.conf

printf '#!/bin/sh\nexit 101\n' | sudo -n tee "$root/usr/sbin/policy-rc.d" >/dev/null
sudo -n chmod 755 "$root/usr/sbin/policy-rc.d"

sudo -n env DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8 LANG=C.UTF-8 \
	PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
	chroot "$root" "$@"
