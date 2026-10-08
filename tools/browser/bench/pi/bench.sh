#!/bin/bash
#
# bench.sh -- the browser benchmark suite on the Pi, one psh command (psh has no pipes, no
# quotes: everything is one /bin/bash run with plain words):
#
#     /bin/bash /usr/share/browser-bench/bench.sh <mode> [arm[,arm...]] [key=value...]
#
#   modes (expected Pi durations: docs/browser/BENCHMARKS.md, "Pre-registration")
#     smoke         after every build, < 10 min: speedometer1, jetstream-ab, acid3, css3test
#     speedometer   Speedometer 3.1, the official 10 iterations
#     speedometer1  Speedometer 3.1, 1 iteration (iter=N for another count)
#     speedometer-suites  each of Speedometer's 20 suites alone, 1 iteration, one browser
#                   each: which suite hangs, crashes or throws (the stock run stops at the first)
#     jetstream     JetStream 2.2, all of it (the WebAssembly part is skipped: no wasm on Phoenix)
#     jetstream-ab  the 16-benchmark subset of jetstream-ab.list (the JIT A/B and the smoke set)
#     motionmark    MotionMark 1.3.2, official (30 s per test, target frame rate measured)
#     motionmark-quick  10 s per test (not a valid score; the graphics paths, quickly)
#     compat        acid3 + css3test
#     suite=NAME    one Speedometer suite (e.g. suite=Perf-Dashboard), 1 iteration
#     test=NAME     one JetStream benchmark (e.g. test=splay)
#     all           compat, speedometer, jetstream, motionmark
#   arms (comma list; each run of the mode runs once per arm, in order; default jit)
#     jit | nojit   the JIT on (default) or JSC_useJIT=false (LLInt only)
#     cpu | gpu     Skia CPU raster (--cpu-rendering) or GPU raster (Ganesh on the V3D, default)
#     shm | dmabuf  frames to labwc through shared memory or as dma-bufs (--dmabuf, default)
#                   The defaults are /bin/browser's since 2026-10-07 (build 47 A/B: GPU+dma-buf
#                   Speedometer 1.256 / MotionMark-quick 40.9 vs CPU+shm 1.168 / 5.0); results
#                   before then with the bare arm "jit" were CPU raster + shared memory.
#     headless      WPEPlatform's headless display, no session (no console lines: titles only;
#                   MotionMark is not valid headless)
#     ahead | opaque  browser=gtk only: webkit-browser --frame-ahead (the web process renders the
#                   next frame while GTK paints this one) | --opaque-frames (an opaque view's
#                   frames drawn without alpha); e.g. gpu,gpu-ahead,gpu-ahead-opaque
#     combined with "-": e.g. jit,nojit  or  jit-cpu-shm,jit-gpu-dmabuf
#   key=value
#     iter=N        Speedometer iterations (speedometer1: 1, speedometer: 10)
#     base=URL      the suite's server (default http://10.42.0.1:8090: serve.py on the netboot
#                   host, tools/browser/bench/host/serve-for-pi.sh); server=pi runs it here,
#                   /bin/python3 on 127.0.0.1:8090
#     timeout=S     every run's limit, instead of the per-benchmark defaults below
#     stall=S       wpe-browser --stall-secs (default 60: a main thread blocked 60 s reports its
#                   threads and stack; 10, the browser's default, floods during long scripts)
#     quiet=1       Speedometer without per-step lines (least reporting while measuring)
#     hold=S        the XFCE session's limit (default: the sum of the run limits + 300 s)
#     env=NAME=VAL  an environment variable for the browser (repeatable), e.g.
#                   env=JSC_jitMemoryReservationSize=67108864 (the JIT pool: 32 MiB on Phoenix)
#                   or env=JSC_useDFGJIT=false / env=JSC_useFTLJIT=false (tier A/B)
#     browser=B     wpe (default: /usr/bin/wpe-browser) or gtk (/usr/bin/webkit-browser, WebKitGTK:
#                   a 1280x800 window, a private session; arms cpu, gpu, dmabuf, shm (WebKit's
#                   WEBKIT_DMABUF_RENDERER_FORCE_SHM=1); no headless). Its runs are named
#                   <bench>-<page>-<arm>-gtk-<nonce>
#     stats=S       the browser's --present-stats=S (both browsers: the frames the view
#                   presented every S s; webkit-browser also its paint watch, gtk-paint lines)
#
# Lines (the browser's own start "WPEB ", the pages' "BENCH <bench> <run> <seq> <kind> ..."):
#   BENCH-SH begin|run|alive|end ...      this script; "alive" every 60 s names the last progress
#                                          line, so a quiet UART still says where the run is
#   BENCH <bench> HUNG run= after_s= last=<the page's last progress line>   no BENCH-DONE in time
#   BENCH <bench> CRASHED run= reason= last=   the web process ended (web-process-terminated)
#   BENCH <bench> EXITED run= rc= last=        the browser ended by itself before BENCH-DONE
#   BENCH-SUM bench= page= arm= run= result=DONE|HUNG|CRASHED|EXITED secs= <the DONE title's fields>
#             temp_mC=<before>/<after> throttled=<after> browser=wpe|gtk   one per run, and all again at the end
# Each run's full output is also in /usr/share/browser-bench/results/logs/<run>.log (the export:
# the host reads it directly); the pages POST their full JSON to the server.
#
# Copyright 2026 Phoenix Systems
#
# This file is part of Phoenix-RTOS.
#
# %LICENSE%

