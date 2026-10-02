#!/usr/bin/env bash
#
# Assemble the B3 benchmark/test bundle for `jsc` in <dest> (a directory to stage next to the
# binary, e.g. on the NFS root). Third-party JS (SunSpider, test262) is downloaded at pinned
# revisions and sha256-verified, never committed. See ../README.md, section "Pi check".
#
#   <dest>/micro.js, sunspider-run.js, test262-run.js,  (this directory), hello.js (print(1+1))
#          jit-check.js (B9)
#   <dest>/sunspider/LIST + 26 tests                    SunSpider 1.0.2, WebKit tag webkitgtk-2.54.0
#   <dest>/test262-subset.json                          test262 7a096c20 (the revision WebKit 2.54
#                                                       imports), every 10th test, test262-bundle.py
#   <dest>/mallocrate, mallocrate-mimalloc              copied from <build-out> when present
#
# Usage: tools/browser/jsc/bench/fetch-bench.sh <dest> [--dl <dir>] [--build-out <dir>] [--stride N]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dest="${1:?usage: fetch-bench.sh <dest> [--dl <dir>] [--build-out <dir>] [--stride N]}"
shift
dl=""
bout=""
stride=10
while [ $# -gt 0 ]; do
	case "$1" in
		--dl) shift; dl="${1:?}" ;;
		--build-out) shift; bout="${1:?}" ;;
		--stride) shift; stride="${1:?}" ;;
		*) echo "fetch-bench.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
[ -n "${dl}" ] || dl="${dest}/.dl"
mkdir -p "${dest}/sunspider" "${dl}"

SUNSPIDER_TAG=webkitgtk-2.54.0
SUNSPIDER_URL="https://raw.githubusercontent.com/WebKit/WebKit/${SUNSPIDER_TAG}/PerformanceTests/SunSpider/tests/sunspider-1.0.2"
SUNSPIDER=(
	"28b521f6f34e989c0a3baedb4d6a0ff7554e3925d5b99978c0bf10d8c02567b3 LIST"
	"3b140ae1a61a98b37fa82fa7e4d390c00692a99eb76f5bb6dd67558049f82b5d 3d-cube.js"
	"c765d13c9f8e958c072f8d821d560f5d86ae36eb92ef33794fda8aefd6e1b240 3d-morph.js"
	"87a1cb968113dcaf427dc2634e95f6ee6460f38e132c26cdf640639521620591 3d-raytrace.js"
	"af16d6f52b448094138cfd8e5f6e24c8d60772654463a4a8627cd54f910f93b5 access-binary-trees.js"
	"a768076e7bae5e4e52fd6709732ccd52bcf1299bea7e464c02b905d189bb7f69 access-fannkuch.js"
	"84f08150e27c075e4a6b1b900b743cc2845abd3a47175cd444f6a1178493487c access-nbody.js"
	"ef62b42b6f926d61d9741a8e57b2758a9aea28ad2d1ee1e7d4747957d00fdc20 access-nsieve.js"
	"908076ee39ddf74f3d6be54b7f7c78fae8ddc9a308849f1ab1e40f1676779b41 bitops-3bit-bits-in-byte.js"
	"4f1a917cd52ebc07ec30dbe9d3e369e03c09825f9d2898fc3403ebd34a9bd853 bitops-bits-in-byte.js"
	"65ce384a6a7bf6a3d7590bb3efb7b275d9964fdf55bd964bc226be8821da47e2 bitops-bitwise-and.js"
	"de7b6de8f565b877dfc5449c6b23d808fa22058a662a81421f1810cbbdc8b6bc bitops-nsieve-bits.js"
	"7689048105ae415ad60df2a882384063df640371d806d7a7d91446f2881b7d83 controlflow-recursive.js"
	"7151c362dd1d10ec6bf8bd332a57bf96aa3d6bf64a87695b7b670af09e430365 crypto-aes.js"
	"5a2688c93d64256f7fb19a771240050da2a3c37a1cadd5d802d82ba3d39f5d20 crypto-md5.js"
	"967f4dda44c6225118cdaaca3fdf4302c12b0ea44ecf553470ffc8b4b6bc1aa1 crypto-sha1.js"
	"cbefaffbecb6769a85f5877765b21f967a1cff5ab2625d4a9066c050fcdc7b5e date-format-tofte.js"
	"8c5dae670a78939e88e46baaf14b75250001c7d73116613e54ad86c4f0c03eba date-format-xparb.js"
	"ad8710fb0e502fb0d214179751102861eff3b3d5c4603344e3129afa693305a6 math-cordic.js"
	"3c0cca9004d6e1d1c651addeaaf59dad90e7df15973eca7c8948dd3d758c7d0c math-partial-sums.js"
	"d57283fcb398ed3fd512991068768c5a20b8af3aa8c69ed0d6dc5416f6d0023f math-spectral-norm.js"
	"0897d1123aa12283a6865cb5e5925db8d09a08097c6fc51117e21f51c76d4135 regexp-dna.js"
	"48e6106fc6df6cb725b2c56934aff1591c97527862d27accfc11e419467639fc string-base64.js"
	"b790324ca6c5caa25da93638fa0cf9a9014a3764cbf4f04b4ff3babf6999f6dd string-fasta.js"
	"9634886bcb846c76141f97b681b33a11df444901e0b4c899d8f72a3b6544f9d5 string-tagcloud.js"
	"6ff9856ad51b877ef29262b942015ec04dd2ffe916b5adc85324aee2a144d382 string-unpack-code.js"
	"518ed0c67fde0c0d65af6238b91f5e498d8ba5d188c428f4a76dd498704737d5 string-validate-input.js"
)

