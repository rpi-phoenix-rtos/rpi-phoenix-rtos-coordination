#!/usr/bin/env bash
# The ENOSPC and hole-read harnesses, both block sizes, unmounted and power-cut.
# usage: run-enospc.sh        (LIBEXT2_SRC selects the ext2/ directory)
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
E="${LIBEXT2_SRC:-$root/sources/phoenix-rtos-filesystems/ext2}"
fails=0

for p in enospc holes; do
	gcc -O1 -g -fsanitize=address,undefined -DEOK=0 -I "$here/shim" -I "$E" \
		"$here/$p.c" "$E"/sb.c "$E"/gdt.c "$E"/inode.c "$E"/block.c "$E"/dir.c \
		"$E"/obj.c "$E"/file.c "$E"/ext2.c \
		"$root/sources/libphoenix/sys/list.c" "$root/sources/libphoenix/sys/rb.c" \
		"$here/shim/attrstub.c" -o "$here/$p" || { echo "BUILD FAILED: $p"; exit 2; }
done

mkimg() {  # mkimg <path> <mb> <blocksz>
	rm -f "$1"; dd if=/dev/zero of="$1" bs=1M count="$2" status=none
	mke2fs -q -t ext2 -b "$3" -I 128 -N 4096 -F "$1" >/dev/null 2>&1
}

# grade <img> <powercut>: e2fsck -fn clean. After a power cut the superblock's
# free counts may lag (ext2 writes them lazily); everything else must be exact.
grade() {
	out=$(e2fsck -fn "$1" 2>&1); frc=$?
	grp=$(printf '%s\n' "$out" | grep -c "for group")
	bmp=$(printf '%s\n' "$out" | grep -c "bitmap differences")
	ino=$(printf '%s\n' "$out" | grep -cE "^Inode [0-9]+")
	echo "e2fsck=$frc group-errs=$grp bitmap-diffs=$bmp inode-errs=$ino"
	[ "$frc" -eq 0 ] && [ "$grp" -eq 0 ] && [ "$bmp" -eq 0 ] && [ "$ino" -eq 0 ]
}

img=/tmp/ra-enospc.img
for b in 1024 4096; do
	for wl in "1000 0" "4096 100" "65536 0" "65536 3000" "1051648 100"; do
		set -- $wl
		for end in 0 1; do
			mkimg "$img" 24 "$b"
			out=$(ASAN_OPTIONS=detect_leaks=0 "$here/enospc" "$img" "$1" "$2" "$end" 2>&1); rc=$?
			g=$(grade "$img"); grc=$?
			printf "  enospc %s wsz=%-7s skew=%-4s %-9s rc=%s %s\n" "$b" "$1" "$2" \
				"$([ "$end" = 1 ] && echo power-cut || echo unmount)" "$rc" "$g"
			if [ "$rc" -ne 0 ] || [ "$grc" -ne 0 ]; then
				printf '%s\n' "$out" | grep -E "FAIL|fill:|big:" | head -12 | sed 's/^/      /'
				fails=1
			fi
		done
	done
	for end in 0 1; do
		mkimg "$img" 24 "$b"
		out=$(ASAN_OPTIONS=detect_leaks=0 "$here/holes" "$img" "$end" 2>&1); rc=$?
		g=$(grade "$img"); grc=$?
		printf "  holes  %s %-9s rc=%s %s\n" "$b" "$([ "$end" = 1 ] && echo power-cut || echo unmount)" "$rc" "$g"
		if [ "$rc" -ne 0 ] || [ "$grc" -ne 0 ]; then
			printf '%s\n' "$out" | grep -E "FAIL" | head -8 | sed 's/^/      /'
			fails=1
		fi
	done
done
rm -f "$img"

echo
[ "$fails" -eq 0 ] && echo "ENOSPC+HOLES: ALL GREEN" || echo "ENOSPC+HOLES: FAILURES"
exit "$fails"
