#!/bin/bash
#
# wpe-demo.sh -- browser showcase, scene 3b: our own HEVC 1080p30 file (the 2026-09-30 showcase
# reel, encoded with the recipe of docs/browser/HEVC-PLATFORMS.md, no audio) played in a web page
# from an HTTP server, as a video site would serve it: <video> in the B8 page, decoded by the
# HEVC block, drawn from its GPU buffers (zero copy). Served by the host's media server
# (tools/browser/media/serve-for-pi.sh start: artifacts/media/demo/). An XFCE_AUTOSTART item:
#
#   /bin/bash=/usr/share/browser-showcase/wpe-demo.sh:55
#
# Knobs: BSHOW_MEDIA (default http://10.42.0.1:8091), BSHOW_DEMO_URL (the clip; default
# ${BSHOW_MEDIA}/demo/phoenix-rpi4-showcase-hevc-1080p30.mp4), BSHOW_VIDEO_SIZE (default 1280x960).
#
# Grading (UART): "BSHOW item=demo start", "B8PAGE … loadedmetadata size=1920x1080",
# "B8PAGE … playing", "WPEB-MEDIA … decoder video=hevc_rpivid … zero_copy=1", "WPEB-MEDIA
# zero-copy path=external-oes", "WPEB-MEDIA … stat … fps=29–31 … hw=1 … zc=1", 0 "Exception #".
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

# shellcheck source=common.sh
. /usr/share/browser-showcase/common.sh

export BROWSER_SIZE=${BSHOW_VIDEO_SIZE:-1280x960}
export WPE_BROWSER_AUTOPLAY=allow
export WPE_PHOENIX_MEDIA_STAT_MS=${WPE_PHOENIX_MEDIA_STAT_MS:-5000}
export WPE_BROWSER_PRESENT_SECS=${WPE_BROWSER_PRESENT_SECS:-5}
clip=${BSHOW_DEMO_URL:-${MEDIA}/demo/phoenix-rpi4-showcase-hevc-1080p30.mp4}
# the reel opens with ~30 s of boot console: jump to its games (Quake III at ~55 s) after 1 s
url="file:///usr/share/wpe-browser/b8.html?src=${clip}&seek=1:${BSHOW_DEMO_FROM:-55}"

bshow_start demo "url=${url}"
exec /bin/bash /bin/browser "${url}"
