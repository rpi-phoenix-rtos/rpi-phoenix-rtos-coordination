#!/bin/bash
#
# survey.sh -- the browser site survey: real websites one by one in the real windowed browser
# (wpe-browser in a labwc window of the XFCE session), one psh command (psh has no pipes and no
# quotes: everything is one /bin/bash run with plain words):
#
#     /bin/bash /usr/share/wpe-browser/survey.sh [key=value...]
#
#   key=value
#     limit=S       a site's first load must finish within S s (default 120): wpe-browser
#                   --timeout=S; the harness's own limit is S+30 (a UI stuck before its main loop
#                   never runs the launcher's timer and ignores SIGTERM)
#     snap=1|0      1 (default): wpe-browser --snapshot=<dir>/<NN>-<name>.png, which paints the
#                   page once its first load has finished and then exits; 0: --exit-after-load
#     dwell=S       keep each page S s after its load finished (catches crashes and console
#                   errors after the load), then end the browser with SIGTERM; no PNG then (this
#                   launcher snapshots at the load only). Default 0
#     sites=FILE    the site list (default /usr/share/wpe-browser/survey-sites.txt: "<name> <url>")
#     only=a,b,...  only these names;  from=N / to=N  only sites N..M of the list (1-based)
#     size=WxH      the window (default 1280x960, /bin/browser's)
#     dmabuf=1|0    frames to labwc as dma-bufs (default 1, /bin/browser's) or shared memory
#     gpu=0|1       Skia CPU raster (default 0, /bin/browser's) or GPU raster on the V3D
#     stall=S       wpe-browser --stall-secs (default 60; the browser's 10 floods on heavy pages)
#     rss=S         wpe-browser --rss-secs: every process's memory footprint every S s (default 3)
#     profile=fresh|home  fresh (default): cookies + HTTP cache in <dir>/profile, empty at the
#                   start, kept from site to site (cold cache for every site, like a new user);
#                   home: the real profile in $HOME (warm)
#     hold=S        the XFCE session's limit (default: sites x (limit+dwell+60) + 300)
#     env=NAME=VAL  an environment variable for the browser (repeatable), e.g. env=JSC_useJIT=false
#
# One browser process per site (not one process with --cycle, as b6.sh soak does): --snapshot,
# --exit-after-load and --timeout act on the first load only, so per-site PNGs, exit codes and
# load limits exist only per process; and a site that wedges, leaks or crashes the UI then cannot
# spoil the sites after it. Navigation from site to site in one process is b6.sh soak's check.
#
# Lines (and the browser's own "WPEB ..." and pages' "...CONSOLE ..." lines between them):
#   SURVEY-SH begin|net|session|end ...          this script
#   SURVEY-SITE run= site=<n> name= url= limit_s=  a site starts (the parser's window opens)
#   SURVEY-ALIVE run= site= t=<s> last=<the last load line>   every 60 s of one site
#   SURVEY run=<nonce> site=<n> name=<tag> result=OK|TIMEOUT|CRASH|HANG|ERROR load_ms= commit_ms=
#          start_ms= wall_s= http= console_errors= js_errors= console_msgs= webprocess_rss_kb=
#          sysmem_used_kb= webprocs= stalls= unresponsive= rc= end= reason= snapshot= temp_mC=
#          url=<u> title=<the rest of the line>                    one per site
#   SURVEY-SUM run= sites= ok= error= timeout= hang= crash= ...  at the end
# The SURVEY lines are printed again before SURVEY-SUM (inside the session and once more at psh):
# the UART corrupts the odd line. Each site's full output, its PNG and the summary are also on the
# root file system, /root/survey/<run>/ (the NFS export: the host reads them directly):
#   <NN>-<name>.log, <NN>-<name>.png, summary.txt;  /root/survey/LATEST holds the last <run>.
# Host side: tools/browser/survey/parse-survey.py <uart log> > the results of
# docs/browser/SITE-SURVEY.md (coordination repo).
#
# Copyright 2026 Phoenix Systems
#
# This file is part of Phoenix-RTOS.
#
# %LICENSE%

