#!/bin/bash
# Is STK's memory corruption CPU-side or in the GPU/Mesa path?
#
# STK crashes 5/5 at the kart-mesh stage with a garbage pointer, and the fault
# shape differs run to run (garbage code pointer vs corrupted malloc tree), so it
# is generic corruption. --no-graphics (main.cpp:2186) loads the same karts and
# materials without ever touching V3D, and the launcher appends user args after
# its own, so they win.
#
#   crashes headless => CPU-side: STK logic or the libphoenix allocator; V3D out
#   clean headless    => the GPU/Mesa path is implicated
#
# Needs no rebuild, which is why it comes before instrumenting anything.
echo "hl: stk --no-graphics"
stk --no-graphics
echo "hl: rc=$?"
echo "hl: done"
