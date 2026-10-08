#!/bin/bash
#
# wpe-sites.sh -- browser showcase, scene 1: the WPE WebKit browser (/bin/browser) on three
# popular sites, driven by synthetic keys typed at a person's pace (wpe-browser --auto). An
# XFCE_AUTOSTART item (item seconds: 120):
#
#   /bin/bash=/usr/share/browser-showcase/wpe-sites.sh:120
#
# Timeline, seconds from the browser's start (the site survey's loads on build 52: Wikipedia
# 14.2 s, DuckDuckGo HTML 1.8 s, GitHub 9.4 s, each from a cold profile):
#
#    0  the start page (local)
#    6  Ctrl+L, "en.wikipedia.org/wiki/Raspberry_Pi" typed from 7 s, Return at 12
#   32  Page_Down x3, 3 s apart (the page has had 20 s to load)
#   44  Ctrl+L, "phoenix rtos" typed, Return at 47: a DuckDuckGo search (plain words)
#   57  Ctrl+L, "github.com/phoenix-rtos/phoenix-rtos-kernel" typed, Return at 64
#   82  Page_Down x3, 4 s apart (the README)
#   96  Alt+Left: back to the search;  102 Alt+Left: back to Wikipedia (the process cache)
#  111  Ctrl+Q: the browser quits by itself, before the item's 120 s are over
# (with the default 12 cs per character; the script computes the times and prints them in its
# BSHOW line)
#
# Knobs (export at psh before the session): BSHOW_SITES_SIZE (default 1600x900),
# BSHOW_TYPE_CS (hundredths of a second per typed character, default 12).
#
# Grading (UART): "BSHOW item=sites start", "WPEB … auto key=ctrl+l", "WPEB … chrome action=go
# source=auto input=en.wikipedia.org/wiki/Raspberry_Pi", "WPEB … load finished uri=https://
# en.wikipedia.org/wiki/Raspberry_Pi", the same for the search and GitHub, "chrome action=back
# source=auto ok=1" twice, "chrome action=quit source=auto", 0 "Exception #".
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

# shellcheck source=common.sh
. /usr/share/browser-showcase/common.sh

export BROWSER_SIZE=${BSHOW_SITES_SIZE:-1600x900}

auto_address 6 en.wikipedia.org/wiki/Raspberry_Pi
wiki=${AUTO_END}
auto_key $((wiki + 20)) Page_Down
auto_key $((wiki + 23)) Page_Down
auto_key $((wiki + 26)) Page_Down

auto_address $((wiki + 32)) "phoenix rtos"
search=${AUTO_END}

auto_address $((search + 10)) github.com/phoenix-rtos/phoenix-rtos-kernel
github=${AUTO_END}
auto_key $((github + 18)) Page_Down
auto_key $((github + 22)) Page_Down
auto_key $((github + 26)) Page_Down

auto_key $((github + 32)) alt+Left
auto_key $((github + 38)) alt+Left
auto_key $((github + 47)) ctrl+q

commas=${AUTO//[!,]/}
bshow_start sites "wikipedia=${wiki}s search=${search}s github=${github}s quit=$((github + 47))s steps=$((${#commas} + 1))"
exec /bin/bash /bin/browser --auto="${AUTO}"
