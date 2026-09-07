#!/bin/bash
# Owner bug #1, decisive form: drive ~40 XResizeWindow calls a second for 3 s
# (what a mouse drag actually does), then STOP and hold still for 12 s so an HDMI
# tick samples a QUIESCENT frame.  The earlier corrupted capture is best explained
# as a mid-repaint sample; grading the settled frame is the only way to tell that
# apart from real corruption.
echo "xdrag: starting wmaker"
/bin/wmaker &
/bin/sleep 55
echo "xdrag: drag test under WindowMaker"
/bin/xresizer 0 drag
echo "xdrag: done"
/bin/sleep 5
