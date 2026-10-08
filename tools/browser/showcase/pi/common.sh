#!/bin/bash
#
# common.sh -- shared helpers of the browser showcase's XFCE_AUTOSTART items (sourced, never run).
# The items live in /usr/share/browser-showcase/ on the Pi (tools/browser/showcase/stage.sh) and
# are started by /bin/xfce-autostart.sh as "/bin/bash=/usr/share/browser-showcase/<item>.sh:<s>".
#
# Every line of ours starts with "BSHOW " (grading, and the reel's offsets):
#
#   BSHOW item=<name> start epoch=<UTC seconds> t=<session SECONDS> size=<WxH> <what>
#
# epoch is the Pi's wall clock (NTP-set by psh at boot) when the browser is about to start. The
# browser's own lines count from its start (WPEB t=<ms>, WKGB t=<ms>), so epoch + t/1000 is the
# wall-clock time of any of them; scripts/browser-reel-events.py turns that into an offset into
# the HDMI recording, whose file name holds the host's UTC start time.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

exec 2>&1
export HOME=${HOME:-/root}
# shellcheck disable=SC2034 # SHOWCASE and MEDIA are for the items that source this
SHOWCASE=/usr/share/browser-showcase
# shellcheck disable=SC2034
MEDIA=${BSHOW_MEDIA:-http://10.42.0.1:8091}

# the wall clock with sub-second digits where coreutils' date is there (busybox's has no %N)
bshow_epoch() {
	local e
	e=$(/usr/bin/date -u +%s.%N 2>/dev/null) || e=""
	case "${e}" in
		*.*[0-9]) printf '%s' "${e%??????}" ;;   # milliseconds are enough
		*) /bin/date -u +%s 2>/dev/null || echo 0 ;;
	esac
}

# bshow_start <item> <what...>: the item's start line, just before the browser is exec'd
bshow_start() {
	local item=$1
	shift
	echo "BSHOW item=${item} start epoch=$(bshow_epoch) t=${SECONDS} size=${BROWSER_SIZE:-default} $*"
}

# --- synthetic keys for wpe-browser --auto (WPE_BROWSER_AUTO) ------------------------------------
# The launcher's "<s>:type:<text>" step feeds the whole text in one tick, which on screen is an
# address that appears at once. For a recording each character is its own step, AUTO_CPS_CS
# hundredths of a second apart, so the address is typed at a person's pace. Times are seconds from
# the browser's start, as the launcher counts them. Steps are comma-separated, so a typed text
# cannot contain a comma; everything else (':', '/', '?', '=', spaces) is fine.
AUTO=""
AUTO_CPS_CS=${BSHOW_TYPE_CS:-12}   # 12 cs per character: ~8 characters a second

# auto_step <hundredths> <step>: one step at <hundredths> of a second from the start
auto_step() {
	local at
	printf -v at '%d.%02d' $(($1 / 100)) $(($1 % 100))
	AUTO="${AUTO:+${AUTO},}${at}:$2"
}

# auto_key <seconds> <keys>: e.g. auto_key 12 Return, auto_key 30 ctrl+l, auto_key 40 alt+Left
auto_key() {
	auto_step $(($1 * 100)) "key:$2"
}

# auto_type <seconds> <text>: one character per step from <seconds> on; sets AUTO_END to the
# whole second after the last character
auto_type() {
	local at=$(($1 * 100)) text=$2 i
	for ((i = 0; i < ${#text}; i++)); do
		auto_step "${at}" "type:${text:i:1}"
		at=$((at + AUTO_CPS_CS))
	done
	AUTO_END=$(((at + 99) / 100))
}

# auto_address <seconds> <text>: Ctrl+L, the text typed from one second later, then Return on
# the whole second after the last character; sets AUTO_END to the second of the Return
auto_address() {
	auto_key "$1" ctrl+l
	auto_type $(($1 + 1)) "$2"
	auto_key "${AUTO_END}" Return
}
