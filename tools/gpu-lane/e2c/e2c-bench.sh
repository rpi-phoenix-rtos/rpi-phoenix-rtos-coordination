#!/bin/sh
#
# e2c-bench.sh — PID 1 of the Raspberry Pi OS reference boot for experiment E2c
# (docs/gpu-new-lane/E2c-pios-baseline.md). Installed into the Linux netboot
# rootfs as /usr/local/bin/e2c-bench.sh and started with
# `init=/usr/local/bin/e2c-bench.sh` (systemd never runs: Pi OS first-boot
# services reboot-loop over an NFS root).
#
# Runs the same benchmarks Phoenix runs, with the same settings, over the stock
# Pi OS KMS/DRM stack (vc4-kms-v3d + Mesa v3d), then powers the board off.
#
# OUTPUT. The UART is the only live channel, and a slow serial console blocks
# whoever writes to it, so benchmark output never goes to the console: each
# benchmark writes to a file on the NFS root (/var/log/e2c/run-NNN/, readable on
# the host under artifacts/linux-netboot/rootfs/var/log/e2c/) and only short
# tagged lines `E2C <bench> ...` reach the console. `E2C DONE` is the last line.
#
# KNOBS (kernel command line, set by scripts/e2c-cycle.sh):
#   e2c.arm=<label>        free-form label echoed into every summary (clock arm)
#   e2c.only=a,b,...       benchmarks to run, in order (default below)
#   e2c.stkruns=N          SuperTuxKart runs in this boot (default 2: the first
#                          may pay shader compiles, the second is the warm one)
#   e2c.label=<label>      the host cycle's label, echoed in `E2C boot` so the host
#                          can match this boot's results directory to its run
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games
export PATH

# This script powers the machine off at the end (sysrq 'o'). Refuse to run as
# anything but PID 1 so it can never be started by mistake on the host or in a
# host-side chroot of the rootfs.
if [ "$$" != 1 ]; then
	echo "e2c-bench.sh: must run as PID 1 (init=); refusing" >&2
	exit 1
fi

mount -t proc     proc  /proc     2>/dev/null
mount -t sysfs    sys   /sys      2>/dev/null
mount -t devtmpfs dev   /dev      2>/dev/null
mkdir -p /dev/pts /dev/shm
mount -t devpts   devpts /dev/pts 2>/dev/null
mount -t tmpfs    tmpfs /dev/shm  2>/dev/null
mount -t tmpfs    tmpfs /run      2>/dev/null
mount -t tmpfs    tmpfs /tmp      2>/dev/null
mount -t debugfs  debugfs /sys/kernel/debug 2>/dev/null

exec </dev/null >/dev/console 2>&1
# Keep kernel messages off the console we grade (warnings and worse only).
dmesg -n 4 2>/dev/null

echo ""
echo "===== E2C START (PID1) ====="

# ---------------------------------------------------------------- parameters
arg() { # arg <name> <default>
	v=$(tr ' ' '\n' </proc/cmdline | sed -n "s/^e2c\.$1=//p" | tail -1)
	[ -n "$v" ] && echo "$v" || echo "$2"
}
ARM=$(arg arm stock)
ONLY=$(arg only stk,quake,kmscube,glmark2,vkmark)
STKRUNS=$(arg stkruns 2)
LABEL=$(arg label none)

# Monotonic seconds since boot (the Pi has no RTC; wall time is meaningless).
up() { cut -d' ' -f1 /proc/uptime; }

# One results directory per boot on the NFS root.
base=/var/log/e2c
mkdir -p "$base"
n=1
while [ -e "$base/run-$(printf %03d $n)" ]; do n=$((n + 1)); done
RES="$base/run-$(printf %03d $n)"
mkdir -p "$RES"
ln -sfn "$(basename "$RES")" "$base/latest"
SUMMARY="$RES/summary.txt"

# say: one tagged line to the console AND the summary file.
say() { echo "E2C $*"; echo "E2C $*" >>"$SUMMARY"; }

