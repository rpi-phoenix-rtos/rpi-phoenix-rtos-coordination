#!/usr/bin/env bash
#
# Host test of the Phoenix D-Bus CONFIGURATION (tools/gpu-lane/dbus): the same dbus 1.16.2
# source and meson options built natively (build.sh --host), run with the shipped
# conf/session-phoenix*.conf. It proves the auth choice, not Phoenix:
#
#   A  session-phoenix.conf, daemon without peer credentials (LD_PRELOAD nopeercred.so = what
#      Phoenix gives today): libdbus (dbus-send) and GDBus (host gdbus) both authenticate
#      ANONYMOUS and get a ListNames reply.
#   B  session-phoenix-external.conf, no peer credentials: EXTERNAL is REJECTED, both clients
#      fall back to ANONYMOUS.
#   C  session-phoenix-external.conf WITH peer credentials (= Phoenix after SO_PEERCRED):
#      EXTERNAL succeeds; the bus reports the client's uid.
#   D  negative control: EXTERNAL-only, no peer credentials: the connection MUST fail (else
#      the preload did not remove the credentials and A/B prove nothing).
#
# Usage: tools/gpu-lane/dbus/hosttest/run.sh [build-out dir]
#
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${1:-${here}/../build-out}"
HB="${out}/host-build"
DAEMON="${HB}/bus/dbus-daemon"
SEND="${HB}/tools/dbus-send"
GDBUS="$(command -v gdbus || true)"
for p in "${DAEMON}" "${SEND}"; do
	[ -x "${p}" ] || { echo "run.sh: ${p} missing (tools/gpu-lane/dbus/build.sh --host)" >&2; exit 1; }
done
[ -n "${GDBUS}" ] || echo "run.sh: host has no gdbus: GDBus checks skipped"

T="$(mktemp -d /tmp/dbus-hosttest.XXXXXX)"
trap '[ -n "${dpid:-}" ] && kill "${dpid}" 2>/dev/null; rm -rf "${T}"' EXIT
cc -O2 -Wall -Wextra -Werror -shared -fPIC -o "${T}/nopeercred.so" "${here}/nopeercred.c" -ldl || exit 1

fails=0
check() {  # description condition-result(0/1)
	if [ "$2" = 0 ]; then echo "  PASS $1"; else echo "  FAIL $1"; fails=$((fails + 1)); fi
}

