#!/bin/bash
# Narrow the STK corruption one step further, still with no rebuild.
#
# Headless (--no-graphics) is CLEAN but skips the SP mesh + shader-compile + V3D
# render path entirely (0 SPMeshBuffer lines, 0 shader compiles vs 5 and 34 in a
# graphical run). --disable-dynamic-lights (main.cpp:923) keeps rendering but
# drops the ADVANCED pipeline, so it separates SP from basic V3D rendering.
#
#   clean          => the SP/advanced pipeline is the culprit -- and STK may be
#                     demo-usable on the fixed pipeline
#   still crashes  => basic V3D rendering is involved too
echo "fx: stk --disable-dynamic-lights"
stk --disable-dynamic-lights
echo "fx: rc=$?"
echo "fx: done"
