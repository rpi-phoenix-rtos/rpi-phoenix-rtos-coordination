#!/bin/bash
# Runs inside the xterm.  Fills the terminal with text, then resizes the window
# via XTWINOPS (CSI 8 ; rows ; cols t), which makes xterm call XResizeWindow --
# the same path a mouse-driven WindowMaker resize takes.
/bin/ls /bin
/bin/sleep 25
echo "RESIZE-1 30x100"
printf '\033[8;30;100t'
/bin/sleep 20
/bin/ls /bin
echo "RESIZE-2 45x140"
printf '\033[8;45;140t'
/bin/sleep 20
/bin/ls /bin
echo "RESIZE-3 12x40"
printf '\033[8;12;40t'
/bin/sleep 20
echo "RESIZE-4 40x110"
printf '\033[8;40;110t'
/bin/ls /bin
/bin/sleep 45
