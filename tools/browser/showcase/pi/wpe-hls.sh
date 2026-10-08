#!/bin/bash
#
# wpe-hls.sh -- browser showcase, scene 3a: an HEVC 1080p HLS stream in the WPE WebKit browser,
# decoded by the Pi's HEVC block (hevc_rpivid) and drawn from the decoder's GPU buffers (zero
# copy). The stream is the test set's own ladder (tools/browser/media: every picture has its
# variant burned in, "HEVC 1080p", and a clock), played by the native-HLS page
# b8-hls.html from the host's media server (tools/browser/media/serve-for-pi.sh start). The
# player chooses the HEVC 1080p variant itself. An XFCE_AUTOSTART item:
#
#   /bin/bash=/usr/share/browser-showcase/wpe-hls.sh:55
#
# Knobs: BSHOW_MEDIA (default http://10.42.0.1:8091), BSHOW_HLS_LADDER (default hevc-fmp4),
# BSHOW_VIDEO_SIZE (default 1280x960, /bin/browser's own).
#
# Grading (UART): "BSHOW item=hls start", "B8HLS … playing", "WPEB-MEDIA … decoder
# video=hevc_rpivid … zero_copy=1", "WPEB-MEDIA zero-copy path=external-oes", then
# "WPEB-MEDIA … stat … fps=29–31 … hw=1 … zc=1 zc_painted=<rising>", 0 "Exception #". The host's
# artifacts/media/serve.log shows which variant directory was fetched (hevc-1080).
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
# muted=1 as well: WebKit's own policy lets a muted video autoplay even without --autoplay=allow
url="${MEDIA}/pages/b8-hls.html?src=%2Fladders%2F${ladder}%2Fmaster.m3u8&muted=1&probe=0&run=bshow-hls"

bshow_start hls "url=${url}"
exec /bin/bash /bin/browser "${url}"
