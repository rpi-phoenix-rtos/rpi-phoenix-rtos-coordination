#!/bin/bash
# Why did `tar xf /stk-assets.tar -C /ramtmp/...` stop after ONE entry?
# The board raises DUMMYFS_SIZE_MAX to 256 MiB (board_config.h:110), so 194 MB
# should fit. The earlier runs hid the answer behind 2>/dev/null; show it.
echo "dg: df /ramtmp (no -h; the ported df has no such flag)"
df /ramtmp
echo "dg: is the archive visible and complete?"
ls -l /stk-assets.tar
echo "dg: extracting WITH stderr shown"
mkdir -p /ramtmp/x
tar xf /stk-assets.tar -C /ramtmp/x
echo "dg: tar rc=$?"
echo "dg: entries extracted under stk-assets:"
ls /ramtmp/x/supertuxkart/stk-assets 2>/dev/null | wc -l
echo "dg: entries under data:"
ls /ramtmp/x/supertuxkart/data 2>/dev/null | wc -l
echo "dg: df after"
df /ramtmp
echo "dg: done"
