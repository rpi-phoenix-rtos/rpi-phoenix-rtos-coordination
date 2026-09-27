#!/bin/bash
#
# xorg-drm-m4a.sh -- run ON THE Pi at the psh prompt:  /bin/bash /bin/xorg-drm-m4a.sh
#
# First Pi cycle of the new-lane X server (docs/gpu-new-lane/M4-xorg-modesetting.md,
# pre-registered cycle m4a-xorg-drm). psh has no '&' and no ';', so this script does the
# job control: it starts Xorg-drm :1 in the background, waits for its socket, runs one
# X client from the old lane's export (xclock, seconds hand = one repaint per second),
# holds, then ends the client -- Xorg-drm runs with -terminate, so it exits when its last
# client goes, which also exercises the clean teardown (rpi4-kms restores the console).
#
# Preconditions (earlier psh commands of the same cycle): the render server first, then
#   /bin/rpi4-v3d-async-m3p2 -r 1 -m serial -i
#   /bin/rpi4-kms-m3p2 -G -C
#
# Environment knobs: XSRV (server binary), CONF, HOLD (seconds with the client up),
# CLIENT (the X client command line), XVERB (-verbose level for the UART),
# DRMPHX_TRACE (libdrm-phoenix ioctl trace in the server, default 1: rate-limited to the
# first 16 calls of each request number, then 1 in 256 -- "DRMPHX ..." lines).
#
# Every line of ours starts with "XORGDRM " (grading).
#
# Copyright 2026 Phoenix Systems
#
# This file is part of Phoenix-RTOS.
#
# %LICENSE%

XSRV=${XSRV:-/bin/Xorg-drm}
CONF=${CONF:-/etc/X11/xorg-drm.conf}
HOLD=${HOLD:-30}
XVERB=${XVERB:-3}
CLIENT=${CLIENT:-/bin/xclock -geometry 480x480+720+300 -update 1}

# What pl_phoenix_xlaunch gives the old lane's clients (psh exports neither HOME nor PATH).
export HOME=/root
export PATH=/bin
export XFILESEARCHPATH=/usr/share/X11/app-defaults/%N
export XLOCALEDIR=/usr/share/X11/locale

rm -f /tmp/.X1-lock /tmp/.X11-unix/X1
echo "XORGDRM start server=${XSRV} conf=${CONF} hold=${HOLD} trace=${DRMPHX_TRACE:-1}"

DRMPHX_TRACE=${DRMPHX_TRACE:-1} "${XSRV}" :1 -config "${CONF}" -logfile /var/log/Xorg-drm.1.log -verbose "${XVERB}" \
	-nolisten tcp -ac -terminate &
xpid=$!
echo "XORGDRM server pid=${xpid}"

# The listening socket exists before the screens are initialised (dix creates it first);
# a client that connects early simply waits for the first Dispatch.
i=0
while [ ! -e /tmp/.X11-unix/X1 ] && [ "${i}" -lt 90 ]; do
	sleep 1
	i=$((i + 1))
done
if [ -e /tmp/.X11-unix/X1 ]; then
	echo "XORGDRM socket=up wait_s=${i} t=${SECONDS}"
else
	echo "XORGDRM socket=missing wait_s=${i} t=${SECONDS}"
fi

echo "XORGDRM client start: DISPLAY=:1 ${CLIENT}"
DISPLAY=:1 ${CLIENT} &
cpid=$!
# Heartbeat every 10 s: the client itself prints nothing, and psh-interact ends a command
# after --idle-secs of UART silence.
held=0
while [ "${held}" -lt "${HOLD}" ]; do
	sleep 10
	held=$((held + 10))
	echo "XORGDRM hold t=${SECONDS} held=${held}s"
done
echo "XORGDRM hold done t=${SECONDS}; ending the client (pid=${cpid})"
kill "${cpid}" 2>/dev/null
wait "${cpid}" 2>/dev/null
echo "XORGDRM client exited rc=$? t=${SECONDS}"

# -terminate: the server exits by itself after its last client; give it 15 s, then TERM.
i=0
while [ -e /tmp/.X11-unix/X1 ] && [ "${i}" -lt 15 ]; do
	sleep 1
	i=$((i + 1))
done
if [ -e /tmp/.X11-unix/X1 ]; then
	echo "XORGDRM server still up after ${i}s: sending TERM"
	kill "${xpid}" 2>/dev/null
	sleep 5
fi
wait "${xpid}" 2>/dev/null
echo "XORGDRM server exited rc=$? socket=$([ -e /tmp/.X11-unix/X1 ] && echo left || echo gone) t=${SECONDS}"
echo "XORGDRM done"