exec 2>&1
SELF=/usr/share/browser-bench/bench.sh
SUITE=${BENCH_SUITE:-/usr/share/browser-bench}   # BENCH_SUITE/BENCH_BROWSER: host dry runs only
BROWSER=${BENCH_BROWSER:-/usr/bin/wpe-browser}
LOGS=${SUITE}/results/logs
export HOME=${HOME:-/root}
export PHX_TRACE_ABORT=1
export THUNAR_START=${THUNAR_START:-0}

# --- arguments (the session's inner run gets them back from BENCH_ARGS) ------------------------
if [ -n "${BENCH_INNER:-}" ]; then
	# shellcheck disable=SC2086 # plain words
	set -- ${BENCH_ARGS}
fi
MODE=${1:-}
[ $# -gt 0 ] && shift
ARMS=jit
if [ $# -gt 0 ] && [ "${1#*=}" = "$1" ]; then
	ARMS=$1
	shift
fi
ITER= BASE=http://10.42.0.1:8090 SERVER=host TIMEOUT= STALL=60 QUIET=0 HOLD_OPT= KIND=wpe STATS=
EXTRA_ENV=()
for kv in "$@"; do
	case "${kv}" in
		iter=*) ITER=${kv#iter=} ;;
		base=*) BASE=${kv#base=} ;;
		server=*) SERVER=${kv#server=} ;;
		timeout=*) TIMEOUT=${kv#timeout=} ;;
		stall=*) STALL=${kv#stall=} ;;
		quiet=*) QUIET=${kv#quiet=} ;;
		hold=*) HOLD_OPT=${kv#hold=} ;;
		env=*=*) EXTRA_ENV+=("${kv#env=}") ;;
		browser=wpe | browser=gtk) KIND=${kv#browser=} ;;
		stats=*) STATS=${kv#stats=} ;;
		*) echo "BENCH-SH bad option ${kv}"; exit 2 ;;
	esac
done
if [ "${KIND}" = gtk ] && [ -z "${BENCH_BROWSER:-}" ]; then
	BROWSER=/usr/bin/webkit-browser
fi
[ "${SERVER}" = pi ] && BASE=http://127.0.0.1:8090
AB=$(tr '\n' ' ' < "${SUITE}/phx/jetstream-ab.list" 2>/dev/null)
NONCE=${BENCH_NONCE:-r$(date +%H%M%S)x${RANDOM}}

# the runs of a mode: <bench>:<page id> (page id selects the URL below)
runs_of() {
	case "$1" in
		smoke) echo "speedometer:speedometer1 jetstream:jetstream-ab acid3:acid3 css3test:css3test" ;;
		speedometer) echo "speedometer:speedometer" ;;
		speedometer1) echo "speedometer:speedometer1" ;;
		speedometer-suites)
			local s
			for s in $(cat "${SUITE}/phx/speedometer-suites.list"); do
				echo "speedometer:suite=${s}"
			done ;;
		jetstream) echo "jetstream:jetstream" ;;
		jetstream-ab) echo "jetstream:jetstream-ab" ;;
		motionmark) echo "motionmark:motionmark" ;;
		motionmark-quick) echo "motionmark:motionmark-quick" ;;
		compat) echo "acid3:acid3 css3test:css3test" ;;
		all) echo "acid3:acid3 css3test:css3test speedometer:speedometer jetstream:jetstream motionmark:motionmark" ;;
		suite=?*) echo "speedometer:$1" ;;
		test=?*) echo "jetstream:$1" ;;
		*) return 1 ;;
	esac
}

