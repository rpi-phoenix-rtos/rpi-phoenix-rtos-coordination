#!/bin/bash
#
# serve-for-pi.sh -- the streaming-video test set's HTTP server for the Pi, on the netboot host
# (http://10.42.0.1:8091, the address the pages and pi/b8-stream.sh use; the bench's server is
# :8090). It serves the media root (artifacts/media: gen-ladders.sh's ladders + stage.sh's pages)
# and logs every request and every page event to artifacts/media/serve.log.
#
#     tools/browser/media/serve-for-pi.sh start|stop|restart|status
#
#   MEDIA_SERVE_ADDRESS (10.42.0.1), MEDIA_SERVE_PORT (8091), MEDIA_ROOT (artifacts/media)
#
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "${here}/../../.." && pwd)
root=${MEDIA_ROOT:-${repo}/artifacts/media}
pidfile=${root}/serve-for-pi.pid
log=${root}/serve.log
address=${MEDIA_SERVE_ADDRESS:-10.42.0.1}
port=${MEDIA_SERVE_PORT:-8091}
mkdir -p "${root}"

running() { [ -f "${pidfile}" ] && kill -0 "$(cat "${pidfile}")" 2>/dev/null; }

start() {
	if running; then
		echo "serve-for-pi: already running (pid $(cat "${pidfile}"))"
	else
		[ -d "${root}/ladders" ] || echo "serve-for-pi: WARNING no ladders in ${root} (tools/browser/media/gen-ladders.sh)"
		[ -d "${root}/pages" ] || echo "serve-for-pi: WARNING no pages in ${root} (tools/browser/media/stage.sh)"
		echo "=== serve-for-pi start $(date '+%Y-%m-%d %H:%M:%S %z')" >> "${log}"
		setsid python3 "${here}/serve-media.py" --root "${root}" --bind "${address}" --port "${port}" \
			>> "${log}" 2>&1 < /dev/null &
		echo $! > "${pidfile}"
		sleep 1
	fi
	if curl -sf -m 5 "http://${address}:${port}/phx-ping"; then
		echo "serve-for-pi: up on http://${address}:${port}/, log ${log}"
	else
		echo "serve-for-pi: NOT answering on ${address}:${port} (is the netboot interface up? log: ${log})"
		exit 1
	fi
}

stop() {
	running && kill "$(cat "${pidfile}")" && echo "serve-for-pi: stopped"
	rm -f "${pidfile}"
}

case "${1:-}" in
	start) start ;;
	stop) stop ;;
	restart) stop; sleep 1; start ;;
	status)
		if running; then echo "serve-for-pi: running (pid $(cat "${pidfile}")), log ${log}"; else echo "serve-for-pi: not running"; exit 1; fi
		;;
	*) echo "usage: $0 start|stop|restart|status" >&2; exit 2 ;;
esac
