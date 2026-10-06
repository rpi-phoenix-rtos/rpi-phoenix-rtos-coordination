#!/bin/bash
#
# serve-for-pi.sh -- the browser benchmark suite's HTTP server for the Pi, on the netboot host
# (bench.sh's default base, http://10.42.0.1:8090). It serves the staged suite from the NFS export
# and writes the pages' JSON reports to artifacts/browser-bench/pi-results/.
#
#     tools/browser/bench/host/serve-for-pi.sh start|stop|status
#
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "${here}/../../../.." && pwd)
root=/srv/phoenix-rpi4-nfs-gcc16/usr/share/browser-bench
results=${repo}/artifacts/browser-bench/pi-results
pidfile=${repo}/artifacts/browser-bench/serve-for-pi.pid
address=${BENCH_SERVE_ADDRESS:-10.42.0.1}
port=${BENCH_SERVE_PORT:-8090}
mkdir -p "${results}"

running() { [ -f "${pidfile}" ] && kill -0 "$(cat "${pidfile}")" 2>/dev/null; }

case "${1:-}" in
	start)
		if running; then
			echo "serve-for-pi: already running (pid $(cat "${pidfile}"))"
		else
			setsid python3 "${here}/../serve.py" --root "${root}" --results "${results}" --bind "${address}" \
				--port "${port}" >> "${results}/serve.log" 2>&1 < /dev/null &
			echo $! > "${pidfile}"
			sleep 1
		fi
		if curl -sf -m 5 "http://${address}:${port}/phx-ping"; then
			echo "serve-for-pi: up, reports -> ${results}"
		else
			echo "serve-for-pi: NOT answering on ${address}:${port} (is the netboot interface up? log: ${results}/serve.log)"
			exit 1
		fi
		;;
	stop)
		running && kill "$(cat "${pidfile}")" && echo "serve-for-pi: stopped"
		rm -f "${pidfile}"
		;;
	status)
		if running; then echo "serve-for-pi: running (pid $(cat "${pidfile}"))"; else echo "serve-for-pi: not running"; exit 1; fi
		;;
	*) echo "usage: $0 start|stop|status" >&2; exit 2 ;;
esac