# url_of <page> <run id>
url_of() {
	local q
	case "$1" in
		speedometer) q="startAutomatically&iterationCount=${ITER:-10}"; [ "${QUIET}" = 1 ] && q="${q}&phx-quiet=1"
			echo "${BASE}/speedometer-3.1/bench.html?${q}&run=$2" ;;
		speedometer1) q="startAutomatically&iterationCount=${ITER:-1}"; [ "${QUIET}" = 1 ] && q="${q}&phx-quiet=1"
			echo "${BASE}/speedometer-3.1/bench.html?${q}&run=$2" ;;
		suite=*) echo "${BASE}/speedometer-3.1/bench.html?startAutomatically&iterationCount=${ITER:-1}&suite=${1#suite=}&run=$2" ;;
		jetstream) echo "${BASE}/jetstream-2.2/bench.html?report=true&run=$2" ;;
		test=*) echo "${BASE}/jetstream-2.2/bench.html?report=true&test=${1#test=}&run=$2" ;;
		jetstream-ab) q=; for t in ${AB}; do q="${q}&test=${t}"; done
			echo "${BASE}/jetstream-2.2/bench.html?report=true${q}&run=$2" ;;
		motionmark) echo "${BASE}/motionmark-1.3.2/MotionMark/bench.html?run=$2" ;;
		motionmark-quick) echo "${BASE}/motionmark-1.3.2/MotionMark/bench.html?phx-test-interval=10&run=$2" ;;
		acid3) echo "${BASE}/phx/acid3.html?run=$2" ;;
		css3test) echo "${BASE}/phx/css3test.html?run=$2" ;;
	esac
}

# limit_of <page> <arm>: seconds (pre-registered estimates x ~2; nojit x2.5 for the JS-bound)
limit_of() {
	local s
	[ -n "${TIMEOUT}" ] && { echo "${TIMEOUT}"; return; }
	case "$1" in
		speedometer) s=$(( 300 + ${ITER:-10} * 420 )) ;;
		speedometer1) s=$(( 300 + ${ITER:-1} * 420 )) ;;
		suite=*) s=$(( 120 + ${ITER:-1} * 120 )) ;;
		jetstream) s=5400 ;;
		test=*) s=600 ;;
		jetstream-ab) s=1500 ;;
		motionmark) s=900 ;;
		motionmark-quick) s=600 ;;
		acid3 | css3test) s=300 ;;
	esac
	case "$2" in *nojit*) case "$1" in acid3 | css3test | motionmark*) ;; *) s=$(( s * 5 / 2 )) ;; esac ;; esac
	echo "${s}"
}

thermal() {  # milli-degrees C, or "-"
	local t
	t=$(timeout 3 cat /dev/thermal 2>/dev/null) || t=-
	echo "${t:--}" | tr -d ' \n'
}
throttled() {
	local t
	t=$(timeout 3 cat /dev/throttled 2>/dev/null) || t=-
	echo "${t:--}" | tr -d ' \n'
}

pids=()
BPID= JOBS_BEFORE=
new_jobs() {  # this run's jobs still running (the browser | tee pipeline), not the ones before it
	local p
	for p in $(jobs -rp); do
		case "${JOBS_BEFORE}" in *" ${p} "*) ;; *) echo "${p}" ;; esac
	done
}
pause() {  # interruptible by the session's SIGTERM
	sleep "$1" &
	wait $!
}

# the page's last progress line in a log (the BENCH line framing, cut short)
last_line() {
	grep -a -o "BENCH $1 $2 [0-9]* [a-z-]* .*" "$3" 2>/dev/null | grep -a -v ' json ' | tail -n 1 | cut -c1-240 |
		sed "s/^BENCH $1 $2 //"
}

