#!/bin/bash
#
# gtk-tabs.sh -- browser showcase, scene 4: the WebKit Browser (WebKitGTK 2.54 + MiniBrowser,
# /usr/bin/webkit-browser) with three tabs that it switches between by itself, and a download in
# the downloads bar. An XFCE_AUTOSTART item:
#
#   /bin/bash=/usr/share/browser-showcase/gtk-tabs.sh:80
#
#   tabs      Wikipedia, GitHub (phoenix-rtos-kernel), the Python documentation (all three loaded
#             cleanly in the site survey), one per argument; --tab-cycle=12 shows the next tab
#             every 12 s
#   download  once the first tab has loaded (~15 s): the CPython 3.14.0 source tarball from
#             python.org (23.6 MB), into /tmp/bshow-downloads (RAM: nothing is left on the NFS
#             root, and a fresh boot never names the file "… (2)")
#
# Knobs: BSHOW_DOWNLOAD_URL (default the tarball; a LAN alternative that needs no internet:
# http://10.42.0.1:8091/demo/phoenix-rpi4-showcase-hevc-1080p30.mp4, 94 MB), BSHOW_TAB_SECS (12),
# BSHOW_GTK_TABS (comma-separated addresses, default the three above).
#
# Grading (UART): "BSHOW item=gtk start", "WKGB … ui start … launcher=b10-r3", "WKGB gdk-gl ok",
# no "Disabled hardware acceleration", "WPEB-WEBKIT swap-chain … type=texture-dmabuf", three
# "WKGB … load finished", "WKGB … download request", "download started", "download destination
# /tmp/bshow-downloads/Python-3.14.0.tar.xz", "download finished … received=23595844" (or the
# size the server sends), "tab switch page=2/3 …", 0 "Exception #".
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

# shellcheck source=common.sh
. /usr/share/browser-showcase/common.sh

download=${BSHOW_DOWNLOAD_URL:-https://www.python.org/ftp/python/3.14.0/Python-3.14.0.tar.xz}
IFS=, read -r -a tabs <<< "${BSHOW_GTK_TABS:-https://en.wikipedia.org/wiki/Raspberry_Pi,https://github.com/phoenix-rtos/phoenix-rtos-kernel,https://docs.python.org/3/library/index.html}"

bshow_start gtk "tabs=${#tabs[@]} download=${download}"
exec /usr/bin/webkit-browser --download-dir=/tmp/bshow-downloads --download="${download}" \
	--tab-cycle="${BSHOW_TAB_SECS:-12}" --present-stats=10 "${tabs[@]}"
