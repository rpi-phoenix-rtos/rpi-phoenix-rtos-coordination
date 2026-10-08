#!/bin/bash
#
# wpe-gpu.sh -- browser showcase, scene 2: WebGL and GPU-composited CSS animation in the WPE
# WebKit browser (/bin/browser: GPU raster, dma-buf frames and WebGL are its defaults), on the
# showcase's own page /usr/share/browser-showcase/gpu.html (no network). An XFCE_AUTOSTART item:
#
#   /bin/bash=/usr/share/browser-showcase/wpe-gpu.sh:70
#
# Knobs: BSHOW_GPU_SIZE (default 1600x900), BSHOW_GPU_PAGE (default the showcase page; the B7
# check pages are the proven fallback: file:///usr/share/wpe-browser/b7-webgl.html and
# b7-anim.html).
#
# Grading (UART): "BSHOW item=gpu start", "WPEB … gpu raster=gpu transport=dmabuf … webgl=on",
# "BSHOW-GPU context=webgl2 renderer=…", "BSHOW-GPU fps=…" every 5 s (rising frame count, no
# "BSHOW-GPU error"), "WPEB … present frames= fps= … buffer=dma-buf" every 5 s, 0 "Exception #".
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

# shellcheck source=common.sh
. /usr/share/browser-showcase/common.sh

export BROWSER_SIZE=${BSHOW_GPU_SIZE:-1600x900}
export WPE_BROWSER_PRESENT_SECS=${WPE_BROWSER_PRESENT_SECS:-5}
page=${BSHOW_GPU_PAGE:-file://${SHOWCASE}/gpu.html}

bshow_start gpu "page=${page}"
exec /bin/bash /bin/browser "${page}"
