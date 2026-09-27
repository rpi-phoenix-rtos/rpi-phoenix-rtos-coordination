#!/usr/bin/env bash
#
# Host test of pi/xfce-desktop.sh's bus half (tools/gpu-lane/xfce-wayland): the same
# libxfce4util + xfconf sources and patches built NATIVELY, the host's dbus-daemon with a
# copy of the Phoenix session configuration (ANONYMOUS, one servicedir) whose servicedir
# holds the built org.xfce.Xfconf.service pointing at the native xfconfd. The Pi script
# then runs unchanged with session "none" and no compositor (LABWC=/nonexistent), which
# exercises steps 1-3 and 6: bus up, xfconfd by BUS ACTIVATION, the xfconf round trip,
# the staged Thunar defaults read through XDG_CONFIG_DIRS, clean shutdown, saved channels.
# (Static, like the Pi build: the native programs need no LD_LIBRARY_PATH.)
# (Plus a negative control: ACTIVATION=0 must report via=explicit.)
#
# Writes only into build-out/hosttest/. Uses /tmp/dbus-session and /tmp/xfce-* like the Pi.
# Proves the script and the configuration, not Phoenix.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${here}/build-out/hosttest"
src="${here}/build-out/src"
[ -d "${src}/xfconf" ] || { echo "run.sh: run build.sh first (sources)" >&2; exit 1; }
mkdir -p "${out}"
H="${out}/prefix"
export PATH="${here}/bin:${PATH}"
export PKG_CONFIG_PATH="${H}/lib/pkgconfig"
if [ ! -x "${H}/lib/xfce4/xfconf/xfconfd" ]; then
	rm -rf "${out}/libxfce4util-build" "${out}/xfconf-build"
	meson setup "${out}/libxfce4util-build" "${src}/libxfce4util" --prefix "${H}" --libdir lib --default-library static \
		-Dintrospection=false -Dvala=disabled > "${out}/libxfce4util.log" 2>&1
	ninja -C "${out}/libxfce4util-build" install >> "${out}/libxfce4util.log" 2>&1
	mkdir -p "${out}/xfconf-build"
	( cd "${out}/xfconf-build" && "${src}/xfconf/configure" --prefix="${H}" --disable-shared --enable-static --disable-nls --disable-introspection \
		--disable-vala --disable-gsettings-backend --disable-checks --with-bash-completion-dir=no ) > "${out}/xfconf.log" 2>&1
	make -C "${out}/xfconf-build" -j"$(nproc)" install >> "${out}/xfconf.log" 2>&1
fi
svc="${out}/services"
mkdir -p "${svc}" "${out}/xdg/xfce4/xfconf/xfce-perchannel-xml"
sed "s|^Exec=.*|Exec=${H}/lib/xfce4/xfconf/xfconfd|" "${H}/share/dbus-1/services/org.xfce.Xfconf.service" > "${svc}/org.xfce.Xfconf.service"
cp "${here}/conf/xfconf/thunar.xml" "${out}/xdg/xfce4/xfconf/xfce-perchannel-xml/"
sed "s|<servicedir>.*</servicedir>|<servicedir>${svc}</servicedir>|" "${here}/../dbus/conf/session-phoenix.conf" > "${out}/session.conf"

run() {  # label env...
	local label="$1"
	shift
	env "$@" BUS_CONF="${out}/session.conf" DAEMON="$(command -v dbus-daemon)" SEND="$(command -v dbus-send)" \
		XFCONFD="${H}/lib/xfce4/xfconf/xfconfd" XFCONF_QUERY="${H}/bin/xfconf-query" LABWC=/nonexistent \
		XFCE_CONFIG_DIRS="${out}/xdg" HOLD=0 bash "${here}/pi/xfce-desktop.sh" none noinput > "${out}/${label}.log" 2>&1 || true
}
fail=0
check() {  # label pattern description
	if grep -qE -- "$2" "${out}/$1.log"; then echo "  PASS $1: $3"; else echo "  FAIL $1: $3 (/$2/)"; fail=1; fi
}
run activation
check activation '^XFCE dbus=up ' 'the bus comes up'
check activation '^XFCE xfconfd activation rc=0 ' 'ListChannels starts xfconfd through the .service file'
check activation '^XFCE xfconfd via=activation names=org.xfce.Xfconf' 'xfconfd owns org.xfce.Xfconf'
check activation '^XFCE xfconf set_rc=0 get_rc=0 value=hello-[0-9]+ thunar_thumbnail_mode=THUNAR_THUMBNAIL_MODE_NEVER \(rc 0\)' 'round trip + staged Thunar default'
check activation '^XFCE xfconf channels=([^,]*,)*thunar(,|$)' 'the staged thunar channel is listed'
check activation '^XFCE dbus exited rc=0 .*socket=gone' 'the bus stops on TERM'
check activation '^XFCE saved channels=.*xfce-phx-probe.xml' 'xfconfd saved the probe channel'
check activation '^XFCE done' 'the script ends'
run explicit ACTIVATION=0
check explicit '^XFCE xfconfd started pid=' 'negative control: no activation call, xfconfd started directly'
check explicit '^XFCE xfconfd via=explicit names=org.xfce.Xfconf' 'negative control: via=explicit'
[ "${fail}" = 0 ] && echo "ALL PASS" || { echo "FAILURES (logs in ${out})"; exit 1; }
