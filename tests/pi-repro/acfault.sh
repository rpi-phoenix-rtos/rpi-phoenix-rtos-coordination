#!/bin/bash
# fileperf's flat-directory mode faulted (EL0 Data Abort in phMutexLock) on a
# command that ran clean before the nfs-fs attribute cache landed. Two things
# changed at once -- the nfs-fs code, and a relink of the tool against a freshly
# rebuilt sysroot -- so isolate them: the SAME mode over a dummyfs ramdisk uses
# no nfs-fs code at all. Crash there too => not the attribute cache.
echo "af: 1/5 flat mode, RAMDISK (no nfs-fs code on this path)"
mkdir -p /ramtmp/flat
cp /usr/share/supertuxkart/stk-assets/textures/*.png /ramtmp/flat 2>/dev/null
ls /ramtmp/flat | wc -l
/bin/fileperf /ramtmp/flat 20
echo "af: 2/5 flat mode, NFS, only 5 files"
/bin/fileperf /usr/share/supertuxkart/stk-assets/textures 5
echo "af: 3/5 flat mode, NFS, 20 files"
/bin/fileperf /usr/share/supertuxkart/stk-assets/textures 20
echo "af: 4/5 flat mode, NFS, 150 files (the command that faulted)"
/bin/fileperf /usr/share/supertuxkart/stk-assets/textures 150
echo "af: 5/5 same 150 again, is it deterministic"
/bin/fileperf /usr/share/supertuxkart/stk-assets/textures 150
echo "af: done"
