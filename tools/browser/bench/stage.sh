#!/bin/bash
#
# stage.sh -- fetch the browser benchmark suite at pinned versions and stage it on the Pi's NFS
# root (Phoenix-RTOS browser benchmarks, docs/browser/BENCHMARKS.md). Host side, no sudo.
#
#     tools/browser/bench/stage.sh [--export DIR] [--no-fetch]
#
#   1. sources: external/browser-bench/<name> (git, the pinned commit checked; Acid3 downloaded
#      file by file and checked against the sha256 list below). --no-fetch: use what is there.
#   2. copies them to <export>/usr/share/browser-bench/<name>/ (default export
#      /srv/phoenix-rpi4-nfs-gcc16), without .git
#   3. adds the suite's own files, all clearly ours:
#        phx/                 bench-common.js, the hooks, acid3.html, css3test.html (wrappers)
#        <bench>/bench.html   a copy of the benchmark's index.html with bench-common.js and its
#                             hook added -- index.html itself stays the benchmark's own
#        acid3/.phx-headers.json   Acid3's server behaviour, applied by serve.py
#        tools/serve.py       the HTTP server; bench.sh at the top; index.html (a menu)
#        VERSIONS.txt, LICENSES.txt, results/ (world-writable: the Pi writes logs there)
#   css3test is the one benchmark changed in place: its Google Analytics and Carbon Ads scripts
#   and a remote background image (network loads, nothing to do with the test) are removed
#   from index.html and style.css.
#
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "${here}/../../.." && pwd)
src=${repo}/external/browser-bench
export_root=/srv/phoenix-rpi4-nfs-gcc16
fetch=1
while [ $# -gt 0 ]; do
	case "$1" in
		--export) export_root=$2; shift 2 ;;
		--no-fetch) fetch=0; shift ;;
		*) echo "usage: $0 [--export DIR] [--no-fetch]" >&2; exit 2 ;;
	esac
done
dst=${export_root}/usr/share/browser-bench

# name | repository | commit | what the commit is
pins=(
	"speedometer-3.1|https://github.com/WebKit/Speedometer|1386415be8fef2f6b6bbdbe1828872471c5d802a|branch release/3.1 (Speedometer 3.1)"
	"jetstream-2.2|https://github.com/WebKit/JetStream|332d8ee1e4d5c40d9492c56f6865e64a9525ee9f|branch JetStream2.2 (JetStream 2.2)"
	"motionmark-1.3.2|https://github.com/WebKit/MotionMark|0e740d50f2321d255f6176e2f57493574c735996|tag release/MotionMark1.3.2"
	"css3test|https://github.com/LeaVerou/css3test|bc9731cf1de96361fcb72b7be920b1f481ba5be2|2025-08-21, the last flat static version (no build step, no CDN library)"
)
fetch_git() {  # fetch_git <name> <url> <commit>
	local dir=${src}/$1
	if [ ! -d "${dir}/.git" ]; then
		mkdir -p "${dir}"
		git -C "${dir}" init -q
		git -C "${dir}" remote add origin "$2"
	fi
	if [ "$(git -C "${dir}" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$3" ]; then
		git -C "${dir}" fetch -q --depth 1 origin "$3"
		git -C "${dir}" checkout -q --detach "$3"
	fi
	[ "$(git -C "${dir}" rev-parse HEAD)" = "$3" ] || { echo "stage: $1 is not at $3" >&2; exit 1; }
}

mkdir -p "${src}"
if [ "${fetch}" = 1 ]; then
	for pin in "${pins[@]}"; do
		IFS='|' read -r name url commit _ <<< "${pin}"
		echo "stage: fetch ${name} @ ${commit}"
		fetch_git "${name}" "${url}" "${commit}"
	done
	mkdir -p "${src}/acid3"
	for f in index.html empty.html empty.xml empty.css empty.png empty.txt support-a.png support-b.png \
		support-c.png svg.xml font.svg font.ttf xhtml.1 xhtml.2 xhtml.3 reference.html reference.png; do
		[ -s "${src}/acid3/${f}" ] || curl -sS -o "${src}/acid3/${f}" "http://acid3.acidtests.org/${f}"
	done
fi
# Acid3: the files must be the ones recorded when the suite was made
(cd "${src}/acid3" && sha256sum -c --quiet "${here}/acid3.sha256")

mkdir -p "${dst}"
for pin in "${pins[@]}"; do
	IFS='|' read -r name _ commit _ <<< "${pin}"
	[ "$(git -C "${src}/${name}" rev-parse HEAD)" = "${commit}" ] || { echo "stage: ${name} is not at ${commit}" >&2; exit 1; }
	echo "stage: copy ${name}"
	rm -rf "${dst:?}/${name}"
	mkdir -p "${dst}/${name}"
	git -C "${src}/${name}" archive HEAD | tar -x -C "${dst}/${name}"
