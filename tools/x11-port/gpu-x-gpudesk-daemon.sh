#!/bin/bash
#
# gpu-x-gpudesk-daemon.sh - concurrent-GPU #13 M3c (THE FULL X11-DE PAYOFF): a live
# GPU-rendered window running CONCURRENTLY inside the 2D X desktop, all through the
# v3d-server daemon. Starts the daemon (sole GPU owner), then the glamor
# GPU-accelerated X server AS A DAEMON CLIENT, running the `gpudesk` session:
#   twm (WM) + gl-x11-window-daemon (a V3D GPU window, ALSO a daemon client) + xclock + xcalc.
#
# BOTH the X server's own 2D rendering AND the GPU window's 3D rendering route their
# V3D work through the daemon, which serializes them (M3b proved 2 concurrent daemon
# clients coexist bit-exact). The GPU window XPutImages its readback into its X
# window, so X composites it — no /dev/fb0 contention. This is the E5 goal: an
# accelerated desktop hosting a live GPU app at the same time.
#
# Stage at the NFS root (/srv/phoenix-rpi4-nfs):
#   /rpi4-v3d, /pl_phoenix_xlaunch-deskapps (has the gpudesk mode; root-staged so the
#   per-cycle /bin resync can't revert it), gpu-x-gpudesk-daemon.sh
#   /bin/Xphoenix-glamor-daemon, /bin/gl-x11-window-daemon (NEW names -> persist)
# Reused from the image: /bin/{twm,xclock,xcalc}, /usr/share/fonts/X11/misc, /dev/fb0
#
# Pi-bash-proven idioms only. Copyright 2026 Phoenix Systems  %LICENSE%

echo "gpu-x-gpudesk-daemon: starting v3d-server (sole GPU owner) in background"
/rpi4-v3d &
srv_pid=$!

i=0
while [ ! -e /dev/v3d-srv ]; do
	i=$((i + 1))
	if [ "$i" -gt 20 ]; then
		echo "gpu-x-gpudesk-daemon: /dev/v3d-srv not visible after ${i}s - launching anyway (clients retry)"
		break
	fi
	sleep 1
done
[ -e /dev/v3d-srv ] && echo "gpu-x-gpudesk-daemon: /dev/v3d-srv is up (server owns the GPU)"

echo "gpu-x-gpudesk-daemon: prefetching glamor X server + GPU window to tmpfs"
cp /bin/Xphoenix-glamor-daemon /tmp/Xphoenix-glamor-daemon
chmod +x /tmp/Xphoenix-glamor-daemon

echo "gpu-x-gpudesk-daemon: launching glamor-X (daemon client) + twm + GPU window (daemon client) + xclock + xcalc"
/pl_phoenix_xlaunch-deskapps --server /tmp/Xphoenix-glamor-daemon gpudesk
echo "gpu-x-gpudesk-daemon: xlaunch returned"

kill "$srv_pid" 2>/dev/null
echo "GPU-X-GPUDESK-DAEMON-DONE"
