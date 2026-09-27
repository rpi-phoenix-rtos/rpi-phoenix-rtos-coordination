#!/bin/sh
# labwc-drm host tests (no Pi, seconds). Each prints "LWHOST <name> ... verdict=PASS|FAIL".
#  1. uchar: compat UTF-8 mbrtoc32/c32rtomb over every scalar value, against glibc's
#     C.UTF-8 conversions (foot's char32_t layer)
#  2. shm: compat shm_open/shm_unlink under wlroots' own util/shm.c (the keymap path),
#     with a file-backed stand-in for shmsrv; then the same with Phoenix's failing
#     fchmod(): the patched util/shm.c (-D__phoenix__) must still succeed, the
#     unpatched build must fail (negative control)
#  3. glib: shims g_string_replace against the host GLib's own (>= 2.68)
#  4. sync: compat C11 threads + semaphores (foot's render-worker pattern) + wcs*
#  5. epoll_pwait: compat epoll_pwait over the M6 epoll emulation, foot's signal pattern
#  6. timerfd_read: read() of an emulated timer (foot's delayed-render timers; the m7b
#     failure), with a negative control built without the read() wrapper
# Needs a build.sh run first (the extracted wlroots tree).
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
top="${here}/.."
W="${top}/../weston-drm"
bo="${top}/build-out"
out="${bo}/hosttest"
mkdir -p "${out}"
CF="-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-result -fsanitize=address,undefined"
rc=0
# show FAIL lines and the verdict; any verdict other than PASS fails the run
t() { o="$("$@" 2>&1)" || true; printf '%s\n' "${o}" | grep -v '^ok'; printf '%s\n' "${o}" | grep -q 'verdict=PASS$' || rc=1; }

cc ${CF} -Dmbrtoc32=lwphx_mbrtoc32 -Dc32rtomb=lwphx_c32rtomb -c "${top}/compat/src/lwphx_uchar.c" -o "${out}/lwphx_uchar.o"
cc ${CF} "${here}/uchar_test.c" "${out}/lwphx_uchar.o" -o "${out}/uchar_test"
t "${out}/uchar_test"

shmdir="$(mktemp -d)"
wl="${bo}/src/wlroots"
for v in phoenix plain; do
	d=""
	[ "${v}" = phoenix ] && d="-D__phoenix__"
	cc ${CF} ${d} -DLWPHX_SHM_NS="\"${shmdir}\"" -I"${top}/compat/include" -I"${W}/compat/include" \
		-D_POSIX_C_SOURCE=200809L -I"${wl}/include" -I"${bo}/wlroots-build/include" -DWLR_USE_UNSTABLE \
		"${top}/compat/src/lwphx_shm.c" "${wl}/util/shm.c" "${here}/shm_test.c" -o "${out}/shm_test_${v}" -lpthread
done
# each run starts with an empty object directory (the stand-in's ids restart at 1)
shm_run() { rm -rf "${shmdir:?}"/*; t "$@"; }
shm_run "${out}/shm_test_phoenix"
shm_run env FCHMOD_FAILS=1 "${out}/shm_test_phoenix"
shm_run env FCHMOD_FAILS=1 "${out}/shm_test_plain"
rm -rf "${shmdir}"

cc ${CF} -Dg_string_replace=lwphx_g_string_replace $(pkg-config --cflags glib-2.0) -c "${top}/shims/src/glib_compat.c" \
	-o "${out}/glib_compat.o"
cc ${CF} $(pkg-config --cflags glib-2.0) "${here}/glib_test.c" "${out}/glib_compat.o" -o "${out}/glib_test" \
	$(pkg-config --libs glib-2.0)
t "${out}/glib_test"

cc ${CF} -I"${top}/compat/include" "${top}/compat/src/lwphx_threads.c" "${top}/compat/src/lwphx_sem.c" \
	-Dwcsncat=lwphx_wcsncat -Dwcscasecmp=lwphx_wcscasecmp -Dwcsncasecmp=lwphx_wcsncasecmp \
	"${top}/compat/src/lwphx_wchar.c" "${here}/sync_test.c" -o "${out}/sync_test" -lpthread
t "${out}/sync_test"

cc ${CF} -DWLPHX_HAVE_ITIMERSPEC -I"${top}/compat/include" -I"${W}/compat/include" -Wl,--wrap=close -Wl,--wrap=write \
	"${W}/compat/src/wlphx_epoll.c" "${top}/compat/src/lwphx_epoll_pwait.c" "${here}/epoll_pwait_test.c" \
	-o "${out}/epoll_pwait_test" -lpthread
t "${out}/epoll_pwait_test"
# timerfd read(): the fix (wrapper linked) must PASS; without the wrapper (the m7b binaries)
# the FOOT ROWs must fail -- the negative control passes when the plain build FAILS
# -U_FORTIFY_SOURCE: a fortified read() becomes __read_chk, which --wrap=read does not see
TF="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DWLPHX_HAVE_ITIMERSPEC -I${top}/compat/include -I${W}/compat/include -Wl,--wrap=close -Wl,--wrap=write"
cc ${CF} ${TF} -DNEGATIVE_NAME='"timerfd_read"' -Wl,--wrap=read "${W}/compat/src/wlphx_epoll.c" \
	"${top}/compat/src/lwphx_read.c" "${here}/timerfd_read_test.c" -o "${out}/timerfd_read_test" -lpthread
t "${out}/timerfd_read_test"
cc ${CF} ${TF} -DNEGATIVE_NAME='"timerfd_read-without-wrapper"' "${W}/compat/src/wlphx_epoll.c" \
	"${here}/timerfd_read_test.c" -o "${out}/timerfd_read_nowrap" -lpthread
neg="$("${out}/timerfd_read_nowrap" 2>&1 || true)"
nfail=$(printf '%s\n' "${neg}" | grep -c '^FAIL .*FOOT ROW' || true)
if [ "${nfail}" -ge 3 ]; then
	echo "LWHOST timerfd_read-negative-control foot_rows_failing=${nfail} verdict=PASS"
else
	echo "LWHOST timerfd_read-negative-control foot_rows_failing=${nfail} verdict=FAIL"
	rc=1
fi

echo "LWHOST all verdict=$([ "${rc}" = 0 ] && echo PASS || echo FAIL)"
exit "${rc}"