done
rm -rf "${dst}/acid3"
mkdir -p "${dst}/acid3"
cp "${src}"/acid3/* "${dst}/acid3/"

# the suite's own files
rm -rf "${dst}/phx" "${dst}/tools"
mkdir -p "${dst}/phx" "${dst}/tools" "${dst}/results/logs"
chmod 0777 "${dst}/results" "${dst}/results/logs"
cp "${here}"/hooks/* "${dst}/phx/"
cp "${here}/serve.py" "${dst}/tools/serve.py"
cp "${here}/pi/bench.sh" "${dst}/bench.sh"
cp "${here}/pi/index.html" "${dst}/index.html"
cp "${here}/acid3-headers.json" "${dst}/acid3/.phx-headers.json"
cp "${here}/jetstream-ab.list" "${dst}/phx/jetstream-ab.list"

python3 - "${dst}" <<'EOF'
import re, sys
dst = sys.argv[1]
MARK = "<!-- Phoenix-RTOS browser benchmark suite: the two scripts below report the run (not part of the benchmark) -->"

def bench_html(path, anchor, add):
    with open(path + "/index.html", encoding="utf-8") as f:
        text = f.read()
    if anchor not in text:
        sys.exit("stage: anchor %r not in %s/index.html" % (anchor, path))
    text = text.replace(anchor, anchor + "\n    " + MARK + "\n    " + "\n    ".join(add), 1)
    with open(path + "/bench.html", "w", encoding="utf-8") as f:
        f.write(text)

bench_html(dst + "/speedometer-3.1", '<script src="resources/main.mjs" type="module"></script>',
           ['<script src="../phx/bench-common.js"></script>',
            '<script src="../phx/speedometer-hook.mjs" type="module"></script>'])
bench_html(dst + "/jetstream-2.2", '<script src="JetStreamDriver.js"></script>',
           ['<script src="../phx/bench-common.js"></script>', '<script src="../phx/jetstream-hook.js"></script>'])
bench_html(dst + "/motionmark-1.3.2/MotionMark", '<script src="resources/runner/benchmark-runner.js" defer></script>',
           ['<script src="../../phx/bench-common.js" defer></script>',
            '<script src="../../phx/motionmark-hook.js" defer></script>'])

# Speedometer's default suites (the ones a stock run runs), for bench.sh speedometer-suites
with open(dst + "/speedometer-3.1/resources/tests.mjs", encoding="utf-8") as f:
    blocks = re.split(r"\n    name: ", f.read())[1:]
suites = [b.split('"')[1] for b in blocks if "\n    disabled: true" not in b.split("\n    name: ")[0]]
if len(suites) != 20:
    sys.exit("stage: expected Speedometer 3.1's 20 default suites, found %d" % len(suites))
with open(dst + "/phx/speedometer-suites.list", "w") as f:
    f.write("\n".join(suites) + "\n")

# css3test: drop the analytics and ad scripts (network loads; the page works offline without them)
p = dst + "/css3test/index.html"
with open(p, encoding="utf-8") as f:
    text = f.read()
n0 = len(text)
text = re.sub(r"[ \t]*<script>var _gaq = .*?</script>\n", "", text)
text = re.sub(r"[ \t]*<script src=\"https://www.google-analytics.com/ga.js\" async></script>\n", "", text)
text = re.sub(r"[ \t]*<script async type=\"text/javascript\" src=\"//cdn.carbonads.com/[^\"]*\" id=\"_carbonads_js\"></script>\n", "", text)
if re.search(r"<script[^>]*src=\"(https?:)?//", text) or n0 == len(text):
    sys.exit("stage: css3test still loads a remote script, or nothing was removed")
with open(p, "w", encoding="utf-8") as f:
    f.write("<!-- Phoenix-RTOS browser benchmark suite: Google Analytics and Carbon Ads scripts removed for offline use -->\n" + text)
p = dst + "/css3test/style.css"
with open(p, encoding="utf-8") as f:
    text = f.read()
remote = "url(https://dabblet.com/img/noise.png) "
if remote not in text:
    sys.exit("stage: css3test/style.css changed: no remote background to remove")
with open(p, "w", encoding="utf-8") as f:
    f.write("/* Phoenix-RTOS browser benchmark suite: remote background image (dabblet.com) removed for offline use */\n" +
            text.replace(remote, ""))
EOF

# what is staged, for the results doc
{
	echo "Phoenix-RTOS browser benchmark suite, staged $(date '+%Y-%m-%d %H:%M %z') by tools/browser/bench/stage.sh"
	echo "coordination repo $(git -C "${repo}" rev-parse --short HEAD)$(git -C "${repo}" diff --quiet -- tools/browser/bench || echo '+local-changes')"
	for pin in "${pins[@]}"; do
		IFS='|' read -r name url commit what <<< "${pin}"
		echo "${name} ${url} ${commit} ${what}"
	done
	echo "acid3 http://acid3.acidtests.org/ fetched 2026-10-06, sha256 in tools/browser/bench/acid3.sha256"
	echo "hooks: $(cd "${here}/hooks" && sha256sum ./* | sha256sum | cut -c1-16) (sha256 of the hook files' sums)"
} > "${dst}/VERSIONS.txt"
cp "${here}/LICENSES.txt" "${dst}/LICENSES.txt"
find "${dst}" -path "${dst}/results" -prune -o -type d -exec chmod a+rx {} + -o -type f -exec chmod a+r {} +
chmod a+rx "${dst}/bench.sh" "${dst}/tools/serve.py"
echo "stage: done ${dst} ($(du -sh "${dst}" | cut -f1))"
cat "${dst}/VERSIONS.txt"