TEST262_REV=7a096c205fd422ecba49a407d5ac4d1b3f842296
TEST262_SHA=54088b23164536010a7b5cdfab8b0036e0b0220c110d37a1895fab9fc61b9486
TEST262_TGZ="test262-${TEST262_REV}.tar.gz"

for rec in "${SUNSPIDER[@]}"; do
	sum="${rec%% *}"
	f="${rec#* }"
	if [ ! -f "${dest}/sunspider/${f}" ] || ! echo "${sum}  ${dest}/sunspider/${f}" | sha256sum -c --quiet - 2> /dev/null; then
		curl -sSfL -o "${dest}/sunspider/${f}" "${SUNSPIDER_URL}/${f}"
	fi
	echo "${sum}  ${dest}/sunspider/${f}" | sha256sum -c --quiet - || { echo "fetch-bench.sh: ${f}: sha256 mismatch" >&2; exit 1; }
done
echo "sunspider: ${#SUNSPIDER[@]} files verified (${SUNSPIDER_TAG})"

if [ ! -f "${dl}/${TEST262_TGZ}" ]; then
	curl -sSfL -o "${dl}/${TEST262_TGZ}.part" "https://github.com/tc39/test262/archive/${TEST262_REV}.tar.gz"
	mv "${dl}/${TEST262_TGZ}.part" "${dl}/${TEST262_TGZ}"
fi
echo "${TEST262_SHA}  ${dl}/${TEST262_TGZ}" | sha256sum -c --quiet - || { echo "fetch-bench.sh: test262 sha256 mismatch" >&2; exit 1; }
t262="${dl}/test262-${TEST262_REV}"
if [ ! -d "${t262}" ]; then
	tar -xzf "${dl}/${TEST262_TGZ}" -C "${dl}"
fi
echo "${TEST262_REV}" > "${t262}/.test262-revision"
python3 "${here}/test262-bundle.py" "${t262}" "${dest}/test262-subset.json" --stride "${stride}"

cp "${here}/micro.js" "${here}/sunspider-run.js" "${here}/test262-run.js" "${here}/jit-check.js" "${dest}/"
# Step 1's fallback when an argument with parentheses does not survive the shell.
echo 'print(1+1)' > "${dest}/hello.js"
if [ -n "${bout}" ]; then
	for b in mallocrate mallocrate-mimalloc; do
		[ -f "${bout}/${b}" ] && cp "${bout}/${b}" "${dest}/"
	done
fi
echo "bundle ready: ${dest}"
