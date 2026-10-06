#!/bin/bash
#
# run-host-baseline.sh -- the browser benchmark suite on the build host, for context next to the
# Pi's numbers (docs/browser/BENCHMARKS.md). Same staged copies, same hooks, same server; the
# browsers are Playwright's headless Chromium, Firefox and WebKit (WPE MiniBrowser), from
# external/browser-bench/host-tools (setup-host-tools.sh).
#
#     tools/browser/bench/host/run-host-baseline.sh [--out DIR] [--port N] [--browsers "chromium webkit firefox"]
#                                                   [--runs "speedometer jetstream motionmark acid3 css3test b9 ..."]
#
#   runs: speedometer (10 iterations, official), speedometer1 (1 iteration), jetstream (full),
#         jetstream-ab (the JIT A/B subset bench.sh uses), motionmark (30 s tests, official),
#         acid3, css3test, b9 (the B9 JavaScript page), and the -nojit variants of speedometer,
#         speedometer1, jetstream-ab and b9 (JSC_useJIT=false: WebKit only)
#   One log per browser x run in DIR (default artifacts/browser-bench/host-<stamp>), the hooks'
#   JSON reports beside them, and DIR/summary.txt: one line per run.
#
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "${here}/../../../.." && pwd)
tools=${repo}/external/browser-bench/host-tools
root=/srv/phoenix-rpi4-nfs-gcc16/usr/share/browser-bench
out=${repo}/artifacts/browser-bench/host-$(date +%Y%m%d-%H%M%S)
port=8091
browsers="chromium webkit firefox"
runs="acid3 css3test b9 b9-nojit speedometer1 speedometer jetstream-ab jetstream-ab-nojit jetstream motionmark"
while [ $# -gt 0 ]; do
	case "$1" in
		--out) out=$2; shift 2 ;;
		--port) port=$2; shift 2 ;;
		--browsers) browsers=$2; shift 2 ;;
		--runs) runs=$2; shift 2 ;;
		*) echo "usage: $0 [--out DIR] [--port N] [--browsers LIST] [--runs LIST]" >&2; exit 2 ;;
	esac
done
mkdir -p "${out}"
export NODE_PATH=${tools}/node_modules PLAYWRIGHT_BROWSERS_PATH=${tools}/browsers PLAYWRIGHT_SKIP_VALIDATE_HOST_REQUIREMENTS=1
# shellcheck source=../jetstream-ab.list
ab=$(tr '\n' ' ' < "${here}/../jetstream-ab.list")
ab_query=$(for t in ${ab}; do printf '&test=%s' "${t}"; done)

python3 "${here}/../serve.py" --root "${root}" --results "${out}" --bind 127.0.0.1 --port "${port}" > "${out}/serve.log" 2>&1 &
server=$!
trap 'kill ${server} 2>/dev/null' EXIT
for _ in $(seq 50); do curl -sf "http://127.0.0.1:${port}/phx-ping" > /dev/null && break; sleep 0.2; done
base=http://127.0.0.1:${port}

{
	echo "# host baseline $(date '+%Y-%m-%d %H:%M %z') $(uname -srm) $(nproc) cpus, $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //')"
	echo "# $(cat "${root}/VERSIONS.txt" | head -1)"
} > "${out}/summary.txt"

for browser in ${browsers}; do
	for r in ${runs}; do
		env=()
		done_prefix="BENCH-DONE "
		timeout=1800
		case "${r}" in
			*-nojit)
				[ "${browser}" = webkit ] || continue
				env=(--env JSC_useJIT=false) ;;
		esac
		id="host-${browser}-${r}"
		case "${r%-nojit}" in
			speedometer) url="${base}/speedometer-3.1/bench.html?startAutomatically&run=${id}" ;;
			speedometer1) url="${base}/speedometer-3.1/bench.html?startAutomatically&iterationCount=1&run=${id}" ;;
			jetstream) url="${base}/jetstream-2.2/bench.html?report=true&run=${id}"; timeout=3600 ;;
			jetstream-ab) url="${base}/jetstream-2.2/bench.html?report=true${ab_query}&run=${id}" ;;
			motionmark) url="${base}/motionmark-1.3.2/MotionMark/bench.html?run=${id}" ;;
			acid3) url="${base}/phx/acid3.html?run=${id}"; timeout=300 ;;
			css3test) url="${base}/phx/css3test.html?run=${id}"; timeout=300 ;;
			b9) url="file:///srv/phoenix-rpi4-nfs-gcc16/usr/share/wpe-browser/b9.html"; done_prefix="B9-JS total="; timeout=300 ;;
			*) echo "unknown run ${r}" >&2; continue ;;
		esac
		log=${out}/${browser}-${r}.log
		load=$(cut -d' ' -f1-3 /proc/loadavg)
		t0=$(date +%s)
		node "${here}/pw-run.mjs" --browser "${browser}" --timeout "${timeout}" "${env[@]}" --done "${done_prefix}" "${url}" > "${log}" 2>&1
		rc=$?
		secs=$(( $(date +%s) - t0 ))
		result=$(grep -a -m1 -o -E "title (BENCH-DONE|B9-JS total=).*" "${log}" | cut -c7-300)
		version=$(grep -a -m1 -o 'version=[^ ]*' "${log}")
		echo "${browser} ${r} rc=${rc} secs=${secs} ${version} load=${load// /,} ${result:-NO-RESULT}" | tee -a "${out}/summary.txt"
	done
done
echo "host baseline: ${out}"
