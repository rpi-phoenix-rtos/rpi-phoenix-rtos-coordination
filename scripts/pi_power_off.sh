#!/usr/bin/env bash
#
# Power off the Raspberry Pi 4 test board via whatever smart plug the
# current host knows how to control. Counterpart of pi_power_on.sh.
#
#   Darwin  : Apple Shortcuts ("GniazdkoOff") talks to Apple Home.
#   Linux   : the script named by $MEROSS_PLUG_SCRIPT (e.g. a Meross
#             cloud-API helper). If that var is unset or the file is
#             missing, the power toggle is skipped (non-fatal) so build
#             and test flows on machines without a smart plug still run.
#

set -euo pipefail

host_os="$(uname -s)"

case "$host_os" in
	Darwin)
		shortcuts run "GniazdkoOff"
		echo "Pi powered off (Apple Home shortcut)"
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
		"$meross_plug" off
		echo "Pi powered off (smart plug via $meross_plug)"
		;;
	*)
		printf 'pi_power_off: unsupported host OS: %s\n' "$host_os" >&2
		exit 1
		;;
esac
