#!/usr/bin/env bash
#
# Power on the Raspberry Pi 4 test board via whatever smart plug the
# current host knows how to control.
#
#   Darwin  : Apple Shortcuts ("GniazdkoOn") talks to Apple Home.
#   Linux   : the script named by $MEROSS_PLUG_SCRIPT (e.g. a Meross
#             cloud-API helper). If that var is unset or the file is
#             missing, the power toggle is skipped (non-fatal) so build
#             and test flows on machines without a smart plug still run.
#

set -euo pipefail

host_os="$(uname -s)"

case "$host_os" in
	Darwin)
		echo "Powering on Pi (Apple Home shortcut)"
		shortcuts run "GniazdkoOn"
		;;
	Linux)
		# Default to the author's-lab helper if present; otherwise the
		# var is unset and we skip the toggle (see below).
		meross_plug="${MEROSS_PLUG_SCRIPT:-}"
		if [ -z "$meross_plug" ] && [ -x /home/houp/meross-plug/plug.py ]; then
			meross_plug=/home/houp/meross-plug/plug.py
		fi
		if [ -z "$meross_plug" ] || [ ! -f "$meross_plug" ]; then
			echo "pi_power: MEROSS_PLUG_SCRIPT not configured or missing; skipping power toggle (set MEROSS_PLUG_SCRIPT to your smart-plug control script)"
			exit 0
		fi
		echo "Powering on Pi (smart plug via $meross_plug)"
		"$meross_plug" on
		;;
	*)
		printf 'pi_power_on: unsupported host OS: %s\n' "$host_os" >&2
		exit 1
		;;
esac
