#!/usr/bin/env bash
#
# Host test of the gtk3-wayland GLib patch 0003 ("gapplication: send stdin only over a
# connection that passes fds"): why `xfdesktop --quit` did nothing in m7h-xfce, and why a
# second Thunar (Super+E, the panel launcher) would fail the same way.
#
# On Phoenix-RTOS dbus-daemon never agrees to UNIX_FD passing (getsockname() on a UNIX
# socket leaves the address empty, so _dbus_socket_can_pass_unix_fd() says no). A TCP bus
# has no fd passing either, so the host reproduces it: the host's dbus-daemon on
# tcp:host=127.0.0.1, ANONYMOUS, and hosttest/gapp_cmdline_test.c -- a primary instance,
# then `--quit` from a remote one (GApplication's CommandLine call).
#
#   arm      GLib                                 bus    predicted
#   stock    the host's (unpatched, 2.88.x)       tcp    remote prints "The connection is closed"
#                                                        and dies of its own SIGTERM (GDBus exit-on-
#                                                        close; the Pi's xfdesktop_rc=143); the
#                                                        primary never hears the quit (= m7h-xfce;
#                                                        negative control)
#   patched  <glib-src> built natively, static    tcp    remote rc=0, primary got quit, stdin=none
#   patched  the same                             unix   remote rc=0, primary got quit,
#                                                        stdin=passed (fd passing still used)
#
# Usage: hosttest/gapp-cmdline.sh [<patched glib source dir>]
#   default: tools/gpu-lane/gtk3-wayland/build-out-usr-2/src/glib (gtk3-wayland/build.sh
#   --usr --out build-out-usr-2 extracts and patches it)
# Writes only into build-out/hosttest/gapp/.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
root="$(cd "${here}/../../.." && pwd)"
glib_src="${1:-${root}/tools/gpu-lane/gtk3-wayland/build-out-usr-2/src/glib}"
out="${here}/build-out/hosttest/gapp"
H="${out}/glib-prefix"
[ -f "${glib_src}/gio/gapplicationimpl-dbus.c" ] || { echo "gapp-cmdline.sh: no GLib source at ${glib_src}" >&2; exit 1; }
grep -q 'G_DBUS_CAPABILITY_FLAGS_UNIX_FD_PASSING' "${glib_src}/gio/gapplicationimpl-dbus.c" \
	|| { echo "gapp-cmdline.sh: ${glib_src} lacks patch 0003" >&2; exit 1; }
mkdir -p "${out}"

# the patched GLib, natively (static: the test binary needs no LD_LIBRARY_PATH)
stamp="$(sha256sum "${glib_src}/gio/gapplicationimpl-dbus.c" | cut -c1-16)"
if [ "$(cat "${out}/glib.stamp" 2>/dev/null || true)" != "${stamp}" ]; then
	rm -rf "${out}/glib-build" "${H}"
	meson setup "${out}/glib-build" "${glib_src}" --prefix "${H}" --libdir lib --default-library static \
		--buildtype=debugoptimized -Dtests=false -Dintrospection=disabled -Dnls=disabled -Dlibmount=disabled \
		-Dselinux=disabled -Dxattr=false -Dman-pages=disabled -Ddocumentation=false -Dsysprof=disabled \
		-Dglib_debug=disabled > "${out}/glib.log" 2>&1 || { tail -30 "${out}/glib.log"; exit 1; }
	ninja -C "${out}/glib-build" install >> "${out}/glib.log" 2>&1 || { tail -30 "${out}/glib.log"; exit 1; }
	echo "${stamp}" > "${out}/glib.stamp"
fi
cc -O1 -g -Wall -Wextra -Werror -o "${out}/test-stock" "${here}/hosttest/gapp_cmdline_test.c" \
	$(pkg-config --cflags --libs gio-2.0)