say boot arm=$ARM label=$LABEL only=$ONLY stkruns=$STKRUNS results=$RES
say cmdline $(cat /proc/cmdline)

# ---------------------------------------------------------------- drivers
# No udev: the GPU drivers are modules and nothing loads them for us.
modprobe vc4 2>&1 | head -3
modprobe v3d 2>&1 | head -3
i=0
while [ $i -lt 40 ]; do
	ls /dev/dri/renderD128 >/dev/null 2>&1 && ls /dev/dri/card* >/dev/null 2>&1 && break
	sleep 0.5; i=$((i + 1))
done
# The KMS card is the one bound to vc4 (v3d is render-only). The v3d device's
# sysfs gpu_stats (kernel >= 6.8) gives per-queue job counts and busy time: the
# Linux counterpart of the Phoenix E2 bin/render spin times.
CARD=""
V3DSTATS=""
for c in /sys/class/drm/card[0-9]; do
	[ -e "$c" ] || continue
	drv=$(basename "$(readlink -f "$c/device/driver")" 2>/dev/null)
	say drm $(basename "$c") driver=$drv
	case "$drv" in
	*vc4*) CARD=/dev/dri/$(basename "$c") ;;
	*v3d*) [ -r "$c/device/gpu_stats" ] && V3DSTATS="$c/device/gpu_stats" ;;
	esac
done
say drm kms_card=${CARD:-NONE} v3d_stats=${V3DSTATS:-NONE} render=$(ls /dev/dri/renderD* 2>/dev/null | tr '\n' ' ')
for s in /sys/class/drm/card*-HDMI-A-*; do
	[ -e "$s/status" ] || continue
	say hdmi $(basename "$s") status=$(cat "$s/status") preferred=$(head -1 "$s/modes" 2>/dev/null)
done
# The ACTIVE mode is in the atomic state (debugfs), not in the connector's mode list.
drm_mode() { grep -h 'mode: "' /sys/kernel/debug/dri/*/state 2>/dev/null | sed 's/^[[:space:]]*//' | head -2 | tr '\n' ' '; }
sleep 1
say kms-mode console $(drm_mode)
# Persistent (NFS) shader cache, so a second boot of the same arm runs warm, as
# the Phoenix E2/E2b trials did (`Mesa shader disk cache KEPT`).
export MESA_SHADER_CACHE_DIR=/var/cache/e2c-mesa
mkdir -p "$MESA_SHADER_CACHE_DIR"
say mesa-cache dir=$MESA_SHADER_CACHE_DIR files_before=$(find "$MESA_SHADER_CACHE_DIR" -type f 2>/dev/null | wc -l)

# ---------------------------------------------------------------- governor + clocks
gov_before=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
	echo performance >"$g" 2>/dev/null
done
say governor before=$gov_before now=$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort -u | tr '\n' ' ')
say cpufreq max_khz=$(cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq 2>/dev/null) cur_khz=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null)

say uname $(uname -a)
say model $(tr -d '\0' </proc/device-tree/model 2>/dev/null)
say revision $(sed -n 's/^Revision[[:space:]]*: //p' /proc/cpuinfo | head -1) mem_kb=$(sed -n 's/^MemTotal: *//p' /proc/meminfo)
say firmware $(vcgencmd version 2>/dev/null | tr '\n' ' ')
for k in arm_freq core_freq v3d_freq gpu_freq force_turbo arm_boost; do
	say config $(vcgencmd get_config $k 2>/dev/null)
done
clk() { # one-line clock/thermal sample
	echo "arm=$(vcgencmd measure_clock arm 2>/dev/null | cut -d= -f2)" \
		"core=$(vcgencmd measure_clock core 2>/dev/null | cut -d= -f2)" \
		"v3d=$(vcgencmd measure_clock v3d 2>/dev/null | cut -d= -f2)" \
		"$(vcgencmd measure_temp 2>/dev/null)" \
		"$(vcgencmd get_throttled 2>/dev/null)"
}
say clk idle $(clk)
for p in /usr/lib/aarch64-linux-gnu/libgallium-*.so; do say mesa-lib $(basename "$p"); done
say pkg $(dpkg-query -W -f='${Package}=${Version} ' supertuxkart quakespasm glmark2-es2-drm kmscube vkmark mesa-libgallium libsdl2-2.0-0 2>/dev/null)

