#!/usr/bin/env bash
#
# e2c-cycle.sh — one Raspberry Pi OS reference boot for experiment E2c
# (docs/gpu-new-lane/E2c-pios-baseline.md): netboot the Pi OS rootfs staged at
# artifacts/linux-netboot/, let /usr/local/bin/e2c-bench.sh run the benchmarks
# (SuperTuxKart, quakespasm timedemo, kmscube, glmark2, vkmark) and power off,
# capture the UART, then put the Phoenix netboot server back.
#
# HOW THE LANE IS SWITCHED (nothing permanent is edited):
#   * TFTP: dnsmasq is restarted by the worker scripts/netboot-server.sh with
#     RPI4B_NETBOOT_TFTPROOT pointing at a per-run copy of the Linux boot files
#     (artifacts/linux-netboot/tftp-e2c/, regenerated every run from tftp/ with
#     this run's cmdline.txt + config.txt). The staged tftp/ is never modified,
#     so the sdflash lane keeps working.
#     NOT netboot-server-up.sh: that wrapper first syncs the Phoenix NFS export.
#   * NFS: the rootfs is already exported persistently (/etc/exports, NFSv3,
#     10.42.0.0/24); this script only asserts that nfs-server is active and the
#     export is live. The Phoenix export (/srv/phoenix-rpi4-nfs-gcc16) is untouched.
#   * RESTORE, on every exit path (trap): Pi powered off, then the same worker
#     with the Phoenix bootfs as tftp-root (not the wrapper, for the same reason),
#     then two checks — dnsmasq.conf's tftp-root is the Phoenix bootfs again, and
#     check-netboot-blob.sh --expect nfsroot.
#
# Results: the UART log under artifacts/rpi4b-uart/ (tagged `E2C ...` lines) and
# the full per-benchmark logs, which the Pi writes to the NFS root and this script
# copies to artifacts/e2c/<label>/.
#
# ⚠ A full run is 20-40 min — longer than the 10 min cap on a single Bash tool
# call. Run it detached so a tool timeout cannot kill it half-way:
#     setsid nohup ./scripts/e2c-cycle.sh --arm stock > artifacts/e2c/stock.out 2>&1 &
# and watch for `E2C DONE` / `=== e2c-cycle done` in that file. (A SIGKILL skips
# the restore trap; if that ever happens run ./scripts/netboot-server-up.sh — the
# usual Phoenix server-up, which also syncs the export as every Phoenix cycle does.)
#
# Usage: ./scripts/e2c-cycle.sh [options]
#   --arm stock|phxclk250|phxclk500   clock arm (default stock):
#        stock      the Pi OS config.txt as shipped (arm_boost, DVFS, core 500)
#        phxclk250  + force_turbo=1 arm_freq=1500 core_freq=250 v3d_freq=500
#                   = the clocks of the Phoenix E2/E2b trials (pctr-clk line)
#        phxclk500  + force_turbo=1 arm_freq=1500 core_freq=500 v3d_freq=500
#                   = Phoenix after the core-500 adoption
#   --only LIST        comma list for e2c.only (default stk,quake,kmscube,glmark2,vkmark)
#   --stk-runs N       STK runs in the boot (default 2; grade the warm one)
#   --capture-secs N   UART capture cap (default 2700); the cycle ends early,
#                      ~10 s after `E2C DONE` appears
#   --label TEXT       log label (default e2c-<arm>-<HHMMSS>)
#   --prepare-only     build tftp-e2c/ and install e2c-bench.sh, then exit
#                      (no server change, no Pi power) — for review
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
if [ -f "$repo/.env.local" ]; then
	set -a
	# shellcheck disable=SC1091
	. "$repo/.env.local"
	set +a
fi

