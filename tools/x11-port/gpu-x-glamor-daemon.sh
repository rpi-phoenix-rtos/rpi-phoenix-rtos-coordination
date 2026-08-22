#!/bin/bash
#
# gpu-x-glamor-daemon.sh - run ON THE Pi (via `bash /gpu-x-glamor-daemon.sh` at the
# psh prompt) to prove M3a: the glamor GPU-accelerated X server as a CLIENT of the
# v3d-server daemon. Starts /rpi4-v3d (sole GPU owner), waits for /dev/v3d-srv, then
# launches Xphoenix-glamor-daemon (glamor X linked against libv3d-client instead of the
# in-process winsys) + xeyes via the SAME pl_phoenix_xlaunch flow the in-process E5
# showcase uses (server :0 -ac, socket wait, client with DISPLAY=:0).
#
# X's GPU work (glamor root-pixmap + xeyes rendering, glReadPixels present -> /dev/fb0)
# routes through the daemon; the daemon prints "CL submit #N done" lines proving the
# render CLs went through it.
#
# Big binaries are prefetched to tmpfs (/tmp): the X server is ~27 MB and NFS
# demand-paging is slow; copying to tmpfs first avoids a multi-second first-paint stall.
#
# Stage at the NFS root (/srv/phoenix-rpi4-nfs):
#   /rpi4-v3d                    (the GPU daemon; from the gl/csd build)
#   /bin/Xphoenix-glamor-daemon  (the daemon-client glamor X server)
# Already staged by the E5 showcase (reused as-is):
#   /bin/pl_phoenix_xlaunch, /bin/xeyes, /usr/share/fonts/X11/misc, /dev/fb0
#
# Copyright 2026 Phoenix Systems  %LICENSE%

echo "gpu-x-glamor-daemon: starting v3d-server (GPU owner) in background"
/rpi4-v3d &
srv_pid=$!

i=0
while [ ! -e /dev/v3d-srv ]; do
	i=$((i + 1))
	if [ "$i" -gt 20 ]; then
		echo "gpu-x-glamor-daemon: /dev/v3d-srv not visible after ${i}s - launching X anyway (client retries)"
		break
	fi
	sleep 1
done
if [ -e /dev/v3d-srv ]; then
	echo "gpu-x-glamor-daemon: /dev/v3d-srv is up (server owns the GPU)"
fi

echo "gpu-x-glamor-daemon: prefetching X server + xeyes + launcher to tmpfs"
cp /bin/Xphoenix-glamor-daemon /tmp/Xphoenix-glamor-daemon
cp /bin/xeyes                  /tmp/xeyes
cp /bin/pl_phoenix_xlaunch     /tmp/pl_phoenix_xlaunch

echo "gpu-x-glamor-daemon: launching glamor X (daemon client) + xeyes (DISPLAY=:0)"
# pl_phoenix_xlaunch <server> <fontdir> <client> : forks the server as :0 -ac, waits
# for /tmp/.X11-unix/X0, then forks the client with DISPLAY=:0 (same as the E5 flow).
/tmp/pl_phoenix_xlaunch /tmp/Xphoenix-glamor-daemon /usr/share/fonts/X11/misc /tmp/xeyes
echo "gpu-x-glamor-daemon: xlaunch returned"

kill "$srv_pid" 2>/dev/null
echo "GPU-X-GLAMOR-DAEMON-DONE"
