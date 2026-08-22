#!/bin/bash
#
# gpu-m2-2client.sh - concurrent-GPU #13 M2: prove the v3d-server daemon SERIALIZES
# two GPU clients running at the same time. This is the exact scenario that corrupted
# in M0 (two GPU processes concurrently = silent corruption + CSD TIMEOUT + 42x
# slowdown, because they clobbered the single global MMU_PT_PA_BASE + overlapping VA).
# Here BOTH clients route through the one daemon (sole GPU owner; one message at a
# time), so both must self-verify PASS with correct numerics.
#
# Two independent csd-matmul-daemon PROCESSES (each its own libv3d-client BO table)
# launched concurrently. Each does 100 GPU matmuls and checks GPU-vs-CPU bit-exactly.
# Client output is captured to per-client files (avoids interleaved UART garbage) and
# printed sequentially at the end; the server's per-dispatch lines interleave live.
#
# Stage at the NFS root (/srv/phoenix-rpi4-nfs) so they land at /:
#   rpi4-v3d, csd-matmul-daemon, gpu-m2-2client.sh
#
# Pi-bash-proven idioms only. Copyright 2026 Phoenix Systems  %LICENSE%

echo "m2: starting v3d-server (sole GPU owner) in background"
/rpi4-v3d &
srv_pid=$!

i=0
while [ ! -e /dev/v3d-srv ]; do
	i=$((i + 1))
	if [ "$i" -gt 20 ]; then
		echo "m2: /dev/v3d-srv not visible after ${i}s - launching clients anyway (they retry)"
		break
	fi
	sleep 1
done
if [ -e /dev/v3d-srv ]; then
	echo "m2: /dev/v3d-srv is up (server owns the GPU)"
fi

# Prefetch two copies into tmpfs (RAM) so both clients exec from RAM, not slow NFS
# demand-paging, and so their startup overlaps tightly (the concurrency we want to test).
cp /csd-matmul-daemon /tmp/cA
cp /csd-matmul-daemon /tmp/cB
chmod +x /tmp/cA /tmp/cB

echo "m2: launching 2 CONCURRENT clients (A + B) against the one daemon"
/tmp/cA > /tmp/outA 2>&1 &
pA=$!
/tmp/cB > /tmp/outB 2>&1 &
pB=$!
wait "$pA"
wait "$pB"

echo "===== M2 CLIENT A ====="
cat /tmp/outA
echo "===== M2 CLIENT B ====="
cat /tmp/outB

kill "$srv_pid" 2>/dev/null
echo "M2-2CLIENT-DONE"
