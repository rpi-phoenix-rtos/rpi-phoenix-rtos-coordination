#!/usr/bin/env bash
#
# run-showcase-gate.sh — run the six-app showcase gate, one Pi cycle per app,
# and print the result table.
#
# This is the gate the weekly log quotes as "6/6, 0 faults each". It existed only
# as six hand-typed test-cycle-psh-interact.sh invocations, which meant the drive
# commands had to be recovered from old UART logs every time somebody re-ran it,
# and the per-app timings were re-guessed. Both are now recorded here.
#
# ⚠ THE PI LOCK: the UART is exclusive, so the cycles run STRICTLY SEQUENTIALLY.
# A second concurrent cycle gets an empty log. Do not background individual apps.
# The whole script takes ~35-50 min for six apps, which is longer than a single
# foreground tool call should hold, so run IT in the background and watch the
# summary file:
#
#     nohup ./scripts/run-showcase-gate.sh --label mygate > gate.log 2>&1 &
#
# ⚠ WHAT THIS SCRIPT CANNOT TELL YOU: whether the app actually RENDERED. A game
# that reaches its main loop and draws nothing still exits 0 with a clean UART
# log. The verdict per app is a HUMAN look at the HDMI frames listed in the
# summary -- see docs/inprogress for what each app should look like. What this
# does check is the mechanical part that is easy to get wrong: the app was
# launched, the boot reached psh, and no fault or mid-print stop appeared.
#
# Boot mode is netboot/nfsroot (the export must be in sync with what you want to
# test -- scripts/sync-netboot-tree.sh), so the SD card must be OUT of the Pi.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
cd "${repo_root}"

# The six drive commands, recovered from the logs of the passing 2026-09-10 gate.
# Bare launches are deliberate for Quake 1/2/vkQuake: each auto-plays a demo, and
# `+commands` are silently ignored on shareware data
# (see docs/misc + the quake shareware +commands note).
apps_default=(
	"x:startx_gpu action"
	"qspasm:quakespasm"
	"q3:quake3"
	"q2:yquake2"
	"vkq:vkquake"
	"stk:stk --track=hacienda --numkarts=4 --profile-laps=2"
)

label="gate"
wait_secs=150
idle_secs=240
max_cmd_secs=300
only=""

usage() {
	cat <<EOF
Usage: $(basename "$0") [options]

  --label TEXT     label prefix for the UART logs (default: $label)
  --only a,b,c     run only these app keys (default: all)
                   keys: $(printf '%s ' "${apps_default[@]%%:*}")
  --wait-secs N    seconds to wait for the psh prompt (default $wait_secs)
  --idle-secs N    capture window after the launch command (default $idle_secs)
  -h, --help       this
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
		--label)        label="$2"; shift 2 ;;
		--only)         only="$2"; shift 2 ;;
		--wait-secs)    wait_secs="$2"; shift 2 ;;
		--idle-secs)    idle_secs="$2"; shift 2 ;;
		--max-cmd-secs) max_cmd_secs="$2"; shift 2 ;;
		-h|--help)      usage; exit 0 ;;
		*)              echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

apps=()
for entry in "${apps_default[@]}"; do
	key="${entry%%:*}"
	if [ -n "${only}" ]; then
		case ",${only}," in *",${key},"*) ;; *) continue ;; esac
	fi
	apps+=("${entry}")
done
[ "${#apps[@]}" -gt 0 ] || { echo "no apps selected (--only '${only}')" >&2; exit 2; }

echo "=== showcase gate: ${#apps[@]} app(s), one Pi cycle each, sequential ==="
echo "    label prefix : ${label}"
echo "    per-app budget: wait ${wait_secs}s + idle ${idle_secs}s"
echo

declare -a rows=()
rc_all=0

for entry in "${apps[@]}"; do
	key="${entry%%:*}"
	cmd="${entry#*:}"
	lbl="${label}-${key}"

	echo "--- [${key}] ${cmd}"
	./scripts/test-cycle-psh-interact.sh \
		--label "${lbl}" \
		--wait-secs "${wait_secs}" \
		--idle-secs "${idle_secs}" \
		--max-cmd-secs "${max_cmd_secs}" \
		-- "${cmd}"
	cycle_rc=$?

	# Newest log carrying this label.
	log="$(./scripts/uart-list.sh 40 "${lbl}" 2>/dev/null | head -1)"
	[ -n "${log}" ] && log="artifacts/rpi4b-uart/${log##*/}"

	prompt="?" faults="?" launched="?"
	if [ -n "${log}" ] && [ -s "${log}" ]; then
		grep -qa 'psh)%' "${log}" && prompt="yes" || prompt="NO"
		faults=$(grep -acE 'Exception|Data Abort|panic|ESR=|ELR=|FAR=|LIB_ASSERT|assertion' "${log}")
		# The command echo proves the launch was issued; the app's own output
		# proves it started. Both matter -- a log that stops AT the echo is the
		# signature of a target that took the command and died.
		grep -qaF "${cmd%% *}" "${log}" && launched="yes" || launched="NO"
	else
		log="(no log)"
	fi

	frames=$(ls -1 artifacts/hdmi/*"${lbl}"* 2>/dev/null | wc -l | tr -d ' ')

	rows+=("${key}|${cycle_rc}|${prompt}|${faults}|${launched}|${frames}|${log}")
	[ "${cycle_rc}" = 0 ] || rc_all=1
	[ "${prompt}" = "yes" ] || rc_all=1
	[ "${faults}" = "0" ] || rc_all=1
	echo
done

echo "=== showcase gate summary ==="
printf '%-8s %-4s %-7s %-7s %-9s %-7s %s\n' app rc prompt faults launched frames log
for r in "${rows[@]}"; do
	IFS='|' read -r a rc p f l fr lg <<<"${r}"
	printf '%-8s %-4s %-7s %-7s %-9s %-7s %s\n' "$a" "$rc" "$p" "$f" "$l" "$fr" "$lg"
done
echo
echo "⚠ MECHANICAL RESULT ONLY. Now LOOK at the HDMI frames for each app"
echo "  (artifacts/hdmi/*<label>*) before recording a pass -- a clean log does not"
echo "  mean anything was drawn."
exit "${rc_all}"
