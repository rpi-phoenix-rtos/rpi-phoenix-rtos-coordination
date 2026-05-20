#!/usr/bin/env bash
#
# Power off the Raspberry Pi 4 test board via whatever smart plug the
# current host knows how to control. Counterpart of pi_power_on.sh.
#
#   Darwin  : Apple Shortcuts ("GniazdkoOff") talks to Apple Home.
#   Linux   : Meross cloud API via /home/houp/meross-plug/plug.py
#             (override path with $MEROSS_PLUG_SCRIPT).
#

set -euo pipefail

host_os="$(uname -s)"

case "$host_os" in
	Darwin)
		shortcuts run "GniazdkoOff"
		echo "Pi powered off (Apple Home shortcut)"
		;;
	Linux)
		meross_plug="${MEROSS_PLUG_SCRIPT:-/home/houp/meross-plug/plug.py}"
		if [ ! -x "$meross_plug" ]; then
			printf 'pi_power_off: meross-plug helper not executable: %s\n' "$meross_plug" >&2
			printf 'pi_power_off: set MEROSS_PLUG_SCRIPT or fix the path.\n' >&2
			exit 1
		fi
		"$meross_plug" off
		echo "Pi powered off (Meross cloud)"
		;;
	*)
		printf 'pi_power_off: unsupported host OS: %s\n' "$host_os" >&2
		exit 1
		;;
esac
