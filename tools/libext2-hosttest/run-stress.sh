#!/usr/bin/env bash
# Randomised libext2 stress, graded by a shadow model AND e2fsck.
# usage: run-stress.sh [blocksz] [seeds] [ops]
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
# LIBEXT2_SRC: test an ext2/ directory other than the sibling checkout
# (e.g. a filesystems worktree on a feature branch).
E="${LIBEXT2_SRC:-$root/sources/phoenix-rtos-filesystems/ext2}"
blocksz="${1:-1024}"; seeds="${2:-5}"; ops="${3:-400}"
gcc -O1 -g -fsanitize=address,undefined -DEOK=0 -I "$here/shim" -I "$E" \
    "$here/stress.c" "$E"/sb.c "$E"/gdt.c "$E"/inode.c "$E"/block.c "$E"/dir.c \
    "$E"/obj.c "$E"/file.c "$E"/ext2.c \
    "$root/sources/libphoenix/sys/list.c" "$root/sources/libphoenix/sys/rb.c" \
    "$here/shim/attrstub.c" -o "$here/libext2-stress" || { echo "BUILD FAILED"; exit 2; }
bad=0
for s in $(seq 1 "$seeds"); do
  img="$here/stress-$blocksz-$s.img"
  rm -f "$img"; dd if=/dev/zero of="$img" bs=1M count=64 status=none
  mke2fs -q -t ext2 -b "$blocksz" -I 128 -F "$img" >/dev/null 2>&1
  out=$(ASAN_OPTIONS=detect_leaks=0 "$here/libext2-stress" "$img" "$s" "$ops" 2>&1); rc=$?
  fsckout=$(e2fsck -fn "$img" 2>&1); frc=$?
  if [ $rc -ne 0 ] || [ $frc -ne 0 ]; then
    bad=1
    echo "=== seed $s FAILED (harness rc=$rc, e2fsck rc=$frc) ==="
    echo "$out" | grep -E "FAIL|STRESS|ERROR|stress:" | head -8
    echo "$fsckout" | grep -vE "^Pass|^e2fsck 1" | head -8
  else
    echo "seed $s: ok (model matches, e2fsck clean)"
  fi
  rm -f "$img"
done
[ $bad -eq 0 ] && echo "ALL SEEDS PASS (blocksz=$blocksz)" || echo "FAILURES at blocksz=$blocksz"
exit $bad
