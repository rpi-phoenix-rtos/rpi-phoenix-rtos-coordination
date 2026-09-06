#!/bin/bash
# Reproduce the owner's "exit Window Maker -> Data Abort" without a mouse:
# run wmaker, let it settle, then ask it to quit the way its own menu does (SIGTERM).
echo "wmexit: starting wmaker"
/bin/wmaker &
wm=$!
/bin/sleep 45
echo "wmexit: sending SIGTERM to wmaker pid $wm"
/bin/kill -TERM "$wm"
/bin/sleep 8
echo "wmexit: wmaker signalled; script exiting"
