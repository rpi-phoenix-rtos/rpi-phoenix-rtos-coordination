#!/bin/bash
# Is STK's malloc-metadata crash caused by nfs-fs, or is it STK's own?
#
# STK honours SUPERTUXKART_DATADIR / SUPERTUXKART_ASSETS_DIR
# (stk-code src/io/file_manager.cpp:179,241), and bash can set them (psh cannot).
# So extract the ONE staged archive to the dummyfs ramdisk and point STK at it:
# every asset open then bypasses nfs-fs entirely. Only the ELF itself still comes
# from NFS.
#
#   still crashes  => nfs-fs is NOT the cause; the bug is STK's or the allocator's
#   loads clean    => the asset path is implicated and attribution flips
#
# This is also the owner's own suggested experiment (one archive -> ramdisk).
# NOTE: the ported `df` has no -h (it prints its help and the script loses the
# line), and gzip-decompressing 122 MB on the Pi consumed a whole capture window.
# Read an UNCOMPRESSED tar instead: 194 MB at ~25 MB/s is I/O, not CPU.
mkdir -p /ramtmp/stk
t0=$(date +%s)
tar xf /stk-assets.tar -C /ramtmp/stk 2>/dev/null
t1=$(date +%s)
echo "sr: extracted 194 MB / 5441 entries in $((t1 - t0)) s"
ls /ramtmp/stk/supertuxkart | tr '\n' ' '; echo
echo "sr: asset file count on ramdisk:"
ls /ramtmp/stk/supertuxkart/stk-assets | wc -l

export SUPERTUXKART_DATADIR=/ramtmp/stk/supertuxkart
export SUPERTUXKART_ASSETS_DIR=/ramtmp/stk/supertuxkart/stk-assets
echo "sr: DATADIR=$SUPERTUXKART_DATADIR"
echo "sr: ASSETS_DIR=$SUPERTUXKART_ASSETS_DIR"
echo "sr: running stk off the ramdisk"
stk
echo "sr: stk exited rc=$?"
echo "sr: done"
