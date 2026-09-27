#!/bin/sh
# weston-drm host test of the compat event-loop emulation (no Pi, ~1 s).
# Builds compat/src/wlphx_epoll.c natively against compat/include (which shadows the
# host's <sys/epoll.h> & co.) with ASan/UBSan and runs hosttest/epoll_test.c.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
out="${here}/../build-out/hosttest"
mkdir -p "${out}"
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-result -fsanitize=address,undefined -DWLPHX_HAVE_ITIMERSPEC -I"${here}/../compat/include" \
	-Wl,--wrap=close -Wl,--wrap=write \
	"${here}/../compat/src/wlphx_epoll.c" "${here}/epoll_test.c" -o "${out}/epoll_test" -lpthread
"${out}/epoll_test"
