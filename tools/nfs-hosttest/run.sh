#!/usr/bin/env bash
# Host test of the NFS server's read/write/reclaim handlers (no Pi, ~2 s).
#
# Compiles the real phoenix-rtos-filesystems nfs/nfs_ops.c + nfs_node.c +
# nfs_dir.c for the host, against a scripted in-memory libnfs (nfs-ops-io.c) and
# a few shim headers standing in for Phoenix's <sys/msg.h> & co. See
# nfs-ops-io.c for what it checks.
#
#   tools/nfs-hosttest/run.sh                       # nfs/ from sources/phoenix-rtos-filesystems
#   NFS_SRC=/path/to/worktree/nfs tools/nfs-hosttest/run.sh
#
# libnfs's public header comes from the port build's git clone with the port's
# patches applied (nfs_renew() is one of ours), like tools/libnfs-hosttest.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
nfs="${NFS_SRC:-$root/sources/phoenix-rtos-filesystems/nfs}"
ports="${PORTS:-$root/sources/phoenix-rtos-ports}"
clone="${LIBNFS_CLONE:-$root/.buildroot/_build/aarch64a72-generic-rpi4b/port-sources/libnfs-6.0.2/libnfs-libnfs-6.0.2}"
work="$(mktemp -d "${TMPDIR:-/tmp}/nfs-hosttest.XXXXXX")"
trap 'rm -rf "$work"' EXIT

if [ ! -d "$clone/.git" ]; then
	echo "NFS-OPS-IO: FAIL (no libnfs clone at $clone; build the libnfs port once, or set LIBNFS_CLONE)"
	exit 2
fi
mkdir -p "$work/src" "$work/inc/sys"
git -C "$clone" archive --format=tar libnfs-6.0.2 include | tar -x -C "$work/src"
for p in "$ports"/libnfs/patches/*.patch; do
	# Only the header hunks matter here; the lib/ files are not exported.
	patch -s -d "$work/src" -p1 --no-backup-if-mismatch -f < "$p" >/dev/null 2>&1 || true
done
cp "$root/sources/libphoenix/include/sys/rb.h" "$work/inc/sys/"

gcc -O2 -g -std=gnu99 -DEOK=0 -fsanitize=address,undefined -Wall -Wextra -Wno-unused-parameter \
	-I"$here/shim" -I"$work/inc" -I"$work/src/include" -I"$nfs" \
	"$here/nfs-ops-io.c" "$nfs/nfs_ops.c" "$nfs/nfs_node.c" "$nfs/nfs_dir.c" \
	"$root/sources/libphoenix/sys/rb.c" -o "$work/nfs-ops-io"
# The node table and the mock's handles are never freed; leaks are not the subject.
ASAN_OPTIONS=detect_leaks=0 "$work/nfs-ops-io"
