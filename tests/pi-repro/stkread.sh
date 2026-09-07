#!/bin/bash
# Spawn-free measurements.  The earlier per-file numbers were invalid: `cat` and
# `ls -l` fork+exec once PER FILE, so they timed process spawn, not NFS.
# tar reads the whole tree in ONE process.
echo "stkread: reading ALL stk assets over NFS in one process"
t0=$(date +%s)
tar cf /dev/null -C /usr/share supertuxkart 2>/dev/null
t1=$(date +%s)
echo "stkread: READ-ALL 5127 files / 194 MB in $((t1 - t0)) s"

echo "stkread: extracting only data/ (1007 files, 46 MB) to ramdisk"
mkdir -p /ramtmp/d
t2=$(date +%s)
tar cf - -C /usr/share/supertuxkart data 2>/dev/null | tar xf - -C /ramtmp/d 2>/dev/null
t3=$(date +%s)
echo "stkread: NFS->RAMDISK 1007 files in $((t3 - t2)) s"

echo "stkread: re-reading those 1007 files from the ramdisk"
t4=$(date +%s)
tar cf /dev/null -C /ramtmp/d data 2>/dev/null
t5=$(date +%s)
echo "stkread: RAMDISK-READ 1007 files in $((t5 - t4)) s"
echo "stkread: done"
