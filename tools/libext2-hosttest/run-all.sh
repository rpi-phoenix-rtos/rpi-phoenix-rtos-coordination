#!/usr/bin/env bash
# Every libext2 host harness, both block sizes. One command, ~2 minutes.
#
# Run this BEFORE spending a Pi cycle on any libext2/libcache change. It found
# ten defects (12-21) on 2026-09-22, four of which destroyed data, and it is
# the only check that covers sparse files, directory link counts, hard links,
# object teardown and the setattr paths.
#
# usage: run-all.sh [seeds]     (default 10)
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
# LIBEXT2_SRC: test an ext2/ directory other than the sibling checkout
# (e.g. a filesystems worktree on a feature branch).
E="${LIBEXT2_SRC:-$root/sources/phoenix-rtos-filesystems/ext2}"
seeds="${1:-10}"
fails=0

build() {
	gcc -O1 -g -fsanitize=address,undefined -DEOK=0 -I "$here/shim" -I "$E" \
		"$here/$1.c" "$E"/sb.c "$E"/gdt.c "$E"/inode.c "$E"/block.c "$E"/dir.c \
		"$E"/obj.c "$E"/file.c "$E"/ext2.c \
		"$root/sources/libphoenix/sys/list.c" "$root/sources/libphoenix/sys/rb.c" \
		"$here/shim/attrstub.c" -o "$here/$1" || { echo "BUILD FAILED: $1"; exit 2; }
}

mkimg() {  # mkimg <path> <mb> <blocksz>
	rm -f "$1"; dd if=/dev/zero of="$1" bs=1M count="$2" status=none
	mke2fs -q -t ext2 -b "$3" -I 128 -N 4096 -F "$1" >/dev/null 2>&1
}

for p in harness stress dirstress linkstress uaf devnode bigdir attrtest dirtime noumount busy; do build "$p"; done

# The concurrency harness needs REAL mutexes and pthreads. Everything else runs
# on the no-op lock path, which keeps those runs simple; this one must not.
gcc -O1 -g -fsanitize=address,undefined -DEOK=0 -DSHIM_REAL_MUTEX \
	-I "$here/shim" -I "$E" "$here/concurrent.c" \
	"$E"/sb.c "$E"/gdt.c "$E"/inode.c "$E"/block.c "$E"/dir.c "$E"/obj.c "$E"/file.c "$E"/ext2.c \
	"$root/sources/libphoenix/sys/list.c" "$root/sources/libphoenix/sys/rb.c" \
	"$here/shim/attrstub.c" "$here/shim/mutexstore.c" -lpthread \
	-o "$here/concurrent" || { echo "BUILD FAILED: concurrent"; exit 2; }

echo "=== single-shot ==="
for b in 1024 4096; do
	printf "  %-10s %s: " harness "$b"; "$here/run.sh" "$b" 2>&1 | grep -o "OVERALL: .*" || fails=1
	for p in linkstress devnode attrtest dirtime; do
		img=/tmp/ra-$p-$b.img; mkimg "$img" 48 "$b"
		ASAN_OPTIONS=detect_leaks=0 "$here/$p" "$img" >/dev/null 2>&1; rc=$?
		e2fsck -fn "$img" >/dev/null 2>&1; frc=$?
		printf "  %-10s %s: rc=%s e2fsck=%s\n" "$p" "$b" "$rc" "$frc"
		[ "$rc" -eq 0 ] && [ "$frc" -eq 0 ] || fails=1
		rm -f "$img"
	done
	img=/tmp/ra-bigdir-$b.img; mkimg "$img" 64 "$b"
	ASAN_OPTIONS=detect_leaks=0 "$here/bigdir" "$img" 800 >/dev/null 2>&1; rc=$?
	e2fsck -fn "$img" >/dev/null 2>&1; frc=$?
	printf "  %-10s %s: rc=%s e2fsck=%s\n" bigdir "$b" "$rc" "$frc"
	[ "$rc" -eq 0 ] && [ "$frc" -eq 0 ] || fails=1
	rm -f "$img"
	# object teardown, case by case
	for c in 1 2 3 4 5; do
		img=/tmp/ra-uaf.img; mkimg "$img" 16 "$b"
		out=$(ASAN_OPTIONS=detect_leaks=0 "$here/uaf" "$img" "$c" 2>&1); rc=$?
		e2fsck -fn "$img" >/dev/null 2>&1; frc=$?
		[ "$rc" -eq 0 ] && [ "$frc" -eq 0 ] || { echo "  uaf case $c ($b): rc=$rc e2fsck=$frc"; fails=1; }
		rm -f "$img"
	done
	printf "  %-10s %s: 5/5 cases\n" uaf "$b"
done