exec 2>&1
# SURVEY_SELF/_BROWSER/_OUT/_SESSION: host dry runs only
SELF=${SURVEY_SELF:-/usr/share/wpe-browser/survey.sh}
BROWSER=${SURVEY_BROWSER:-/usr/bin/wpe-browser}
OUT_ROOT=${SURVEY_OUT:-/root/survey}
SESSION=${SURVEY_SESSION:-/bin/xfce-session}
export HOME=${HOME:-/root}
export PHX_TRACE_ABORT=1
export THUNAR_START=${THUNAR_START:-0}   # no file manager window over the browser

# --- arguments (the session's inner run gets them back from SURVEY_ARGS) ------------------------
if [ -n "${SURVEY_INNER:-}" ]; then
	# shellcheck disable=SC2086 # plain words
	set -- ${SURVEY_ARGS}
fi
ARGS_TEXT="$*"
LIMIT=120 SNAP=1 DWELL=0 SITES=/usr/share/wpe-browser/survey-sites.txt ONLY= FROM=1 TO=999
SIZE=1280x960 DMABUF=1 GPU=0 STALL=60 RSS=3 PROFILE_MODE=fresh HOLD_OPT=
EXTRA_ENV=()
for kv in "$@"; do
	case "${kv}" in
		limit=*) LIMIT=${kv#limit=} ;;
		snap=*) SNAP=${kv#snap=} ;;
		dwell=*) DWELL=${kv#dwell=} ;;
		sites=*) SITES=${kv#sites=} ;;
		only=*) ONLY=${kv#only=} ;;
		from=*) FROM=${kv#from=} ;;
		to=*) TO=${kv#to=} ;;
		size=*) SIZE=${kv#size=} ;;
		dmabuf=*) DMABUF=${kv#dmabuf=} ;;
		gpu=*) GPU=${kv#gpu=} ;;
		stall=*) STALL=${kv#stall=} ;;
		rss=*) RSS=${kv#rss=} ;;
		profile=*) PROFILE_MODE=${kv#profile=} ;;
		hold=*) HOLD_OPT=${kv#hold=} ;;
		env=*=*) EXTRA_ENV+=("${kv#env=}") ;;
		help | -h | --help) sed -n '3,50p' "${SELF}"; exit 0 ;;
		*) echo "SURVEY-SH bad option ${kv} (help: /bin/bash ${SELF} help)"; exit 2 ;;
	esac
done
NONCE=${SURVEY_NONCE:-s$(date +%m%d%H%M)x${RANDOM}}
DIR=${OUT_ROOT}/${NONCE}
SUMMARY=${DIR}/summary.txt
if [ "${PROFILE_MODE}" = home ]; then
	PROFILE_ARGS=()
else
	PROFILE_ARGS=(--data-dir="${DIR}/profile/data" --cache-dir="${DIR}/profile/cache")
fi

# the selected sites, "<n> <name> <url>" per line (n: the position in the list file)
site_list() {
	local n=0 name url
	while read -r name url _; do
		case "${name}" in '' | '#'*) continue ;; esac
		n=$((n + 1))
		[ "${n}" -ge "${FROM}" ] && [ "${n}" -le "${TO}" ] || continue
		if [ -n "${ONLY}" ]; then
			case ",${ONLY}," in *",${name},"*) ;; *) continue ;; esac
		fi
		echo "${n} ${name} ${url}"
	done < "${SITES}"
}

thermal() {  # milli-degrees C, or "-"
	local t
	t=$(timeout 3 cat /dev/thermal 2>/dev/null) || t=-
	echo "${t:--}" | tr -d ' \n'
}

pids=()
BPID= JOBS_BEFORE=
new_jobs() {  # this site's jobs still running (the browser | tee pipeline), not the ones before it
	local p
	for p in $(jobs -rp); do
		case "${JOBS_BEFORE}" in *" ${p} "*) ;; *) echo "${p}" ;; esac
	done
}
pause() {  # interruptible by the session's SIGTERM
	sleep "$1" &
	wait $!
}

