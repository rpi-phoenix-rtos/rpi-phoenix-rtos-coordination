#!/usr/bin/env bash
# Build the host harness around the real libphoenix Doug-Lea allocator.
# Nothing under sources/ is modified or written to.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
lp="$here/../../sources/libphoenix"
out="$here/malloc-harness"
san=""

for a in "$@"; do
	case "$a" in
		--asan) san="-fsanitize=address -fno-omit-frame-pointer" ;;
		*) echo "usage: $0 [--asan]" >&2; exit 2 ;;
	esac
done

# -Istubs must come FIRST so <sys/threads.h>, <sys/debug.h>, <arch.h> and the
# forwarders for <sys/rb.h>/<sys/list.h>/<sys/minmax.h> resolve to stubs/.  The
# libphoenix include tree is deliberately NOT on the -I path (its stdio.h,
# stdlib.h, ... would shadow the host's).
cflags=(-std=gnu11 -g -O0 -Wall -Wextra
	-Wno-unused-parameter -Wno-unused-function -Wno-sign-compare
	-I"$here/stubs")

set -x
gcc "${cflags[@]}" $san -c -o "$here/rb.o"      "$lp/sys/rb.c"
gcc "${cflags[@]}" $san -c -o "$here/list.o"    "$lp/sys/list.c"
gcc "${cflags[@]}" $san -c -o "$here/harness.o" "$here/harness.c"
gcc $san -o "$out" "$here/harness.o" "$here/rb.o" "$here/list.o"
set +x

echo "built $out"
