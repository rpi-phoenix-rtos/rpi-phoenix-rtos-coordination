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
# Usage:  scripts/pi-busy.sh [stale_secs]      (default 90)
# Exit:   0 = looks BUSY (log written recently), 1 = looks free
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
art="$repo/artifacts/rpi4b-uart"
stale="${1:-90}"

shopt -s nullglob
newest=""
newest_t=0
for f in "$art"/*.log; do
	t=$(stat -c %Y "$f" 2>/dev/null) || continue
	if [ "$t" -gt "$newest_t" ]; then
		newest_t=$t
		newest=$f
	fi
done

if [ -z "$newest" ]; then
	echo "no UART logs at all -- bench free"
	exit 1
fi

now=$(date +%s)
age=$((now - newest_t))
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
