#!/bin/sh
# weston-drm host tests (no Pi, a few seconds):
#  1. epoll_test: compat/src/wlphx_epoll.c built natively against compat/include (which
#     shadows the host's <sys/epoll.h> & co.) with ASan/UBSan
#  2. xkb_test: Weston's XKB start-up (patches 0003 + 0007) against the native libxkbcommon
#     build.sh made from the same source (build-out/host-xkbcommon-build), with no include
#     path existing -- the Pi's situation -- and the baked keymap (build-out/weston_keymap.h)
# Needs a build.sh run first (for the native libxkbcommon and the keymap header).
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
bo="${here}/../build-out"
out="${bo}/hosttest"
mkdir -p "${out}"
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-result -fsanitize=address,undefined -DWLPHX_HAVE_ITIMERSPEC \
	-I"${here}/../compat/include" -Wl,--wrap=close -Wl,--wrap=write \
	"${here}/../compat/src/wlphx_epoll.c" "${here}/epoll_test.c" -o "${out}/epoll_test" -lpthread
"${out}/epoll_test"
hx="${bo}/host-xkbcommon-build"
cc -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I"${bo}/src/libxkbcommon/include" -I"${bo}" \
	"${here}/xkb_test.c" -o "${out}/xkb_test" -L"${hx}" -lxkbcommon -Wl,-rpath,"${hx}"
"${out}/xkb_test"