# eglinfo over GBM/surfaceless names the Mesa build and GL ES version the apps get.
timeout 30 eglinfo -B >"$RES/eglinfo.txt" 2>&1
grep -E "EGL (vendor|version) string|OpenGL ES profile (vendor|renderer|version)|OpenGL core profile version|OpenGL compatibility profile version" \
	"$RES/eglinfo.txt" | sort -u | head -12 | while read -r l; do say eglinfo $l; done

# ---------------------------------------------------------------- helpers
# gpu: "bin=<jobs>/<busy_ns> render=... tfu=... csd=... cache_clean=..." from gpu_stats
gpu() {
	[ -n "$V3DSTATS" ] || { echo "gpu_stats=NONE"; return; }
	awk 'NR > 1 { printf "%s%s=%s/%s", (NR > 2 ? " " : ""), $1, $3, $4 } END { printf "\n" }' "$V3DSTATS"
}

SAMPLER=""
start_sampler() { # start_sampler <bench>: clock + GPU-queue line every 5 s while it runs
	( while :; do say clk "$1" t=$(up) $(clk) $(gpu); sleep 5; done ) &
	SAMPLER=$!
}
stop_sampler() { [ -n "$SAMPLER" ] && kill "$SAMPLER" 2>/dev/null; wait "$SAMPLER" 2>/dev/null; SAMPLER=""; }

# Frame counter independent of each app: Mesa's HUD, invisible, printing
# "fps: N" to the app's stdout every second -- counted at SwapBuffers, the direct
# analogue of the Phoenix winsys `flipstat` counter.
HUD="GALLIUM_HUD=stdout,fps GALLIUM_HUD_VISIBLE=false GALLIUM_HUD_PERIOD=1"