echo "=== randomised, $seeds seeds each ==="
for b in 1024 4096; do
	mw=4096; [ "$b" = 4096 ] && mw=16384
	bad=0
	for s in $(seq 1 "$seeds"); do
		img=/tmp/ra-st.img; mkimg "$img" 64 "$b"
		ASAN_OPTIONS=detect_leaks=0 "$here/stress" "$img" "$s" 800 "$mw" >/dev/null 2>&1 || bad=$((bad+1))
		e2fsck -fn "$img" >/dev/null 2>&1 || bad=$((bad+1))
		rm -f "$img"
	done
	echo "  stress    $b: $bad failure(s)"; [ "$bad" -eq 0 ] || fails=1
	bad=0
	for s in $(seq 1 "$seeds"); do
		img=/tmp/ra-dir.img; mkimg "$img" 32 "$b"
		ASAN_OPTIONS=detect_leaks=0 "$here/dirstress" "$img" "$s" 300 >/dev/null 2>&1 || bad=$((bad+1))
		e2fsck -fn "$img" >/dev/null 2>&1 || bad=$((bad+1))
		rm -f "$img"
	done
	echo "  dirstress $b: $bad failure(s)"; [ "$bad" -eq 0 ] || fails=1
done

echo "=== concurrency (real mutexes, as UMASS_N_MSG_THREADS=2) ==="
for b in 1024 4096; do
	for nt in 2 4; do
		img=/tmp/ra-cc.img; mkimg "$img" 64 "$b"
		timeout 240 env ASAN_OPTIONS=detect_leaks=0 "$here/concurrent" "$img" "$nt" 400 >/tmp/ra-cc.out 2>&1
		rc=$?
		e2fsck -fn "$img" >/dev/null 2>&1; frc=$?
		if [ "$rc" -eq 124 ]; then
			echo "  threads $nt ($b): TIMED OUT -- possible deadlock"; fails=1
		else
			printf "  threads %s (%s): rc=%s e2fsck=%s\n" "$nt" "$b" "$rc" "$frc"
			[ "$rc" -eq 0 ] && [ "$frc" -eq 0 ] || fails=1
		fi
		rm -f "$img"
	done
done

# Is the filesystem still in use? umount needs this and a wrong answer either way
# is bad: a false "busy" makes umount never work, a false "idle" is the data-loss
# defect it exists to prevent (issue C3).
echo "=== busy predicate ==="
for b in 1024 4096; do
	img=/tmp/ra-busy.img; mkimg "$img" 48 "$b"
	out=$(ASAN_OPTIONS=detect_leaks=0 "$here/busy" "$img" 2>&1); rc=$?
	ok=$(printf '%s' "$out" | grep -c '\[ ok \]')
	printf "  busy      %s: rc=%s checks_passed=%s\n" "$b" "$rc" "$ok"
	{ [ "$rc" -eq 0 ] && [ "$ok" -ge 14 ]; } || { printf '%s\n' "$out"; fails=1; }
	rm -f "$img"
done

# Every harness above ends with ext2_objs_destroy(), which IS an unmount -- so
# none of them models the SD root, which is "/" and is never unmounted. This one
# does the workload and then just stops, like a power cut.
#
# The grade is against a PRISTINE image of the same mke2fs, not against "e2fsck
# is happy": a filesystem that never allocated anything also passes e2fsck. The
# tagged "(ino N)" lines assert the workload actually ran, so a harness that
# silently did nothing fails here instead of reading as a clean power cut.
echo "=== no-unmount (power-cut model) ==="
for b in 1024 4096; do
	ref=/tmp/ra-nu-ref.img; mkimg "$ref" 48 "$b"
	want=$(e2fsck -fn "$ref" 2>&1 | grep -oE '[0-9]+/[0-9]+ files.*blocks' | tail -1)
	rm -f "$ref"
	for m in 1 2 3; do
		img=/tmp/ra-nu.img; mkimg "$img" 48 "$b"
		out=$(ASAN_OPTIONS=detect_leaks=0 "$here/noumount" "$img" "$m" 2>&1); rc=$?
		e2fsck -fn "$img" >/dev/null 2>&1; frc=$?
		got=$(e2fsck -fn "$img" 2>&1 | grep -oE '[0-9]+/[0-9]+ files.*blocks' | tail -1)
		did=$(printf '%s' "$out" | grep -c '(ino ')
		printf "  mode %s (%s): rc=%s e2fsck=%s allocations=%s residue=%s\n" \
			"$m" "$b" "$rc" "$frc" "$did" \
			"$([ "$got" = "$want" ] && echo none || echo "$got vs pristine $want")"
		{ [ "$rc" -eq 0 ] && [ "$frc" -eq 0 ] && [ "$did" -gt 0 ] && [ "$got" = "$want" ]; } || fails=1
		rm -f "$img"
	done
done

echo
[ "$fails" -eq 0 ] && echo "ALL GREEN" || echo "FAILURES -- see above"
exit "$fails"
