#!/bin/bash
#
# gpu-x-desktop-daemon.sh - X11-DE milestone: a GPU-accelerated multi-window X
# DESKTOP running through the concurrent-GPU v3d-server daemon (#13). Starts the
# daemon (sole GPU owner), then brings up the glamor GPU-accelerated X server AS A
# DAEMON CLIENT with a window manager + several 2D apps (twm + xterm + xclock +
# xcalc + xeyes) via pl_phoenix_xlaunch's `deskapps` mode.
#
# This converts the proven #13 daemon + E5 glamor-X into a real accelerated desktop:
# X's own 2D rendering runs on the V3D GPU, routed through the daemon (so a future
# concurrent GPU app is possible). No second in-process GPU client here (that is the
# M3c display-compositing follow-on) — deskapps is pure 2D so it can't fight the
# server for the single V3D.
#
# Stage at the NFS root (/srv/phoenix-rpi4-nfs):
#   /rpi4-v3d, /bin/Xphoenix-glamor-daemon, /bin/pl_phoenix_xlaunch,
#   /bin/{twm,xterm,xclock,xcalc,xeyes}, /usr/share/fonts/X11/misc, gpu-x-desktop-daemon.sh
#
# Pi-bash-proven idioms only. Copyright 2026 Phoenix Systems  %LICENSE%

echo "gpu-x-desktop-daemon: starting v3d-server (sole GPU owner) in background"
/rpi4-v3d &
srv_pid=$!

i=0
while [ ! -e /dev/v3d-srv ]; do
	i=$((i + 1))
	if [ "$i" -gt 20 ]; then
		echo "gpu-x-desktop-daemon: /dev/v3d-srv not visible after ${i}s - launching X anyway (client retries)"
		break
	fi
	sleep 1
done
[ -e /dev/v3d-srv ] && echo "gpu-x-desktop-daemon: /dev/v3d-srv is up (server owns the GPU)"

# Prefetch the ~27 MB glamor X server to tmpfs (NFS demand-paging is slow); the WM +
# apps page from /bin on demand (smaller). The launcher forks the server from the
# --server path and the clients from /bin.
echo "gpu-x-desktop-daemon: prefetching glamor X server to tmpfs"
cp /bin/Xphoenix-glamor-daemon /tmp/Xphoenix-glamor-daemon
chmod +x /tmp/Xphoenix-glamor-daemon

echo "gpu-x-desktop-daemon: launching glamor-X (daemon client) + twm + xterm + xclock + xcalc + xeyes"
# Use the root-staged launcher (/pl_phoenix_xlaunch-deskapps): the export's /bin is
# repopulated from the built image each cycle, which reverts a hand-staged
# /bin/pl_phoenix_xlaunch to the older in-image build (no `deskapps` mode); a new-named
# file at the NFS root persists (like /rpi4-v3d).
/pl_phoenix_xlaunch-deskapps --server /tmp/Xphoenix-glamor-daemon deskapps
echo "gpu-x-desktop-daemon: xlaunch returned"

kill "$srv_pid" 2>/dev/null
echo "GPU-X-DESKTOP-DAEMON-DONE"