run_case() {  # id conf preload(0/1) expect(ok|fail)
	local id="$1" conf="$2" preload="$3" expect="$4" sock="${T}/bus-$1" c="${T}/$1.conf" i
	sed "s|unix:path=/tmp/dbus-session|unix:path=${sock}|" "${conf}" > "${c}"
	if [ "${id}" = D ]; then
		sed -i -e '/<auth>ANONYMOUS<\/auth>/d' -e '/<allow_anonymous\/>/d' "${c}"
	fi
	echo "== case ${id}: $(basename "${conf}")$([ "${id}" = D ] && echo ' minus ANONYMOUS'), peer credentials $([ "${preload}" = 1 ] && echo REMOVED || echo present), expect ${expect}"
	if [ "${preload}" = 1 ]; then
		LD_PRELOAD="${T}/nopeercred.so" NOPEERCRED_QUIET=1 DBUS_VERBOSE=1 "${DAEMON}" --config-file="${c}" --nofork \
			--print-address=1 > "${T}/${id}.addr" 2> "${T}/${id}.daemon.log" &
	else
		DBUS_VERBOSE=1 "${DAEMON}" --config-file="${c}" --nofork --print-address=1 > "${T}/${id}.addr" 2> "${T}/${id}.daemon.log" &
	fi
	dpid=$!
	for i in $(seq 50); do [ -S "${sock}" ] && break; sleep 0.1; done
	check "${id}: daemon listening on ${sock} (address: $(head -1 "${T}/${id}.addr"))" "$([ -S "${sock}" ]; echo $?)"
	export DBUS_SESSION_BUS_ADDRESS="unix:path=${sock}"

	DBUS_VERBOSE=1 timeout 10 "${SEND}" --session --dest=org.freedesktop.DBus --type=method_call --print-reply \
		/org/freedesktop/DBus org.freedesktop.DBus.ListNames > "${T}/${id}.send.out" 2> "${T}/${id}.send.log"
	local rc=$?
	local mech
	mech="$(grep -oE 'client: Trying mechanism [A-Z_0-9]+' "${T}/${id}.send.log" | awk '{print $4}' | tr '\n' ' ')"
	echo "  dbus-send rc=${rc}, libdbus client tried: ${mech:-none}"
	if [ "${expect}" = ok ]; then
		check "${id}: dbus-send ListNames reply contains org.freedesktop.DBus" "$(grep -q '"org.freedesktop.DBus"' "${T}/${id}.send.out"; echo $?)"
	else
		check "${id}: dbus-send is refused" "$([ "${rc}" != 0 ]; echo $?)"
	fi

	if [ -n "${GDBUS}" ]; then
		G_DBUS_DEBUG=authentication timeout 10 "${GDBUS}" call --session --dest org.freedesktop.DBus \
			--object-path /org/freedesktop/DBus --method org.freedesktop.DBus.ListNames > "${T}/${id}.gdbus.out" 2>&1
		rc=$?
		mech="$(grep -oE "CLIENT: Trying mechanism '[A-Z_0-9]+'" "${T}/${id}.gdbus.out" | awk '{print $4}' | tr -d "'" | tr '\n' ' ')"
		echo "  gdbus rc=${rc}, GDBus client tried: ${mech:-none}"
		if [ "${expect}" = ok ]; then
			check "${id}: gdbus ListNames reply contains org.freedesktop.DBus" "$(grep -q "'org.freedesktop.DBus'" "${T}/${id}.gdbus.out"; echo $?)"
		else
			check "${id}: gdbus is refused" "$([ "${rc}" != 0 ]; echo $?)"
		fi
	fi

	kill "${dpid}" 2>/dev/null
	wait "${dpid}" 2>/dev/null
	dpid=
	local anon ext noc
	anon=$(grep -c 'authenticated client as anonymous' "${T}/${id}.daemon.log")
	ext=$(grep -c 'authenticated client based on socket credentials' "${T}/${id}.daemon.log")
	noc=$(grep -c "no credentials, mechanism EXTERNAL can't authenticate" "${T}/${id}.daemon.log")
	echo "  daemon: anonymous=${anon} external=${ext} external-without-credentials=${noc}"
	case "${id}" in
		A) check "A: both clients authenticated ANONYMOUS, none EXTERNAL" "$([ "${anon}" -ge 1 ] && [ "${ext}" = 0 ]; echo $?)" ;;
		B) check "B: EXTERNAL rejected for lack of credentials, then ANONYMOUS" "$([ "${noc}" -ge 1 ] && [ "${anon}" -ge 1 ] && [ "${ext}" = 0 ]; echo $?)" ;;
		C) check "C: EXTERNAL succeeded, no ANONYMOUS" "$([ "${ext}" -ge 1 ] && [ "${anon}" = 0 ]; echo $?)" ;;
		D) check "D: EXTERNAL failed for lack of credentials" "$([ "${noc}" -ge 1 ] && [ "${ext}" = 0 ]; echo $?)" ;;
	esac
}

run_case A "${here}/../conf/session-phoenix.conf" 1 ok
run_case B "${here}/../conf/session-phoenix-external.conf" 1 ok
run_case C "${here}/../conf/session-phoenix-external.conf" 0 ok
run_case D "${here}/../conf/session-phoenix-external.conf" 1 fail

echo "== result: $([ "${fails}" = 0 ] && echo "ALL PASS" || echo "${fails} FAIL")"
[ "${fails}" = 0 ]