# run_one <bench> <page> <arm>
run_one() {
	local bench=$1 page=$2 arm=$3 id url limit log pidf t0 next_beat status=HUNG secs extra=() envs=() rc done_kv temp0 reason last
	id=${bench}-$(echo "${page}" | tr -c 'A-Za-z0-9.\n-' '_' | cut -c1-40)-${arm}-${NONCE}
	[ "${KIND}" = gtk ] && id=${id%-"${NONCE}"}-gtk-${NONCE}
	url=$(url_of "${page}" "${id}")
	limit=$(limit_of "${page}" "${arm}")
	log=${LOGS}/${id}.log
	pidf=/tmp/bench-${id}.pid
	envs=("${EXTRA_ENV[@]}")
	case "${arm}" in *nojit*) envs+=(JSC_useJIT=false) ;; esac
	case "${arm}" in *cpu*) extra+=(--cpu-rendering) ;; esac
	if [ "${KIND}" = gtk ]; then
		# webkit-browser (WebKitGTK): dma-buf frames whenever GDK has GL; shared memory on request
		case "${arm}" in *shm*) envs+=(WEBKIT_DMABUF_RENDERER_FORCE_SHM=1) ;; esac
		case "${arm}" in *ahead*) extra+=(--frame-ahead) ;; esac
		case "${arm}" in *opaque*) extra+=(--opaque-frames) ;; esac
		extra+=(--size=1280x800 --private)
	else
		case "${arm}" in *shm*) extra+=(--shm) ;; *headless*) ;; *) extra+=(--dmabuf) ;; esac
		case "${arm}" in *headless*) extra+=(--headless) ;; esac
		extra+=(--size=1280x800 --toolbar=never --ephemeral --stall-secs="${STALL}" --hang-recovery=0)
	fi
	[ -n "${STATS}" ] && extra+=(--present-stats="${STATS}")
	temp0=$(thermal)
	echo "BENCH-SH run id=${id} bench=${bench} page=${page} arm=${arm} limit_s=${limit} temp_mC=${temp0} url=${url}"
	echo "BENCH-SH run id=${id} args=${extra[*]} env=${envs[*]:-none}" > "${log}"
	t0=${SECONDS}
	JOBS_BEFORE=" $(jobs -rp | tr '\n' ' ') "
	(
		unset WEBKIT_SKIA_ENABLE_CPU_RENDERING
		env "${envs[@]}" "${BROWSER}" "${extra[@]}" "${url}" 2>&1 &
		echo $! > "${pidf}"
		wait $!
		echo "BENCH-SH browser-exit rc=$?"
	) | tee -a "${log}" &
	pids=($!)
	next_beat=60
	pause 2
	BPID=$(cat "${pidf}" 2>/dev/null)
	while :; do
		secs=$(( SECONDS - t0 ))
		if grep -a -q "title BENCH-DONE ${bench} .*run=${id}" "${log}"; then
			status=DONE; break
		fi
		if grep -a -q "web-process-terminated" "${log}"; then
			status=CRASHED; break
		fi
		if grep -a -q "^BENCH-SH browser-exit" "${log}"; then
			status=EXITED; break
		fi
		[ "${secs}" -ge "${limit}" ] && { status=HUNG; break; }
		if [ "${secs}" -ge "${next_beat}" ]; then
			echo "BENCH-SH alive id=${id} t=${secs}s temp_mC=$(thermal) last=$(last_line "${bench}" "${id}" "${log}")"
			next_beat=$(( next_beat + 60 ))
		fi
		pause 5
	done
	last=$(last_line "${bench}" "${id}" "${log}")
	case "${status}" in
		HUNG) echo "BENCH ${bench} HUNG run=${id} after_s=${secs} last=${last:-none}" ;;
		CRASHED)
			reason=$(grep -a -m1 -o 'web-process-terminated reason=[a-z-]*' "${log}" | cut -d= -f2)
			echo "BENCH ${bench} CRASHED run=${id} after_s=${secs} reason=${reason} last=${last:-none}" ;;
		EXITED)
			rc=$(grep -a -m1 -o 'browser-exit rc=[0-9]*' "${log}" | cut -d= -f2)
			echo "BENCH ${bench} EXITED run=${id} after_s=${secs} rc=${rc} last=${last:-none}" ;;
		DONE) pause 3 ;;  # the page's last lines, the POST
	esac
	# end the browser; its subshell writes "BENCH-SH browser-exit" into the log once it is gone
	# (the browser is not our job: no jobs/kill -0 checks on it)
	exited() { grep -a -q '^BENCH-SH browser-exit' "${log}"; }
	if [ -n "${BPID}" ] && ! exited; then
		kill -TERM "${BPID}" 2>/dev/null
		for _ in 1 2 3 4 5 6 7 8; do exited && break; pause 2; done
		if ! exited; then
			echo "BENCH-SH kill -KILL ${BPID} (no exit 16 s after SIGTERM)"
			kill -KILL "${BPID}" 2>/dev/null
			for _ in 1 2 3 4 5; do exited && break; pause 2; done
		fi
	fi
	# the pipe (tee) closes when the browser's children are gone too (orphans exit within ~2 s)
	for _ in 1 2 3 4 5 6; do [ -z "$(new_jobs)" ] && break; pause 2; done
	if [ -n "$(new_jobs)" ]; then
		echo "BENCH-SH WARNING run=${id}: output pipe still open (a child of the browser lives on); not waiting for it"
	else
		wait "${pids[0]}" 2>/dev/null
	fi
	BPID=
	rm -f "${pidf}"
	done_kv=$(grep -a -m1 -o "title BENCH-DONE ${bench} .*run=${id}" "${log}" | sed "s/^title BENCH-DONE ${bench} //; s/ run=${id}\$//")
	echo "BENCH-SUM bench=${bench} page=${page} arm=${arm} run=${id} result=${status} secs=${secs} ${done_kv:-score=NaN} temp_mC=${temp0}/$(thermal) throttled=$(throttled) browser=${KIND}" |
		tee -a "${LOGS}/summary-${NONCE}.txt"
	pause 5
}

