#!/bin/bash
# Bug #1, third cut: hold the XOR rubber band on screen for 4 s per step so the
# periodic HDMI grab always samples the banded state, not just the repainted one.
echo "xrn: starting wmaker"
/bin/wmaker &
/bin/sleep 60
echo "xrn: XOR rubber band, long dwell"
/bin/xresizer 14 band
echo "xrn: done"
/bin/sleep 5
