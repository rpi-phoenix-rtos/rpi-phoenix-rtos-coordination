#!/bin/bash
# Bug #1 bisect: which interactive-resize op corrupts the screen -- the XOR
# rubber band on the root window, or the in-window XCopyArea?  Same probe, two
# passes, one op each, under WindowMaker.
echo "xrb: starting wmaker"
/bin/wmaker &
/bin/sleep 60
echo "xrb: pass 1 -- XOR rubber band ONLY"
/bin/xresizer 8 band
echo "xrb: pass 1 done"
/bin/sleep 6
echo "xrb: pass 2 -- XCopyArea ONLY"
/bin/xresizer 8 copy
echo "xrb: pass 2 done"
/bin/sleep 5