run_all() {  # run_all <window|headless>: the runs of MODE x ARMS for that display
	local arm r bench page
	for arm in ${ARMS//,/ }; do
		case "${arm}" in *headless*) [ "$1" = headless ] || continue ;; *) [ "$1" = window ] || continue ;; esac
		case "${KIND}-${arm}" in gtk-*headless*) echo "BENCH-SH skip arm=${arm}: webkit-browser has no headless display"; continue ;; esac
		for r in $(runs_of "${MODE}"); do
			bench=${r%%:*} page=${r#*:}
			run_one "${bench}" "${page}" "${arm}"
		done
	done
}

# --- inside the XFCE session: the autostart item -------------------------------------------------
if [ -n "${BENCH_INNER:-}" ]; then
	trap '[ -n "${BPID}" ] && kill -TERM "${BPID}" 2>/dev/null; echo "BENCH-SH stopped by the session t=${SECONDS}"; exit 0' TERM
	echo "BENCH-SH session start t=${SECONDS} display=${WAYLAND_DISPLAY:-unset}"
	run_all window
	echo "BENCH-SH session done t=${SECONDS}"
	# end the session now rather than at HOLD (the panel's Log Out path)
	[ -n "${XFCE_LOGOUT_FLAG:-}" ] && : > "${XFCE_LOGOUT_FLAG}"
	exit 0
fi

# --- at psh ---------------------------------------------------------------------------------------
if ! runs_of "${MODE}" > /dev/null; then
	sed -n '3,30p' "${SELF}"
	exit 2
fi
mkdir -p "${LOGS}"
echo "BENCH-SH begin mode=${MODE} arms=${ARMS} browser=${KIND} nonce=${NONCE} base=${BASE} server=${SERVER} iter=${ITER:-default} stall=${STALL} stats=${STATS:-none} env=${EXTRA_ENV[*]:-none} temp_mC=$(thermal) throttled=$(throttled) uptime=$(uptime 2>/dev/null | tr -s ' ')"
echo "BENCH-SH versions $(head -n 1 "${SUITE}/VERSIONS.txt" | cut -c1-120)"
if [ "${SERVER}" = pi ]; then
	/bin/python3 "${SUITE}/tools/serve.py" --root "${SUITE}" --results "${SUITE}/results" --bind 127.0.0.1 --port 8090 &
	server_pid=$!
fi
ok=0
for _ in $(seq 1 45); do
	if curl -s -m 5 -o /dev/null "${BASE}/phx-ping"; then ok=1; break; fi
	sleep 2
done
if [ "${ok}" != 1 ]; then
	echo "BENCH-SH SERVER-UNREACHABLE base=${BASE} (host: tools/browser/bench/host/serve-for-pi.sh start; or server=pi)"
	[ -n "${server_pid:-}" ] && kill "${server_pid}"
	exit 3
fi
echo "BENCH-SH server ok $(curl -s -m 5 "${BASE}/phx-ping" | tr -d '\n')"

export BENCH_NONCE=${NONCE}
run_all headless

window_runs=0 hold=300
for arm in ${ARMS//,/ }; do
	case "${arm}" in *headless*) continue ;; esac
	for r in $(runs_of "${MODE}"); do
		window_runs=$(( window_runs + 1 ))
		hold=$(( hold + $(limit_of "${r#*:}" "${arm}") + 40 ))
	done
done
if [ "${window_runs}" -gt 0 ]; then
	export BENCH_INNER=1 BENCH_ARGS="${MODE} ${ARMS} $*" XFCE_AUTOSTART="/bin/bash=${SELF}" HOLD=${HOLD_OPT:-${hold}}
	echo "BENCH-SH session runs=${window_runs} hold=${HOLD}s"
	/bin/bash /bin/xfce-session
	echo "BENCH-SH session end rc=$?"
	unset BENCH_INNER
fi
[ -n "${server_pid:-}" ] && kill "${server_pid}" 2>/dev/null
echo "BENCH-SH summary nonce=${NONCE}"
cat "${LOGS}/summary-${NONCE}.txt" 2>/dev/null
echo "BENCH-SH end mode=${MODE} nonce=${NONCE} temp_mC=$(thermal)"
