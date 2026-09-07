#!/bin/bash
# Owner's discriminator: fetch ONE archive from NFS, extract to a ramdisk, then
# run STK against it.  Fast => the cost was many small NFS ops.  Still slow =>
# the bug is in asset loading, not NFS.
# Also re-measures raw throughput with dd bs=1M, because a `cat`-through-bash
# figure can be bottlenecked by bash's own read size rather than by NFS.
echo "stkzip: dd throughput test"
t0=$(date +%s)
dd if=/usr/share/quake2/baseq2/pak0.pak of=/dev/null bs=1M 2>&1 | tail -1
t1=$(date +%s)
echo "stkzip: DD pak0 (49951322 B) in $((t1 - t0)) s"

echo "stkzip: ramdisk space before"
df -h /ramtmp 2>/dev/null | tail -1

echo "stkzip: extracting archive from NFS to /ramtmp"
t2=$(date +%s)
mkdir -p /ramtmp/stk
tar xzf /srv-stk-assets.tar.gz -C /ramtmp/stk 2>/dev/null || tar xzf /stk-assets.tar.gz -C /ramtmp/stk
t3=$(date +%s)
echo "stkzip: EXTRACT (122424062 B archive -> 194 MB) in $((t3 - t2)) s"
df -h /ramtmp 2>/dev/null | tail -1
ls /ramtmp/stk/supertuxkart | head -3
echo "stkzip: done"
