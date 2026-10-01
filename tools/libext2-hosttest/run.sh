#!/usr/bin/env bash
# Run the real libext2 against a file-backed device on the host, then let
# e2fsck judge it. Seconds, not a Pi cycle. Default block size 1024 matches
# the SD root (the lane where fstBlock == 1 and runs are longest).
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
# LIBEXT2_SRC: test an ext2/ directory other than the sibling checkout
# (e.g. a filesystems worktree on a feature branch).
E="${LIBEXT2_SRC:-$root/sources/phoenix-rtos-filesystems/ext2}"
blocksz="${1:-1024}"
img="$here/ext2-${blocksz}.img"

gcc -O1 -g -fsanitize=address,undefined -DEOK=0 \
    -I "$here/shim" -I "$E" \
    "$here/harness.c" \
    "$E"/sb.c "$E"/gdt.c "$E"/inode.c "$E"/block.c "$E"/dir.c "$E"/obj.c "$E"/file.c "$E"/ext2.c \
    "$root/sources/libphoenix/sys/list.c" "$root/sources/libphoenix/sys/rb.c" \
    "$here/shim/attrstub.c" \
    -o "$here/libext2-host" || { echo "BUILD FAILED"; exit 2; }

rm -f "$img"
dd if=/dev/zero of="$img" bs=1M count=64 status=none
mke2fs -q -t ext2 -b "$blocksz" -I 128 -F "$img" >/dev/null 2>&1 || { echo "mke2fs FAILED"; exit 2; }

echo "=== libext2 on the host (block size $blocksz) ==="
ASAN_OPTIONS=detect_leaks=0 "$here/libext2-host" "$img" "$blocksz"
rc=$?

echo
echo "=== e2fsck -fn (the oracle) ==="
e2fsck -fn "$img"
fsck=$?
echo "e2fsck exit=$fsck  (0 = clean; anything else = libext2 corrupted the filesystem)"
[ "$rc" -eq 0 ] && [ "$fsck" -eq 0 ] && echo "OVERALL: PASS" || echo "OVERALL: FAIL"
exit $(( rc != 0 || fsck != 0 ))
