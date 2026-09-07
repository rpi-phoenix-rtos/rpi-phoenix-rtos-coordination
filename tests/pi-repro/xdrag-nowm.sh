#!/bin/bash
# Same drag test with NO window manager.  If the geometry mismatch disappears,
# WindowMaker is dropping the final resize of a burst; if it persists, the X
# server is.
echo "xdragnowm: drag test with NO WM"
/bin/xresizer 0 drag
echo "xdragnowm: done"
/bin/sleep 5