arm="stock"
only="stk,quake,kmscube,glmark2,vkmark"
stk_runs=2
capture_secs=2700
label=""
prepare_only=0
while [ "$#" -gt 0 ]; do
	case "$1" in
	--arm) arm="$2"; shift 2 ;;
	--only) only="$2"; shift 2 ;;
	--stk-runs) stk_runs="$2"; shift 2 ;;
	--capture-secs) capture_secs="$2"; shift 2 ;;
	--label) label="$2"; shift 2 ;;
	--prepare-only) prepare_only=1; shift ;;
	-h|--help) awk 'NR>=2 && NR<=51' "${BASH_SOURCE[0]}"; exit 0 ;;
	*) echo "e2c-cycle: unknown argument $1" >&2; exit 2 ;;
	esac
done
case "$arm" in stock|phxclk250|phxclk500) ;; *) echo "e2c-cycle: bad --arm $arm" >&2; exit 2 ;; esac
[ -n "$label" ] || label="e2c-${arm}-$(date +%H%M%S)"

lnx="$repo/artifacts/linux-netboot"
rootfs="$lnx/rootfs"
tftp_src="$lnx/tftp"
tftp_run="$lnx/tftp-e2c"
bench_src="$repo/tools/gpu-lane/e2c/e2c-bench.sh"
bench_dst="$rootfs/usr/local/bin/e2c-bench.sh"
phx_tftp="${PHOENIX_BUILDROOT:-$repo/.buildroot}/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs"
state_dir="${RPI4B_NETBOOT_STATE_DIR:-$repo/artifacts/netboot}"
serial_dev="${RPI4B_SERIAL_DEV:-/dev/ttyUSB0}"
uart_dir="$repo/artifacts/rpi4b-uart"
out_dir="$repo/artifacts/e2c/$label"

die() { echo "e2c-cycle: $*" >&2; exit 1; }

# ------------------------------------------------------------------ preflight
[ -f "$tftp_src/kernel8.img" ] && [ -f "$tftp_src/start4.elf" ] || die "no Linux boot files in $tftp_src"
[ -x "$rootfs/usr/games/supertuxkart" ] || die "rootfs lacks supertuxkart (see E2c doc: install step)"
[ -d "$rootfs/opt/e2c/stk/stk-assets" ] && [ -f "$rootfs/opt/e2c/quake/id1/pak0.pak" ] \
	|| die "rootfs lacks /opt/e2c game data (see E2c doc: install step)"
[ -f "$bench_src" ] || die "missing $bench_src"
sudo -n true 2>/dev/null || die "needs passwordless sudo (install e2c-bench.sh, dnsmasq)"

# The kernel in tftp/ must match the modules in the rootfs, or vc4/v3d never load.
kver_mods=$(ls "$rootfs/lib/modules" | grep -- '-rpi-v8$' | head -1)
[ -n "$kver_mods" ] || die "no -rpi-v8 modules in rootfs"

# e2c-bench.sh: the coord-repo copy is the source of truth.
if ! cmp -s "$bench_src" "$bench_dst"; then
	sudo -n install -m 755 "$bench_src" "$bench_dst" || die "cannot install $bench_dst"
	echo "installed $bench_dst"
fi

