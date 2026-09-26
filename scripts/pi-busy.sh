#!/bin/bash
# Is the Pi bench busy? Answers WITHOUT pgrep/ps pattern matching.
#
# WHY THIS EXISTS. "Is a cycle still running?" was hand-rolled four times in one
# night as `pgrep -f 'psh-interact.py'` or `ps -eo args | grep -c ...`, and it was
# WRONG every time: the shell executing the check has the pattern in its own argv,
# so it matches itself and reports BUSY while the bench is idle. Once it also
# reported a healthy run as a 0-byte void, and the obvious response to that --
# re-run it -- would have started a second concurrent cycle and destroyed the run
# in flight, because the UART is exclusive.
#
# The reliable signal is the UART log's mtime. A live capture writes continuously;
# a log that has not been touched for a while means the cycle is over. Nothing
# about this can match itself.
#
# ⚠ One caveat it handles explicitly: a cycle sits out its --idle-secs window AFTER
# the workload finishes, during which the log is silent but the Pi is still held.
# So "log is stale" is reported as IDLE-OR-DONE rather than free, with the age, and
# the caller decides. The default staleness threshold is deliberately larger than a
# normal inter-line gap and smaller than a typical idle window.
#
# --wait MODE. Hand-rolled waiters have the SAME defect, and worse consequences:
# `until ! pgrep -f 'psh-interact.py'; do sleep 30; done` never terminates, because
# the loop's own shell carries the pattern. One such waiter sat for 1h36m holding a
# build that should have started, and left the bench idle ~15 minutes before it was
# noticed. So the wait belongs here, keyed on mtime, where it cannot self-match.
#
# Pass a staleness larger than the cycle's --idle-secs (e.g. 750 for --idle-secs
# 700): the cycle holds the Pi silently through that window, so only a longer gap
# proves it has exited.
#
# Usage:  scripts/pi-busy.sh [stale_secs]            (default 90)
#         scripts/pi-busy.sh --wait <stale_secs> [timeout_secs]
# Exit:   0 = looks BUSY (log written recently), 1 = looks free
#         --wait: 0 once free, 2 on timeout
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
art="$repo/artifacts/rpi4b-uart"

wait_mode=0
if [ "${1:-}" = "--wait" ]; then
	wait_mode=1
	stale="${2:-750}"
	deadline=$(( $(date +%s) + ${3:-5400} ))
else
	stale="${1:-90}"
fi

# ⚠ ONE `ls -t`, not a stat per file. The archive holds thousands of logs, and
# forking stat for each took longer than the 20 s poll interval -- the wait loop
# never got to compare anything. Measured: it blew a 30 s timeout on a bench that
# had been idle half an hour.
_newest_file() {
	ls -t "$art"/*.log 2>/dev/null | head -1
}

_newest_age() {
	local f t
	f=$(_newest_file)
	[ -z "$f" ] && { echo 999999; return; }
	t=$(stat -c %Y "$f" 2>/dev/null) || { echo 999999; return; }
	echo $(( $(date +%s) - t ))
}

if [ "$wait_mode" -eq 1 ]; then
	while :; do
		age=$(_newest_age)
		if [ "$age" -ge "$stale" ]; then
			printf 'bench free: no UART writes for %ss (>= %ss)\n' "$age" "$stale"
			exit 0
		fi
		if [ "$(date +%s)" -ge "$deadline" ]; then
			printf 'TIMEOUT waiting for the bench (last write %ss ago)\n' "$age" >&2
			exit 2
		fi
		sleep 20
	done
fi

shopt -s nullglob
newest=$(_newest_file)

if [ -z "$newest" ]; then
	echo "no UART logs at all -- bench free"
	exit 1
fi

age=$(_newest_age)
printf 'newest log : %s\n' "$(basename "$newest")"
printf 'last write : %ss ago\n' "$age"

if [ "$age" -lt "$stale" ]; then
	echo "verdict    : BUSY -- a capture is writing right now. Do NOT start a cycle."
	exit 0
fi

echo "verdict    : no writes for ${age}s (>= ${stale}s)."
echo "             The workload is finished, but a cycle may still be sitting out"
echo "             its --idle-secs window and holding the Pi. If you started that"
echo "             cycle, wait for its own completion rather than assuming free."
exit 1
