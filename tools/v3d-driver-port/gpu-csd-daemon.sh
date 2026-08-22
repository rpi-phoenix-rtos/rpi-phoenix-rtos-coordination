#!/bin/bash
#
# gpu-csd-daemon.sh - run ON THE Pi (via `bash /gpu-csd-daemon.sh` at the psh
# prompt) to prove the M1 CSD path end-to-end: start the v3d-server (sole GPU
# owner), let it register /dev/v3d-srv, then run the CSD matmul microbench as a
# CLIENT of the daemon (linked against libv3d-client, not the in-process winsys).
# Patterned on the M0 2-proc repro + the redis-persist Pi scripts (psh mis-parses
# inline `bash -c '...'` single quotes, so this ships as a script file).
#
# Uses only Pi-bash-proven idioms (#!/bin/bash, `&`, integer `sleep`, `$((...))`,
# `[ -e ]`). Cleanup (`kill`) is best-effort; correctness does NOT depend on it -
# libv3d-client retries the /dev/v3d-srv lookup for ~5 s on its own.
#
# Stage these three at the NFS root (/srv/phoenix-rpi4-nfs) so they land at /:
#   rpi4-v3d, csd-matmul-daemon, gpu-csd-daemon.sh
#
# Copyright 2026 Phoenix Systems  %LICENSE%

echo "gpu-csd-daemon: starting v3d-server (GPU owner) in background"
/rpi4-v3d &
srv_pid=$!

# Informative wait for the node (non-fatal: the client retries lookup internally).
i=0
while [ ! -e /dev/v3d-srv ]; do
	i=$((i + 1))
	if [ "$i" -gt 20 ]; then
		echo "gpu-csd-daemon: /dev/v3d-srv not visible after ${i}s - running client anyway (it retries)"
		break
	fi
	sleep 1
done
if [ -e /dev/v3d-srv ]; then
	echo "gpu-csd-daemon: /dev/v3d-srv is up (server owns the GPU)"
fi

echo "gpu-csd-daemon: running csd-matmul-daemon (client of /dev/v3d-srv)"
/csd-matmul-daemon
echo "gpu-csd-daemon: client done"

# Best-effort cleanup (ignored if kill/\$! unsupported).
kill "$srv_pid" 2>/dev/null
echo "GPU-CSD-DAEMON-DONE"
