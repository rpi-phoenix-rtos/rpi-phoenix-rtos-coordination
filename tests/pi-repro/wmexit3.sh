#!/bin/bash
# Reproduce "exit Window Maker -> Data Abort" without a mouse.
#
# NOTE the earlier version of this test was INVALID: it ran as a second psh
# command after `startx_gpu`, but xlaunch blocks in a waitpid loop
# (pl_phoenix_xlaunch.c:711) and never returned, so the script never executed and
# its clean result said nothing. Background X from inside one script instead.
#
# pkill -x matches the exact process NAME, not the command line: `pkill -f wmaker`
# would match this script's own argv and kill the harness (a footgun already hit
# in this project).
echo "wx: starting the GPU desktop in the background"
startx_gpu deskapps &
xl=$!
echo "wx: xlaunch pid=$xl; waiting for the desktop to settle"
sleep 75
# Show the raw table: the previous run's `ps | grep -c` printed nothing, so
# neither ps nor pkill can be assumed present. Signal the launcher by the PID we
# already hold instead of hunting for wmaker's -- SIGTERM to xlaunch runs the
# same client+server teardown the owner's "exit Window Maker" triggers.
echo "wx: --- ps table ---"
ps || echo "wx: ps unavailable"
echo "wx: --- end ps ---"
echo "wx: sending SIGTERM to xlaunch pid=$xl (desktop teardown)"
kill -TERM "$xl" || echo "wx: kill failed"
sleep 25
echo "wx: done"
