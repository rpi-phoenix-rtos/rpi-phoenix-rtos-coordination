#!/bin/bash
# Owner bug #1 (GPU X11 resize artefacts), mouse-free.  Phase A runs the
# self-resizing probe with NO window manager, so anything wrong is the server's
# own resize path; phase B repeats it under WindowMaker, which is how the owner
# hit it.
echo "xr2: phase A -- xresizer, no WM"
/bin/xresizer 12
echo "xr2: phase A done"
echo "xr2: starting wmaker"
/bin/wmaker &
/bin/sleep 55
echo "xr2: phase B -- xresizer under WindowMaker"
/bin/xresizer 12
echo "xr2: phase B done"
/bin/sleep 5