# A site's log -> "key=value ..." with title=<rest> last. Only the UI process's lines carry the
# load events; children print role=, mem and stall lines; pages print "...CONSOLE <SOURCE> <LEVEL>
# <message>" (JSC ConsoleClient::printConsoleMessage, persistent sessions only). busybox awk.
# killed: the harness ended the browser (limit|dwell|none); rc: the browser's exit status.
analyse() {
	awk -v killed="$2" -v rc="$3" '
	function ms(line,   rest) {
		rest = substr(line, 8)
		return substr(rest, 1, index(rest, " ") - 1) + 0
	}
	function field(line, key,   i, rest) {   # the value of " key=" up to the next space
		i = index(line, " " key "=")
		if (!i) return ""
		rest = substr(line, i + length(key) + 2)
		i = index(rest, " ")
		return i ? substr(rest, 1, i - 1) : rest
	}
	function clean(s, n) {
		gsub(/[\001-\037\177]/, "", s)
		if (length(s) > n) s = substr(s, 1, n)
		return s
	}
	BEGIN { st = cm = fin = -1; http = "-"; title = ""; err = ""; crash = ""; webmax = -1; sysmax = -1
		nweb = cerr = jserr = cmsg = stalls = unresp = lastresp = startstall = timeout = 0
		snap = "-"; exitst = "" }
	/CONSOLE/ {
		cmsg++
		if ($0 ~ /CONSOLE( [A-Z][A-Za-z]*)?( [A-Z]+)? ERROR( |$)/) cerr++
		if ($0 ~ /CONSOLE JS ERROR/) jserr++
	}
	/^WPEB t=[0-9]+ load started / { if (st < 0) st = ms($0) }
	/^WPEB t=[0-9]+ load committed / { if (cm < 0) cm = ms($0) }
	/^WPEB t=[0-9]+ load finished / { if (fin < 0) fin = ms($0) }
	/^WPEB t=[0-9]+ load-failed-tls / { if (fin < 0 && err == "") err = "tls-flags-" field($0, "flags") "-clock-" field($0, "clock") }
	/^WPEB t=[0-9]+ load-failed uri=/ {
		e = $0; sub(/.* error=/, "", e); sub(/ page-id=[0-9]+$/, "", e)
		if (fin < 0 && err == "" && e !~ /[Cc]ancel/) err = e
	}
	/^WPEB t=[0-9]+ title / { t = substr($0, index($0, " title ") + 7); if (t != "") title = t }
	/^WPEB t=[0-9]+ policy response status=/ { if (fin < 0) http = field($0, "status") }
	/^WPEB t=[0-9]+ web-process-terminated reason=/ { if (crash == "") crash = field($0, "reason") }
	/^WPEB t=[0-9]+ web-process responsive=0/ { unresp++; lastresp = 0 }
	/^WPEB t=[0-9]+ web-process responsive=1/ { lastresp = 1 }
	/^WPEB t=[0-9]+ role=web pid=[0-9]+ ppid=/ { nweb++ }
	/^WPEB t=[0-9]+ mem role=web / { v = field($0, "footprint_kb") + 0; if (v > webmax) webmax = v }
	/^WPEB t=[0-9]+ sysmem used_kb=/ { v = field($0, "used_kb") + 0; if (v > sysmax) sysmax = v }
	/ stall n=[0-9]+ main_ms=[0-9]+ report=0 / { stalls++ }
	/ (frame|present)-stall n=[0-9]+ report=0 / { stalls++ }
	/ ipc-stall readable_ms=/ { stalls++ }
	/ start-stall phase=[a-z-]+ phase_ms=[0-9]+ report=0/ { stalls++; if ($0 ~ /role=ui /) startstall = 1 }
	/^WPEB t=[0-9]+ timeout after / { timeout = 1 }
	/^WPEB t=[0-9]+ snapshot file=/ { snap = field($0, "file") }
	/^WPEB t=[0-9]+ snapshot-error/ { snap = "error" }
	/^WPEB t=[0-9]+ exit status=/ { exitst = field($0, "status") }
	END {
		reason = "-"
		if (crash != "") { result = "CRASH"; reason = "web-" crash }
		else if (killed == "none" && rc != "" && rc != 0 && rc != 1 && rc != 2 && rc != 3) { result = "CRASH"; reason = "ui-rc-" rc }
		else if (err != "") { result = "ERROR"; reason = err }
		else if (fin >= 0) result = "OK"
		else if (killed == "none" && rc == 1) { result = "ERROR"; reason = "ui-rc-1" }
		else if (st < 0) { result = "HANG"; reason = startstall ? "ui-start-stall" : "no-load-start" }
		else if (unresp && !lastresp) { result = "HANG"; reason = "unresponsive" }
		else if (stalls) { result = "HANG"; reason = "stalled" }
		else { result = "TIMEOUT"; reason = timeout ? "launcher-timeout" : "harness-limit" }
		gsub(/ /, "_", reason); reason = clean(reason, 80)
		# (no comparisons inside a printf argument list: ">" there is an output redirection)
		load = "-"; if (fin >= 0 && st >= 0) load = fin - st
		commit = "-"; if (cm >= 0 && st >= 0) commit = cm - st
		start = "-"; if (st >= 0) start = st
		web = "-"; if (webmax >= 0) web = webmax
		sys = "-"; if (sysmax >= 0) sys = sysmax
		if (rc == "") rc = "-"
		printf "result=%s load_ms=%s commit_ms=%s start_ms=%s http=%s console_errors=%d js_errors=%d console_msgs=%d",
			result, load, commit, start, http, cerr, jserr, cmsg
		printf " webprocess_rss_kb=%s sysmem_used_kb=%s webprocs=%d stalls=%d unresponsive=%d rc=%s end=%s reason=%s snapshot=%s",
			web, sys, nweb, stalls, unresp, rc, killed, reason, snap
		printf " TITLE=%s\n", clean(title, 120)
	}' "$1"
}