# Per-run TFTP tree: a copy of tftp/ with this run's cmdline.txt and config.txt.
rm -rf "$tftp_run"
cp -r --no-preserve=ownership,mode "$tftp_src" "$tftp_run" || die "cannot copy $tftp_src"
chmod -R u+rwX,go+rX "$tftp_run"
base_cmdline=$(tr -d '\n' < "$tftp_src/cmdline.txt")
case "$base_cmdline" in *init=*) die "tftp/cmdline.txt already carries init= — expected the plain one" ;; esac
# console=serial0 stays the LAST console= (it becomes /dev/console, the UART we capture).
# video= pins the KMS mode to 1080p60 (the HDMI grabber also offers 2160p).
# (Both HDMI ports: whichever the grabber is on.) e2c.label ties the Pi's results
# directory to this run.
printf '%s init=/usr/local/bin/e2c-bench.sh video=HDMI-A-1:1920x1080@60 video=HDMI-A-2:1920x1080@60 loglevel=4 e2c.arm=%s e2c.only=%s e2c.stkruns=%s e2c.label=%s\n' \
	"$base_cmdline" "$arm" "$only" "$stk_runs" "$label" > "$tftp_run/cmdline.txt"
{
	printf '\n# --- e2c-cycle.sh arm=%s ---\n[all]\n' "$arm"
	case "$arm" in
	stock) printf '# stock Pi OS clocks (arm_boost=1 above, firmware/kernel DVFS)\n' ;;
	phxclk250) printf 'force_turbo=1\narm_freq=1500\ncore_freq=250\nv3d_freq=500\n' ;;
	phxclk500) printf 'force_turbo=1\narm_freq=1500\ncore_freq=500\nv3d_freq=500\n' ;;
	esac
} >> "$tftp_run/config.txt"
echo "tftp-e2c: $tftp_run"
echo "cmdline:  $(cat "$tftp_run/cmdline.txt")"
echo "config.txt tail:"; tail -n 7 "$tftp_run/config.txt" | sed 's/^/  /'

if [ "$prepare_only" = 1 ]; then
	echo "--prepare-only: nothing switched, Pi untouched"
	exit 0
fi

systemctl is-active --quiet nfs-server || die "nfs-server is not active"
sudo -n exportfs -v 2>/dev/null | grep -q "^$rootfs" || die "rootfs is not exported (expected in /etc/exports: $rootfs)"

# ------------------------------------------------------------------ restore (trap)
cycle_pid=""
restored=0
restore_phoenix() {
	local rc=$?
	[ "$restored" = 1 ] && return
	restored=1
	echo ""
	echo "=== e2c-cycle: restoring the Phoenix netboot server ==="
	"$repo/scripts/pi_power_off.sh" >/dev/null 2>&1 || echo "!! pi_power_off.sh failed" >&2
	if [ -n "$cycle_pid" ] && kill -0 "$cycle_pid" 2>/dev/null; then
		fuser -k -TERM "$serial_dev" >/dev/null 2>&1 || true
		kill "$cycle_pid" 2>/dev/null || true
		sleep 2
		kill -9 "$cycle_pid" 2>/dev/null || true
	fi
	local ok=1
	# The worker again, not netboot-server-up.sh: the wrapper would also run
	# sync-netboot-tree.sh on the Phoenix export (and can clear its shader cache).
	# This regenerates exactly the dnsmasq.conf a Phoenix server-up writes.
	RPI4B_NETBOOT_TFTPROOT="$phx_tftp" RPI4B_NETBOOT_STATE_DIR="$state_dir" \
		"$repo/scripts/netboot-server.sh" up >/dev/null 2>&1 \
		|| { ok=0; echo "!! netboot-server.sh up (Phoenix tree) FAILED" >&2; }
	local served
	served=$(sed -n 's/^tftp-root=//p' "$state_dir/dnsmasq.conf" 2>/dev/null)
	if [ "$served" = "$phx_tftp" ]; then
		echo "dnsmasq tftp-root: $served (Phoenix)"
	else
		ok=0; echo "!! dnsmasq tftp-root is '$served', expected '$phx_tftp'" >&2
	fi
	"$repo/scripts/netboot-server.sh" status 2>/dev/null | head -1
	"$repo/scripts/check-netboot-blob.sh" --expect nfsroot || ok=0
	if [ "$ok" = 1 ]; then
		echo "=== Phoenix netboot restored ==="
	else
		echo "!!! Phoenix netboot NOT verified — run ./scripts/netboot-server-up.sh and check-netboot-blob.sh by hand" >&2
		[ "$rc" = 0 ] && rc=4
	fi
	exit "$rc"
}
trap '' HUP
trap restore_phoenix EXIT INT TERM

