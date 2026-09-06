#!/bin/bash
# Owner bug #1, focused: WindowMaker FIRST (fully settled), then the resize
# probe.  The previous run showed the probe painting every step while nothing
# appeared on screen, so this run reports the window's root-relative placement
# and its parent to tell "the WM put it somewhere invisible" apart from "the
# server never painted it".
echo "xr3: starting wmaker"
/bin/wmaker &
/bin/sleep 60
echo "xr3: wmaker settled -- xresizer under WindowMaker"
/bin/xresizer 12
echo "xr3: done"
/bin/sleep 5
