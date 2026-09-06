#!/bin/bash
# Minimal, X-free reproducer for the EL1 _pmap_switch Data Abort seen when a
# window-manager session is torn down (bug #4).  If this faults, the bug is a
# general process-teardown use-after-free and has nothing to do with X11.
echo "sigterm-child: spawning"
/bin/sleep 60 &
child=$!
/bin/sleep 5
echo "sigterm-child: SIGTERM to $child"
/bin/kill -TERM "$child"
/bin/sleep 5
echo "sigterm-child: done"
