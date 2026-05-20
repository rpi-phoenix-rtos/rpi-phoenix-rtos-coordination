#!/usr/bin/env bash
#
# Power on the Raspberry Pi 4 test board via whatever smart plug the
# current host knows how to control.
#
#   Darwin  : Apple Shortcuts ("GniazdkoOn") talks to Apple Home.
#   Linux   : Meross cloud API via /home/houp/meross-plug/plug.py
#             (override path with $MEROSS_PLUG_SCRIPT).
#

set -euo pipefail

host_os="$(uname -s)"

case "$host_os" in
	Darwin)
		echo "Powering on Pi (Apple Home shortcut)"
		shortcuts run "GniazdkoOn"
		;;
	Linux)
		meross_plug="${MEROSS_PLUG_SCRIPT:-/home/houp/meross-plug/plug.py}"
		if [ ! -x "$meross_plug" ]; then
			printf 'pi_power_on: meross-plug helper not executable: %s\n' "$meross_plug" >&2
			printf 'pi_power_on: set MEROSS_PLUG_SCRIPT or fix the path.\n' >&2
			exit 1
		fi
		echo "Powering on Pi (Meross cloud)"
		"$meross_plug" on
		;;
	*)
		printf 'pi_power_on: unsupported host OS: %s\n' "$host_os" >&2
		exit 1
		;;
esac
