#!/bin/bash
# stk-assets/textures is FLAT with 647 regular files -- a real sample, and the
# tool is non-recursive so it must be pointed at a flat directory.
echo "fp: NFS (stk-assets/textures, flat, 647 files)"
/bin/fileperf /usr/share/supertuxkart/stk-assets/textures 150
echo "fp: copy to ramdisk"
mkdir -p /ramtmp/t
tar cf - -C /usr/share/supertuxkart/stk-assets textures 2>/dev/null | tar xf - -C /ramtmp/t 2>/dev/null
echo "fp: RAMDISK (same files)"
/bin/fileperf /ramtmp/t/textures 150
echo "fp: RAMDISK second pass (warm)"
/bin/fileperf /ramtmp/t/textures 150
echo "fp: done"
