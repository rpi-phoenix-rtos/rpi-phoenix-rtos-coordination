#!/bin/bash
#
# wpe-mse.sh -- browser showcase, optional scene 3c: the same HEVC ladder as wpe-hls.sh, played by
# hls.js (the player most video sites use) over Media Source Extensions: the page b8-hlsjs.html
# from the host's media server, with hls.js 1.7.3 from its vendor/ directory
# (tools/browser/media/stage.sh). hls.js starts on a low level and its adaptive bitrate climbs, so
# the burned-in label changes on screen (e.g. "HEVC 480p" -> "HEVC 1080p"); every HEVC level is
# decoded by the HEVC block. An XFCE_AUTOSTART item:
#
#   /bin/bash=/usr/share/browser-showcase/wpe-mse.sh:60
#
# Knobs: BSHOW_MEDIA, BSHOW_HLS_LADDER, BSHOW_VIDEO_SIZE (as wpe-hls.sh).
#
# Grading (UART): "BSHOW item=mse start", "B8HLSJS … branch=hls.js", "B8HLSJS … level" switches,
# "WPEB-MEDIA … mse init tracks=1 video=hevc 1920x1080" (after the climb), "WPEB-MEDIA … mse decoder
# video=hevc_rpivid", "WPEB-MEDIA … stat … hw=1", no "B8HLSJS … error" that is fatal, 0
# "Exception #". The stage-1c gate (build 59) climbed to 1080p HEVC with 0 stalls.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

# shellcheck source=common.sh
. /usr/share/browser-showcase/common.sh

export BROWSER_SIZE=${BSHOW_VIDEO_SIZE:-1280x960}
export WPE_BROWSER_AUTOPLAY=allow
export WPE_PHOENIX_MEDIA_STAT_MS=${WPE_PHOENIX_MEDIA_STAT_MS:-5000}
export WPE_BROWSER_PRESENT_SECS=${WPE_BROWSER_PRESENT_SECS:-5}
ladder=${BSHOW_HLS_LADDER:-hevc-fmp4}
url="${MEDIA}/pages/b8-hlsjs.html?src=%2Fladders%2F${ladder}%2Fmaster.m3u8&muted=1&run=bshow-mse"

bshow_start mse "url=${url}"
exec /bin/bash /bin/browser "${url}"
