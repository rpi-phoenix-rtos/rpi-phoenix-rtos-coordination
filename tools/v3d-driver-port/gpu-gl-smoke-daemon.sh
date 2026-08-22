#!/bin/bash
#
# gpu-gl-smoke-daemon.sh - run ON THE Pi (via `bash /gpu-gl-smoke-daemon.sh` at the
# psh prompt) to prove the CL (render) path through the v3d-server daemon: start the
# server (sole GPU owner), let it register /dev/v3d-srv, then run the GL render-clear
# test as a CLIENT of the daemon (gl-smoke-daemon = gl_frontend_smoke.c + the Mesa GL
# stack, linked against libv3d-client instead of the in-process winsys).
#
# The GL test glClears an FBO-backed RT to green and reads back the center pixel; the
# daemon prints "CL submit #N done" for each render CL it executes. Correct result:
#   client:  gl: GLCLEAR readback center=0xff00ff00
#   server:  rpi4-v3d: CL submit #1 done ...
#
# Uses only Pi-bash-proven idioms; correctness does not depend on the kill (the client
# retries the /dev/v3d-srv lookup internally).
#
# Stage these at the NFS root (/srv/phoenix-rpi4-nfs) so they land at /:
#   rpi4-v3d, gl-smoke-daemon, gpu-gl-smoke-daemon.sh
#
# Copyright 2026 Phoenix Systems  %LICENSE%

echo "gpu-gl-smoke-daemon: starting v3d-server (GPU owner) in background"
/rpi4-v3d &
srv_pid=$!

i=0
while [ ! -e /dev/v3d-srv ]; do
	i=$((i + 1))
	if [ "$i" -gt 20 ]; then
		echo "gpu-gl-smoke-daemon: /dev/v3d-srv not visible after ${i}s - running client anyway (it retries)"
		break
	fi
	sleep 1
done
if [ -e /dev/v3d-srv ]; then
	echo "gpu-gl-smoke-daemon: /dev/v3d-srv is up (server owns the GPU)"
fi

echo "gpu-gl-smoke-daemon: running gl-smoke-daemon (GL render-clear client of /dev/v3d-srv)"
# Prefetch the ~20 MB client into tmpfs (/tmp is RAM) so execution is not bottlenecked
# on random NFS demand-paging (sequential cp + read-ahead is far faster than page faults).
echo "gpu-gl-smoke-daemon: prefetching client to /tmp (ramdisk)"
cp /gl-smoke-daemon /tmp/gl-smoke-daemon
chmod +x /tmp/gl-smoke-daemon
/tmp/gl-smoke-daemon
echo "gpu-gl-smoke-daemon: client done"

kill "$srv_pid" 2>/dev/null
echo "GPU-GL-SMOKE-DAEMON-DONE"
