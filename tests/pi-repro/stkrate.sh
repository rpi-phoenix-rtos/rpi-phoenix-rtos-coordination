#!/bin/bash
# Bounded, spawn-free per-file cost: data/gui is 243 files / 3 MB, so any time
# beyond ~0.2 s (3 MB at 25 MB/s) is pure per-file overhead.  Repeat on the
# ramdisk to separate NFS from the VFS/libc path.
echo "stkrate: NFS read of data/gui (243 files, 3 MB)"
t0=$(date +%s)
tar cf /dev/null -C /usr/share/supertuxkart data/gui 2>/dev/null
t1=$(date +%s)
echo "stkrate: NFS 243 files in $((t1 - t0)) s"

mkdir -p /ramtmp/g
tar cf - -C /usr/share/supertuxkart data/gui 2>/dev/null | tar xf - -C /ramtmp/g 2>/dev/null
echo "stkrate: RAMDISK read of the same 243 files"
t2=$(date +%s)
tar cf /dev/null -C /ramtmp/g data/gui 2>/dev/null
t3=$(date +%s)
echo "stkrate: RAMDISK 243 files in $((t3 - t2)) s"
echo "stkrate: done"
