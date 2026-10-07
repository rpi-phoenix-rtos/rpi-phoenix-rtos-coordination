#!/bin/bash
#
# stage.sh -- put the streaming-video test pages where the Pi reaches them
# (docs/browser/MSE-DESIGN.md §9.3). Host side, no sudo.
#
#     tools/browser/media/stage.sh [--root DIR] [--export DIR] [--no-export] [--no-fetch]
#
#   1. hls.js: the pinned npm release (HLSJS_VERSION, sha256 in hls.js.sha256) fetched once into
#      external/browser-media/ (git-ignored) and checked; --no-fetch: use what is there.
#   2. the media root (default artifacts/media, what serve-media.py serves; gen-ladders.sh fills its
#      ladders/ and mse/):  pages/ (tools/browser/media/pages/*)  vendor/hls.js/ (hls.min.js,
#      LICENSE, VERSION)  VERSIONS.txt  LICENSES.txt
#   3. the Pi's NFS root (default /srv/phoenix-rpi4-nfs-gcc16; --no-export skips it):
#      <export>/usr/share/browser-media/ gets b8-stream.sh (the Pi-side gate runner), the same pages/
#      and vendor/ (for file:// runs) and the two text files. The media itself stays on the host:
#      the pages fetch it from serve-media.py (serve-for-pi.sh) at http://10.42.0.1:8091/.
#
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "${here}/../../.." && pwd)
root=${MEDIA_ROOT:-${repo}/artifacts/media}
export_root=/srv/phoenix-rpi4-nfs-gcc16
do_export=1
fetch=1
HLSJS_VERSION=1.7.3
while [ $# -gt 0 ]; do
	case "$1" in
		--root) root=$2; shift 2 ;;
		--export) export_root=$2; shift 2 ;;
		--no-export) do_export=0; shift ;;
		--no-fetch) fetch=0; shift ;;
		*) echo "usage: $0 [--root DIR] [--export DIR] [--no-export] [--no-fetch]" >&2; exit 2 ;;
	esac
done
cache=${repo}/external/browser-media
tgz=hls.js-${HLSJS_VERSION}.tgz
mkdir -p "${cache}" "${root}"

if [ ! -s "${cache}/${tgz}" ]; then
	[ "${fetch}" = 1 ] || { echo "stage: ${cache}/${tgz} missing and --no-fetch" >&2; exit 1; }
	echo "stage: fetch hls.js ${HLSJS_VERSION}"
	curl -sSf -o "${cache}/${tgz}.part" "https://registry.npmjs.org/hls.js/-/${tgz}"
	mv "${cache}/${tgz}.part" "${cache}/${tgz}"
fi
(cd "${cache}" && sha256sum -c --quiet "${here}/hls.js.sha256") ||
	{ echo "stage: ${cache}/${tgz} does not match hls.js.sha256" >&2; exit 1; }
licence=$(tar -xzOf "${cache}/${tgz}" package/LICENSE)
case "${licence}" in
	*"Apache License, Version 2.0"*) ;;
	*) echo "stage: hls.js ${HLSJS_VERSION}: LICENSE is not Apache-2.0 any more -- check before staging" >&2; exit 1 ;;
esac

stage_into() {  # stage_into <dir>: pages/, vendor/, VERSIONS.txt, LICENSES.txt
	local dst=$1
	rm -rf "${dst}/pages" "${dst}/vendor"
	mkdir -p "${dst}/pages" "${dst}/vendor/hls.js"
	cp "${here}"/pages/* "${dst}/pages/"
	tar -xzOf "${cache}/${tgz}" package/dist/hls.min.js > "${dst}/vendor/hls.js/hls.min.js"
	tar -xzOf "${cache}/${tgz}" package/LICENSE > "${dst}/vendor/hls.js/LICENSE"
	echo "hls.js ${HLSJS_VERSION} (npm), $(cut -d' ' -f1 "${here}/hls.js.sha256") ${tgz}" > "${dst}/vendor/hls.js/VERSION"
	cp "${here}/LICENSES.txt" "${dst}/LICENSES.txt"
	{
		echo "Phoenix-RTOS streaming-video test set, staged $(date '+%Y-%m-%d %H:%M %z') by tools/browser/media/stage.sh"
		echo "coordination repo $(git -C "${repo}" rev-parse --short HEAD)$(git -C "${repo}" diff --quiet -- tools/browser/media || echo '+local-changes')"
		echo "hls.js ${HLSJS_VERSION} $(cut -d' ' -f1 "${here}/hls.js.sha256")"
		echo "pages: $(cd "${here}/pages" && sha256sum ./* | sha256sum | cut -c1-16) (sha256 of the pages' sums)"
		for m in "${root}"/ladders/*/manifest.json "${root}/mse/manifest.json"; do
			[ -f "${m}" ] || continue
			python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); print("media %s ffmpeg %s generated %s" % (m.get("ladder", "mse"), m["ffmpeg"], m["generated"]))' "${m}"
		done
	} > "${dst}/VERSIONS.txt"
	chmod -R a+rX "${dst}/pages" "${dst}/vendor"
	chmod a+r "${dst}/VERSIONS.txt" "${dst}/LICENSES.txt"
}

stage_into "${root}"
echo "stage: media root ${root} (pages, vendor/hls.js ${HLSJS_VERSION})"
[ -d "${root}/ladders" ] || echo "stage: WARNING no ladders in ${root} yet: run tools/browser/media/gen-ladders.sh"
if [ "${do_export}" = 1 ]; then
	dst=${export_root}/usr/share/browser-media
	mkdir -p "${dst}"
	stage_into "${dst}"
	cp "${here}/pi/b8-stream.sh" "${dst}/b8-stream.sh"
	chmod a+rx "${dst}/b8-stream.sh"
	echo "stage: Pi export ${dst} (b8-stream.sh, pages, vendor)"
fi