# run_one <n> <name> <url>
run_one() {
	local n=$1 name=$2 url=$3 id log pidf png t0 secs hard next_beat=60 extra=() killed=none rc kv title temp0 fin_at=
	id=$(printf '%02d' "${n}")-${name}
	log=${DIR}/${id}.log
	png=${DIR}/${id}.png
	pidf=/tmp/survey-${NONCE}-${id}.pid
	extra=(--size="${SIZE}" "${PROFILE_ARGS[@]}" --stall-secs="${STALL}" --hang-recovery=0 --rss-secs="${RSS}"
		--timeout="${LIMIT}")
	[ "${GPU}" = 1 ] || extra+=(--cpu-rendering)
	[ "${DMABUF}" = 1 ] && extra+=(--dmabuf)
	if [ "${DWELL}" -gt 0 ]; then
		:   # no exit option: the harness ends the browser DWELL s after the load
	elif [ "${SNAP}" = 1 ]; then
		extra+=(--snapshot="${png}")
	else
		extra+=(--exit-after-load)
	fi
	hard=$((LIMIT + DWELL + 30))
	temp0=$(thermal)
	echo "SURVEY-SITE run=${NONCE} site=${n} name=${name} url=${url} limit_s=${LIMIT} temp_mC=${temp0}"
	echo "SURVEY-SH site=${n} args=${extra[*]} env=${EXTRA_ENV[*]:-none}" > "${log}"
	t0=${SECONDS}
	JOBS_BEFORE=" $(jobs -rp | tr '\n' ' ') "
	(
		unset WEBKIT_SKIA_ENABLE_CPU_RENDERING   # --cpu-rendering decides (gpu=)
		env "${EXTRA_ENV[@]}" "${BROWSER}" "${extra[@]}" "${url}" 2>&1 &
		echo $! > "${pidf}"
		wait $!
		echo "SURVEY-SH browser-exit rc=$?"
	) | tee -a "${log}" &
	pids=($!)
	pause 2
	BPID=$(cat "${pidf}" 2>/dev/null)
	while :; do
		secs=$((SECONDS - t0))
		grep -a -q '^SURVEY-SH browser-exit' "${log}" && break
		if [ "${DWELL}" -gt 0 ]; then
			grep -a -q 'web-process-terminated' "${log}" && { killed=crash; break; }
			if [ -z "${fin_at}" ] && grep -a -q '^WPEB t=[0-9]* load finished' "${log}"; then
				fin_at=${SECONDS}
			fi
			[ -n "${fin_at}" ] && [ $((SECONDS - fin_at)) -ge "${DWELL}" ] && { killed=dwell; break; }
		fi
		[ "${secs}" -ge "${hard}" ] && { killed=limit; break; }
		if [ "${secs}" -ge "${next_beat}" ]; then
			echo "SURVEY-ALIVE run=${NONCE} site=${n} t=${secs} temp_mC=$(thermal) last=$(grep -a -o 'WPEB t=[0-9]* load [a-z-]*' "${log}" | tail -n 1)"
			next_beat=$((next_beat + 60))
		fi
		pause 2
	done
	# end the browser; its subshell writes "SURVEY-SH browser-exit" into the log once it is gone
	exited() { grep -a -q '^SURVEY-SH browser-exit' "${log}"; }
	if [ -n "${BPID}" ] && ! exited; then
		kill -TERM "${BPID}" 2>/dev/null
		for _ in 1 2 3 4 5 6 7 8; do exited && break; pause 2; done
		if ! exited; then
			echo "SURVEY-SH kill -KILL ${BPID} (no exit 16 s after SIGTERM)"
			kill -KILL "${BPID}" 2>/dev/null
			for _ in 1 2 3 4 5; do exited && break; pause 2; done
		fi
	fi
	# the pipe (tee) closes when the browser's children are gone too (orphans exit within ~2 s)
	for _ in 1 2 3 4 5 6; do [ -z "$(new_jobs)" ] && break; pause 2; done
	if [ -n "$(new_jobs)" ]; then
		echo "SURVEY-SH WARNING site=${n}: output pipe still open (a child of the browser lives on); not waiting for it"
	else
		wait "${pids[0]}" 2>/dev/null
	fi
	BPID=
	rm -f "${pidf}"
	secs=$((SECONDS - t0))
	rc=$(grep -a -m1 -o 'browser-exit rc=[0-9]*' "${log}" | cut -d= -f2)
	[ "${killed}" = crash ] && killed=none   # the crash decides; the harness only ended the rest
	kv=$(analyse "${log}" "${killed}" "${rc}")
	title=${kv#* TITLE=}
	kv=${kv%% TITLE=*}
	echo "SURVEY run=${NONCE} site=${n} name=${name} ${kv} wall_s=${secs} temp_mC=${temp0}/$(thermal) url=${url} title=${title}" |
		tee -a "${SUMMARY}"
	pause 5
}

# SURVEY-SUM from the summary file
summarize() {
	awk -v run="${NONCE}" -v wall="$1" '
	/^SURVEY run=/ {
		n++
		for (i = 1; i <= NF; i++) {
			if ($i ~ /^result=/) r = substr($i, 8)
			if ($i ~ /^load_ms=[0-9]/) { l = substr($i, 9) + 0; nl++; sl += l; if (l > ml) ml = l }
			if ($i ~ /^webprocess_rss_kb=[0-9]/) { k = substr($i, 19) + 0; if (k > mk) mk = k }
			if ($i ~ /^console_errors=/) ce += substr($i, 16)
			if ($i ~ /^js_errors=/) je += substr($i, 11)
		}
		c[r]++
	}
	END {
		printf "SURVEY-SUM run=%s sites=%d ok=%d error=%d timeout=%d hang=%d crash=%d mean_load_ms=%s max_load_ms=%s max_webprocess_rss_kb=%s console_errors=%d js_errors=%d wall_s=%s\n",
			run, n, c["OK"], c["ERROR"], c["TIMEOUT"], c["HANG"], c["CRASH"], nl ? int(sl / nl) : "-", nl ? ml : "-", mk ? mk : "-", ce, je, wall
	}' "${SUMMARY}"
}

# --- inside the XFCE session: the autostart item -------------------------------------------------
if [ -n "${SURVEY_INNER:-}" ]; then
	trap '[ -n "${BPID}" ] && kill -TERM "${BPID}" 2>/dev/null; echo "SURVEY-SH stopped by the session t=${SECONDS}"; exit 0' TERM
	echo "SURVEY-SH session start run=${NONCE} t=${SECONDS} display=${WAYLAND_DISPLAY:-unset}"
	sites=()
	while read -r line; do sites+=("${line}"); done < "${DIR}/sites.run"
	for line in "${sites[@]}"; do
		# shellcheck disable=SC2086 # "<n> <name> <url>"
		run_one ${line}
	done
	echo "SURVEY-SH session done run=${NONCE} t=${SECONDS}"
	cat "${SUMMARY}" 2>/dev/null
	summarize "${SECONDS}"
	# end the session now rather than at HOLD (the panel's Log Out path)
	[ -n "${XFCE_LOGOUT_FLAG:-}" ] && : > "${XFCE_LOGOUT_FLAG}"
	exit 0
fi

# --- at psh ---------------------------------------------------------------------------------------
[ -r "${SITES}" ] || { echo "SURVEY-SH no site list ${SITES}"; exit 2; }
mkdir -p "${DIR}" || { echo "SURVEY-SH cannot create ${DIR}"; exit 2; }
echo "${NONCE}" > "${OUT_ROOT}/LATEST"
site_list > "${DIR}/sites.run"
count=0
while read -r _; do count=$((count + 1)); done < "${DIR}/sites.run"
[ "${count}" -gt 0 ] || { echo "SURVEY-SH no site selected (only=${ONLY} from=${FROM} to=${TO})"; exit 2; }
: > "${SUMMARY}"
[ "${PROFILE_MODE}" = home ] || rm -rf "${DIR}/profile"
hold=$((count * (LIMIT + DWELL + 60) + 300))
echo "SURVEY-SH begin run=${NONCE} sites=${count} limit=${LIMIT} snap=${SNAP} dwell=${DWELL} size=${SIZE} dmabuf=${DMABUF} gpu=${GPU} stall=${STALL} rss=${RSS} profile=${PROFILE_MODE} env=${EXTRA_ENV[*]:-none} out=${DIR} temp_mC=$(thermal) args=${ARGS_TEXT// /,}"
# TLS needs the clock (no RTC: ntpclient at boot); the network through the host's NAT
year=$(date +%Y)
echo "SURVEY-SH net date=$(date -u +%Y-%m-%dT%H:%M:%SZ) clock=$([ "${year}" -ge 2024 ] && echo set || echo UNSET) https=$(curl -s -m 20 -o /dev/null -w '%{http_code}' https://en.wikipedia.org/ 2>/dev/null) http=$(curl -s -m 20 -o /dev/null -w '%{http_code}' http://info.cern.ch/ 2>/dev/null)"

export SURVEY_NONCE=${NONCE} SURVEY_INNER=1 SURVEY_ARGS="${ARGS_TEXT}" XFCE_AUTOSTART="/bin/bash=${SELF}" HOLD=${HOLD_OPT:-${hold}}
echo "SURVEY-SH session sites=${count} hold=${HOLD}s"
/bin/bash "${SESSION}"
echo "SURVEY-SH session end rc=$?"
unset SURVEY_INNER
echo "SURVEY-SH summary run=${NONCE}"
cat "${SUMMARY}" 2>/dev/null
summarize "${SECONDS}"
echo "SURVEY-SH end run=${NONCE} out=${DIR} temp_mC=$(thermal)"
