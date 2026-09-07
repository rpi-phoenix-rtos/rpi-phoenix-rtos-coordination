#!/bin/bash
# Ramdisk leg only, small sample so it fits the window: is the ~45 ms
# path-resolution cost filesystem-independent?
mkdir -p /ramtmp/t
echo "fp2: copying 60 files to the ramdisk"
tar cf - -C /usr/share/supertuxkart/stk-assets/textures . 2>/dev/null | tar xf - -C /ramtmp/t 2>/dev/null &
sleep 45
echo "fp2: RAMDISK pass 1"
/bin/fileperf /ramtmp/t 60
echo "fp2: RAMDISK pass 2 (warm)"
/bin/fileperf /ramtmp/t 60
echo "fp2: DEVFS (no filesystem at all)"
/bin/fileperf /dev 60
echo "fp2: done"