# ------------------------------------------------------------------ switch to Linux
echo "=== switching TFTP to the Pi OS lane ==="
RPI4B_NETBOOT_TFTPROOT="$tftp_run" RPI4B_NETBOOT_STATE_DIR="$state_dir" \
	"$repo/scripts/netboot-server.sh" up || die "dnsmasq did not come up on the Linux lane"
served=$(sed -n 's/^tftp-root=//p' "$state_dir/dnsmasq.conf")
[ "$served" = "$tftp_run" ] || die "dnsmasq serves '$served', not $tftp_run"

# ------------------------------------------------------------------ cycle
mkdir -p "$out_dir"
echo "=== cycle: label=$label capture cap=${capture_secs}s ==="
# --skip-server-up: the Linux lane is already up. --skip-bridge-recovery: on a DHCP
# timeout its recovery would run netboot-server-restart.sh, i.e. restart dnsmasq
# on the PHOENIX tree mid-cycle.
"$repo/scripts/test-cycle-netboot.sh" --skip-server-up --skip-bridge-recovery \
	--capture-secs "$capture_secs" --label "$label" &
cycle_pid=$!

log=""
t0=$(date +%s)
done_seen=0
while kill -0 "$cycle_pid" 2>/dev/null; do
	if [ -z "$log" ]; then
		log=$(ls -t "$uart_dir"/rpi4b-uart-*-netboot-"$label".log 2>/dev/null | head -1)
	fi
	# Either marker: the UART corrupts ~1 % of lines, and a lost banner would cost the whole cap.
	if [ -n "$log" ] && [ "$done_seen" = 0 ] && grep -aqE "===== E2C DONE =====|E2C DONE results=" "$log"; then
		done_seen=1
		echo "E2C DONE seen after $(( $(date +%s) - t0 )) s; ending the capture"
		sleep 10
		fuser -k -TERM "$serial_dev" >/dev/null 2>&1 || true
	fi
	sleep 5
done
wait "$cycle_pid"
cycle_rc=$?
cycle_pid=""

# ------------------------------------------------------------------ results
echo ""
echo "=== E2C lines from the UART log ==="
if [ -n "$log" ] && [ -f "$log" ]; then
	echo "log: $log"
	tr -d '\r' < "$log" | grep -a '^E2C ' | grep -av ' clk ' || echo "(no E2C lines — check the log)"
	cp "$log" "$out_dir/"
else
	echo "no UART log found for label $label"
fi
# The Pi wrote the full logs to the NFS root; `latest` points at this boot's directory.
res_dir=$(sed -n 's/.*E2C DONE results=\([^ ]*\).*/\1/p' "${log:-/dev/null}" 2>/dev/null | tr -d '\r' | tail -1)
# The UART corrupts a small share of lines; fall back to the Pi's own `latest` link,
# but only if that directory's summary carries THIS run's label (if the boot never
# reached the script, `latest` is a previous run).
if [ -z "$res_dir" ] || [ ! -d "$rootfs$res_dir" ]; then
	latest=$(readlink "$rootfs/var/log/e2c/latest" 2>/dev/null)
	if [ -n "$latest" ] && grep -q "label=$label " "$rootfs/var/log/e2c/$latest/summary.txt" 2>/dev/null; then
		res_dir="/var/log/e2c/$latest"
		echo "(results dir from the Pi's latest link: $res_dir)"
	fi
fi
if [ -n "$res_dir" ] && [ -d "$rootfs$res_dir" ]; then
	cp -r "$rootfs$res_dir" "$out_dir/" && echo "full logs: $out_dir/$(basename "$res_dir")"
else
	echo "NFS results dir not reported (boot did not finish?) — look in $rootfs/var/log/e2c/"
fi
[ "$done_seen" = 1 ] || echo "!! E2C DONE never appeared: the boot hung or the cap ($capture_secs s) was too short"
echo "=== e2c-cycle done (cycle rc=$cycle_rc) ==="
exit "$cycle_rc"
