#!/bin/bash
#
# gpu-m3b-concurrent.sh - concurrent-GPU #13 M3b (THE PAYOFF): two REAL GPU apps
# running concurrently through the one v3d-server daemon = the E5 single-GPU-process
# limit LIFTED. A GPU-accelerated X server (glamor, presents to /dev/fb0) and a GPU
# compute client (csd-matmul, self-verifying) are launched AT THE SAME TIME, so X's
# startup render-CL burst interleaves with csd's 100 CSD dispatches at the daemon.
#
# This is the M0 corruption scenario (two GPU processes at once), now mediated by the
# daemon's one-message-at-a-time serialization. PASS = csd self-verifies bit-exact
# (max_rel_err=0, no CSD TIMEOUT) AND X comes up with xeyes rendered to HDMI — i.e.
# both real GPU clients coexisted without the M0 corruption, and X stays visible.
#
# Stage at the NFS root (/srv/phoenix-rpi4-nfs):
#   /rpi4-v3d, /bin/Xphoenix-glamor-daemon, /csd-matmul-daemon, gpu-m3b-concurrent.sh
# Reused from E5: /bin/pl_phoenix_xlaunch, /bin/xeyes, /usr/share/fonts/X11/misc, /dev/fb0
#
# Pi-bash-proven idioms only. Copyright 2026 Phoenix Systems  %LICENSE%

echo "m3b: starting v3d-server (sole GPU owner) in background"
/rpi4-v3d &
srv_pid=$!

i=0
while [ ! -e /dev/v3d-srv ]; do
	i=$((i + 1))
	if [ "$i" -gt 20 ]; then
		echo "m3b: /dev/v3d-srv not visible after ${i}s - launching anyway (clients retry)"
		break
	fi
	sleep 1
done
[ -e /dev/v3d-srv ] && echo "m3b: /dev/v3d-srv is up (server owns the GPU)"

# Prefetch the big binaries to tmpfs so both clients start from RAM and overlap tightly.
echo "m3b: prefetching X server + xeyes + launcher + csd client to tmpfs"
cp /bin/Xphoenix-glamor-daemon /tmp/Xphoenix-glamor-daemon
cp /bin/xeyes                  /tmp/xeyes
cp /bin/pl_phoenix_xlaunch     /tmp/pl_phoenix_xlaunch
cp /csd-matmul-daemon          /tmp/csd-matmul-daemon
chmod +x /tmp/Xphoenix-glamor-daemon /tmp/xeyes /tmp/pl_phoenix_xlaunch /tmp/csd-matmul-daemon

echo "m3b: launching glamor X (daemon client) AND csd-matmul (daemon client) CONCURRENTLY"
# X in background: its startup render-CL burst (root pixmap + xeyes first paint) runs now.
/tmp/pl_phoenix_xlaunch /tmp/Xphoenix-glamor-daemon /usr/share/fonts/X11/misc /tmp/xeyes &
xlaunch_pid=$!
# csd immediately, concurrently: 100 CSD dispatches interleave with X's CLs at the daemon.
/tmp/csd-matmul-daemon > /tmp/csdout 2>&1 &
csd_pid=$!

wait "$csd_pid"
echo "===== M3b CONCURRENT CSD CLIENT (ran alongside glamor-X) ====="
cat /tmp/csdout

# Let X finish coming up + xeyes paint so the HDMI grab shows it still visible.
sleep 8

kill "$xlaunch_pid" 2>/dev/null
kill "$srv_pid" 2>/dev/null
echo "M3B-CONCURRENT-DONE"