# hud_summary <log> [<begin-regex> <end-regex>]: fps samples strictly between the
# first line matching begin and the first line matching end after it (the whole
# log without them). ANSI colour codes (STK colours its log even into a file)
# are stripped first, so a HUD line glued to a colour reset still counts.
hud_summary() {
	awk -v b="${2:-}" -v e="${3:-}" '
		BEGIN { on = (b == "") }
		{ gsub(/\033\[[0-9;]*m/, "") }
		!on && $0 ~ b { on = 1; next }
		on && e != "" && $0 ~ e { exit }
		on && /^fps: / { v[n++] = $2 + 0 }
		END {
			if (n == 0) { print "n=0"; exit }
			# E2 convention: drop the first 3 and the last sample (start/finish transients)
			lo = (n > 8) ? 3 : 0; hi = (n > 8) ? n - 1 : n
			s = 0; mn = 1e9; mx = 0
			for (i = lo; i < hi; i++) { s += v[i]; if (v[i] < mn) mn = v[i]; if (v[i] > mx) mx = v[i] }
			m = s / (hi - lo)
			# median
			k = 0; for (i = lo; i < hi; i++) w[k++] = v[i]
			for (i = 0; i < k; i++) for (j = i + 1; j < k; j++) if (w[j] < w[i]) { t = w[i]; w[i] = w[j]; w[j] = t }
			md = (k % 2) ? w[int(k / 2)] : (w[k / 2 - 1] + w[k / 2]) / 2
			printf "n=%d used=%d mean=%.2f median=%.2f min=%.2f max=%.2f\n", n, hi - lo, m, md, mn, mx
		}' "$1"
}

# hud_series <log> <tag>: every HUD sample, 30 per line, for host-side re-analysis
hud_series() {
	sed 's/\x1b\[[0-9;]*m//g' "$1" | grep -a '^fps: ' | awk '{printf "%s%s", (NR % 30 == 1 ? "" : " "), $2; if (NR % 30 == 0) printf "\n"} END {if (NR % 30) printf "\n"}' |
		while read -r l; do say "$2" $l; done
}

# ---------------------------------------------------------------- SuperTuxKart
# Settings = tools/supertuxkart-port/stk-launcher.c exactly: same base args, the
# same seeded players.xml/config.xml (show_fps + scale_rtts_factor 0.75, every
# other option at its 1.4 default: anisotropic 4, dynamic lights on, ...), the
# same data/ + stk-assets (copied byte-identical from the Phoenix export into
# /opt/e2c/stk), a fresh SAVEDIR per boot (Phoenix: /tmp/stk in RAM).
run_stk() { # run_stk <run#>
	r=$1; log="$RES/stk-$r.log"
	rm -rf /tmp/stk; mkdir -p /tmp/stk/config-0.10
	cat >/tmp/stk/config-0.10/players.xml <<'EOF'
<?xml version="1.0"?>
<players version="1" >
    <current player="Player"/>
    <player name="Player" guest="false" use-frequency="1" unique-id="1"/>
</players>
EOF
	cat >/tmp/stk/config-0.10/config.xml <<'EOF'
<?xml version="1.0"?>
<stkconfig version="8" >

    <!-- Status of internet: 0 user wasn't asked, 1: allowed, 2: not allowed -->
    <enable_internet value="2" />

    <Video show_fps="true" scale_rtts_factor="0.75" />

</stkconfig>
EOF
	say stk run=$r start t=$(up)
	start_sampler stk-$r
	# Snapshot the GPU queues when the track has loaded and when the profile
	# race ends -- the same log anchors hud_summary uses for the gameplay window.
	: >"$log"
	( m=0
	  while [ $m -lt 2 ]; do
		if [ $m = 0 ] && grep -aq "scene complexity estimated" "$log"; then
			echo "t=$(up) $(gpu)" >"$RES/stk-$r.gpu-start"; m=1
			drm_mode >"$RES/stk-$r.kms-mode"
		fi
		if [ $m = 1 ] && grep -aq "profile: Number of frames" "$log"; then
			echo "t=$(up) $(gpu)" >"$RES/stk-$r.gpu-end"; m=2
		fi
		sleep 0.5
	  done ) &
	watcher=$!
	t0=$(up)
	env $HUD SDL_VIDEODRIVER=kmsdrm HOME=/root \
		SUPERTUXKART_DATADIR=/opt/e2c/stk \
		SUPERTUXKART_ASSETS_DIR=/opt/e2c/stk/stk-assets \
		SUPERTUXKART_SAVEDIR=/tmp/stk \
		timeout 1200 /usr/games/supertuxkart \
			--screensize=1920x1080 --fullscreen \
			--disable-texture-compression \
			--disable-addon-karts --disable-addon-tracks \
			--track=hacienda --numkarts=4 --profile-laps=2 \
			>"$log" 2>&1
	rc=$?
	t1=$(up)
	stop_sampler
	kill $watcher 2>/dev/null; wait $watcher 2>/dev/null
	cp /tmp/stk/config-0.10/config.xml "$RES/stk-$r.config-after.xml" 2>/dev/null
	say stk run=$r rc=$rc wall_s=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.1f", b-a}')
	grep -a -E "Using renderer:|OpenGL version string|OpenGL renderer:|scene complexity|profile: Number of frames|profile: min |Unknown value for|scale_rtts|Desktop|screensize|resolution" "$log" \
		| sed 's/\x1b\[[0-9;]*m//g' | head -20 | while read -r l; do say stk run=$r log $l; done
	say stk run=$r kms-mode $(cat "$RES/stk-$r.kms-mode" 2>/dev/null)
	say stk run=$r hud-gameplay $(hud_summary "$log" "scene complexity estimated" "profile: Number of frames")
	say stk run=$r hud-all $(hud_summary "$log")
	hud_series "$log" "stk run=$r hud-series"
	# Per-frame GPU time over the race window: queue busy-time deltas divided by
	# the frames the HUD counted in that window (every sample, none dropped).
	if [ -f "$RES/stk-$r.gpu-start" ] && [ -f "$RES/stk-$r.gpu-end" ]; then
		frames=$(sed 's/\x1b\[[0-9;]*m//g' "$log" | awk '
			/scene complexity estimated/ { on = 1; next }
			on && /profile: Number of frames/ { exit }
			on && /^fps: / { f += $2 }
			END { printf "%.0f", f }')
		say stk run=$r gpu-race $(cat "$RES/stk-$r.gpu-start" "$RES/stk-$r.gpu-end" | tr '\n' ' ' | awk -v fr="$frames" '
			{
				for (i = 1; i <= NF; i++) {
					split($i, kv, "="); k = kv[1]; v = kv[2]
					if (k == "t") { if (t0 == "") t0 = v; else t1 = v; continue }
					split(v, jb, "/")
					if (!(k in j0)) { j0[k] = jb[1]; n0[k] = jb[2] } else { j1[k] = jb[1]; n1[k] = jb[2] }
				}
			}
			END {
				dt = t1 - t0; if (fr < 1) fr = 1
				printf "window_s=%.1f frames=%d fps=%.2f", dt, fr, fr / dt
				split("bin render tfu csd cache_clean", q, " ")
				for (i = 1; i <= 5; i++) { k = q[i]; if (!(k in j1)) continue
					printf " %s_ms_per_frame=%.2f %s_jobs_per_frame=%.2f %s_busy=%.1f%%", k, (n1[k] - n0[k]) / 1e6 / fr, k, (j1[k] - j0[k]) / fr, k, (n1[k] - n0[k]) / 1e9 / dt * 100 }
				printf "\n"
			}')
	else
		say stk run=$r gpu-race NONE "(race anchors not seen or no gpu_stats)"
	fi
}

# ---------------------------------------------------------------- quakespasm
# Same shareware pak0.pak and the same id1/config.cfg + autoexec.cfg as the
# Phoenix export (1920x1080 fullscreen, scr_showfps 1, scr_conscale 4).
# timedemo leaves the engine at the console, so wait for the result line and stop it.
run_quake() { # run_quake <run#>
	r=$1; log="$RES/quake-$r.log"
	say quake run=$r start t=$(up)
	start_sampler quake-$r
	env $HUD SDL_VIDEODRIVER=kmsdrm HOME=/root \
		timeout 300 /usr/games/quakespasm -basedir /opt/e2c/quake \
			-width 1920 -height 1080 -fullscreen \
			+vid_vsync 0 +timedemo demo1 >"$log" 2>&1 &
	qp=$!
	g0="t=$(up) $(gpu)"
	i=0
	while [ $i -lt 290 ] && kill -0 $qp 2>/dev/null; do
		grep -aqE "[0-9]+ frames +[0-9.]+ seconds +[0-9.]+ fps" "$log" && break
		sleep 1; i=$((i + 1))
	done
	g1="t=$(up) $(gpu)"
	sleep 1
	kill $qp 2>/dev/null; sleep 1; kill -9 $qp 2>/dev/null
	wait $qp 2>/dev/null
	stop_sampler
	res=$(grep -aoE "[0-9]+ frames +[0-9.]+ seconds +[0-9.]+ fps" "$log" | tail -1)
	say quake run=$r timedemo ${res:-NONE}
	# GPU queue busy time over the whole process (load + timedemo, so an upper
	# bound per timedemo frame), for comparison with the M1 qstat render busy.
	tdf=$(echo "$res" | awk '{print $1 + 0}')
	say quake run=$r gpu-process $(echo "$g0 $g1" | awk -v fr="$tdf" '
		{ for (i = 1; i <= NF; i++) { split($i, kv, "="); k = kv[1]; v = kv[2]
			if (k == "t") { if (t0 == "") t0 = v; else t1 = v; continue }
			split(v, jb, "/")
			if (!(k in n0)) { j0[k] = jb[1]; n0[k] = jb[2] } else { j1[k] = jb[1]; n1[k] = jb[2] } } }
		END { if (fr < 1) { print "no timedemo frames"; exit }
			printf "wall_s=%.1f timedemo_frames=%d", t1 - t0, fr
			split("bin render", q, " ")
			for (i = 1; i <= 2; i++) { k = q[i]; if (!(k in n1)) continue
				printf " %s_ms_total=%.0f %s_ms_per_td_frame=%.2f %s_jobs=%d", k, (n1[k] - n0[k]) / 1e6, k, (n1[k] - n0[k]) / 1e6 / fr, k, j1[k] - j0[k] }
			printf "\n" }')
	grep -a -E "^GL_VERSION|^GL_RENDERER|^Video mode|vid_vsync|^SDL|swap interval|^GL_VENDOR" "$log" | head -8 \
		| while read -r l; do say quake run=$r log $l; done
	say quake run=$r hud-all $(hud_summary "$log")
}

# ---------------------------------------------------------------- kmscube
run_kmscube() {
	log="$RES/kmscube.log"
	start_sampler kmscube
	t0=$(up)
	timeout 60 kmscube -D "$CARD" -c 600 >"$log" 2>&1
	rc=$?
	t1=$(up)
	stop_sampler
	say kmscube rc=$rc frames=600 wall_s=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}') \
		fps_wall=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.1f", 600/(b-a)}') "(includes init; vsync-locked)"
	grep -aiE "fps|mode|Using" "$log" | tail -5 | while read -r l; do say kmscube log $l; done
}

# ---------------------------------------------------------------- glmark2
# The research doc's reference (sec 2.3): glmark2-es2 off-screen 800x600, performance governor.
run_glmark2() {
	log="$RES/glmark2-offscreen.log"
	start_sampler glmark2
	timeout 900 glmark2-es2-drm --off-screen -s 800x600 \
		--winsys-options drm-device="$CARD" >"$log" 2>&1
	rc=$?
	stop_sampler
	say glmark2 offscreen rc=$rc $(grep -a "glmark2 Score" "$log" | tail -1)
	grep -a -E "GL_VERSION|GL_RENDERER|Surface Size" "$log" | head -4 | while read -r l; do say glmark2 log $l; done
	grep -a -E "^\[[a-z-]+\].*FPS:" "$log" | sed 's/ *FrameTime.*//' | while read -r l; do say glmark2 scene $l; done
}

# ---------------------------------------------------------------- vkmark
run_vkmark() {
	log="$RES/vkmark-headless.log"
	start_sampler vkmark
	timeout 600 vkmark --winsys headless >"$log" 2>&1
	rc=$?
	stop_sampler
	say vkmark headless rc=$rc $(grep -a "vkmark Score" "$log" | tail -1)
	grep -a -E "Vendor|Device Name|Driver Version" "$log" | head -4 | while read -r l; do say vkmark log $l; done
}

# ---------------------------------------------------------------- run
if [ -z "$CARD" ]; then
	say ERROR no vc4 KMS card -- GPU benchmarks skipped
else
	for b in $(echo "$ONLY" | tr ',' ' '); do
		case "$b" in
		stk)     r=1; while [ $r -le "$STKRUNS" ]; do run_stk $r; r=$((r + 1)); done ;;
		quake)   run_quake 1; run_quake 2 ;;
		kmscube) run_kmscube ;;
		glmark2) run_glmark2 ;;
		vkmark)  run_vkmark ;;
		*)       say WARN unknown bench "$b" ;;
		esac
	done
fi

say mesa-cache files_after=$(find "$MESA_SHADER_CACHE_DIR" -type f 2>/dev/null | wc -l)
say clk end $(clk)
dmesg >"$RES/dmesg.txt" 2>&1
grep -iE "v3d|vc4|drm|hang|timeout|reset" "$RES/dmesg.txt" | grep -iE "error|fail|hang|timeout|reset" | head -10 \
	| while read -r l; do say dmesg $l; done
sync
say DONE results=$RES t=$(up)
echo "===== E2C DONE ====="
sleep 1
sync
echo o >/proc/sysrq-trigger 2>/dev/null
while true; do sleep 3600; done
