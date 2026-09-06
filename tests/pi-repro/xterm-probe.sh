#!/bin/bash
# Why does `xterm -e ...` launched from a script never appear?  Run it in the
# foreground twice -- bare (no WM) and under WindowMaker -- so its stderr and
# exit status reach the UART.
echo "probe: xterm A (no wm)"
/bin/xterm -e /bin/sleep 30
echo "probe: A status=$?"
echo "probe: starting wmaker"
/bin/wmaker &
/bin/sleep 40
echo "probe: xterm B (under wm)"
/bin/xterm -e /bin/sleep 30
echo "probe: B status=$?"
/bin/sleep 5
echo "probe: done"
