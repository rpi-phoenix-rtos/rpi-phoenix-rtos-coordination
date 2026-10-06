#!/bin/bash
#
# setup-host-tools.sh -- the host baseline's browsers, without sudo: Playwright (npm) and its
# headless Chromium, Firefox and WebKit builds in external/browser-bench/host-tools (git-ignored).
# Playwright's WebKit is the WPE MiniBrowser; on Ubuntu 26.04 it needs four libraries the host
# lacks (libavif16, libyuv0, libgav1-2, libbacktrace0, libmanette-0.2-0): apt-get download + unpack
# into its own lib directory instead of installing them system-wide.
#
#     tools/browser/bench/host/setup-host-tools.sh
#
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tools=$(cd "${here}/../../../.." && pwd)/external/browser-bench/host-tools
mkdir -p "${tools}/debs"
cd "${tools}"
[ -f package.json ] || npm init -y > /dev/null
npm install --no-audit --no-fund playwright@1.63.0
export PLAYWRIGHT_BROWSERS_PATH=${tools}/browsers
npx playwright install chromium webkit firefox || true   # the host-requirements check fails on WebKit's libraries
cd debs
apt-get download libavif16 libyuv0 libgav1-2 libbacktrace0 libmanette-0.2-0
for d in *.deb; do dpkg-deb -x "${d}" root; done
for lib in "${tools}"/browsers/webkit-*/minibrowser-wpe/lib; do
	cp --update=none root/usr/lib/x86_64-linux-gnu/lib*.so* "${lib}/"
done
echo "host tools ready in ${tools}"
