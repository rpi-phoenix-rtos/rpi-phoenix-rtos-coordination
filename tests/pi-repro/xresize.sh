#!/bin/bash
# Bug #4/#1 reproducer driver: bring up a WindowMaker session and drive an xterm
# through a series of window resizes with NO mouse, so the GPU X server's resize
# path can be observed on HDMI.  Run as the launcher's client:
#   startx_gpu /bin/Xphoenix-glamor-daemon <fontdirs> /bin/bash /root/xresize.sh
echo "xresize: starting wmaker"
/bin/wmaker &
/bin/sleep 50
echo "xresize: starting xterm"
/bin/xterm -xrm 'xterm*allowWindowOps: true' -geometry 60x20+140+140 \
	-e /bin/bash /root/xresize-inner.sh &
/bin/sleep 190
echo "xresize: done"
