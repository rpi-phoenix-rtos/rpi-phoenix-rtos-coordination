#!/bin/bash
#
# scene.sh -- one browser-showcase scene as ONE psh command (docs/BROWSER-SHOWCASE-PLAN.md):
#
#     /bin/bash /usr/share/browser-showcase/scene.sh sites|gpu|video|mse|gtk|all
#
# The same as the plan's two psh commands per scene (an `export` of THUNAR_START, HOLD and
# XFCE_AUTOSTART, then `/bin/bash /bin/xfce-session`), without the export's silent idle window in
# a recording cycle: psh-interact waits REC_IDLE_SECS after a command that prints nothing. `all`
# runs the four scenes in one session (a rehearsal: one boot for everything).
#
#   sites  wpe-sites.sh:120                       HOLD 140
#   gpu    wpe-gpu.sh:70                          HOLD 90
#   video  wpe-hls.sh:55, wpe-demo.sh:55          HOLD 130
#   mse    wpe-mse.sh:60 (optional: hls.js)       HOLD 75
#   gtk    gtk-tabs.sh:80                         HOLD 100
#   all    sites, gpu, video, gtk, 5 s apart      HOLD 420
#
# BSHOW_HOLD overrides HOLD. Lines of ours start with "BSHOW ".
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

exec 2>&1
S=/usr/share/browser-showcase
I="/bin/bash=${S}"
case "${1:-}" in
	sites) items="${I}/wpe-sites.sh:120"; hold=140 ;;
	gpu) items="${I}/wpe-gpu.sh:70"; hold=90 ;;
	video) items="${I}/wpe-hls.sh:55,${I}/wpe-demo.sh:55"; hold=130 ;;
	mse) items="${I}/wpe-mse.sh:60"; hold=75 ;;
	gtk) items="${I}/gtk-tabs.sh:80"; hold=100 ;;
	all)
		items="${I}/wpe-sites.sh:120,sleep:5,${I}/wpe-gpu.sh:70,sleep:5,${I}/wpe-hls.sh:55,${I}/wpe-demo.sh:55,sleep:5,${I}/gtk-tabs.sh:80"
		hold=420
		;;
	*)
		echo "usage: /bin/bash ${S}/scene.sh sites|gpu|video|mse|gtk|all"
		exit 2
		;;
esac
export THUNAR_START=0
export HOLD=${BSHOW_HOLD:-${hold}}
export XFCE_AUTOSTART="${items}"
echo "BSHOW scene=$1 hold=${HOLD} autostart=${XFCE_AUTOSTART}"
/bin/bash /bin/xfce-session
echo "BSHOW scene=$1 end rc=$?"