cc -O1 -g -Wall -Wextra -Werror -o "${out}/test-patched" "${here}/hosttest/gapp_cmdline_test.c" \
	$(PKG_CONFIG_PATH="${H}/lib/pkgconfig" PKG_CONFIG_LIBDIR="${H}/lib/pkgconfig:$(pkg-config --variable pc_path pkg-config)" \
		pkg-config --static --cflags --libs gio-2.0)

bus_conf() {  # listen-address -> config file
	cat > "${out}/bus-$2.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>$1</listen>
  <auth>ANONYMOUS</auth>
  <allow_anonymous/>
  <apparmor mode="disabled"/>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF
}
bus_conf "tcp:host=127.0.0.1,port=0" tcp
bus_conf "unix:path=${out}/bus-socket" unix

fail=0
arm() {  # label binary bus
	local label="$1" bin="${out}/$2" bus="$3" addr dpid ppid rc i log="${out}/$1.log"
	rm -f "${out}/bus-socket"
	dbus-daemon --config-file="${out}/bus-${bus}.conf" --nofork --print-address=3 3> "${out}/${label}.addr" 2> "${out}/${label}.daemon" &
	dpid=$!
	for i in $(seq 50); do [ -s "${out}/${label}.addr" ] && break; sleep 0.1; done
	addr="$(head -1 "${out}/${label}.addr")"
	export DBUS_SESSION_BUS_ADDRESS="${addr}"
	"${bin}" > "${log}" 2>&1 &
	ppid=$!
	for i in $(seq 50); do grep -q '^GAPP primary up' "${log}" && break; sleep 0.1; done
	rc=0
	timeout 10 "${bin}" --quit > "${out}/${label}.remote" 2>&1 < /dev/null || rc=$?
	for i in $(seq 30); do kill -0 "${ppid}" 2> /dev/null || break; sleep 0.1; done
	if kill -0 "${ppid}" 2> /dev/null; then primary=running; kill "${ppid}"; else primary=exited; fi
	wait "${ppid}" 2> /dev/null || true
	kill "${dpid}"
	wait "${dpid}" 2> /dev/null || true
	unset DBUS_SESSION_BUS_ADDRESS
	echo "  ${label}: bus=${addr%%,*} remote_rc=${rc} remote_said=\"$(tr '\n' ' ' < "${out}/${label}.remote" | cut -c1-80)\" primary=${primary} got=\"$(grep -h '^GAPP primary got' "${log}" | cut -c18- || true)\""
	eval "${label}_rc=${rc} ${label}_primary=${primary}"
}
check() {  # description condition
	if eval "$2"; then echo "  PASS $1"; else echo "  FAIL $1 [$2]"; fail=1; fi
}

echo "== GLib CommandLine over a bus without / with fd passing"
arm stock_tcp test-stock tcp
arm patched_tcp test-patched tcp
arm patched_unix test-patched unix
check 'negative control: stock GLib over TCP fails as on the Pi (remote rc!=0, "The connection is closed")' \
	'[ "${stock_tcp_rc}" != 0 ] && grep -q "The connection is closed" "${out}/stock_tcp.remote" && ! grep -q "^GAPP primary got" "${out}/stock_tcp.log"'
check 'patched GLib over TCP: the remote --quit reaches the primary (rc=0, stdin=none), the primary exits' \
	'[ "${patched_tcp_rc}" = 0 ] && grep -q "^GAPP primary got remote quit=1 stdin=none" "${out}/patched_tcp.log" && [ "${patched_tcp_primary}" = exited ]'
check 'patched GLib over a UNIX bus: stdin is still passed (fd passing kept where it exists)' \
	'[ "${patched_unix_rc}" = 0 ] && grep -q "^GAPP primary got remote quit=1 stdin=passed" "${out}/patched_unix.log" && [ "${patched_unix_primary}" = exited ]'
[ "${fail}" = 0 ] && echo "ALL PASS" || { echo "FAILURES (logs in ${out})"; exit 1; }
