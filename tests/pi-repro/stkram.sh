#!/bin/bash
# Is STK's malloc-metadata crash caused by nfs-fs, or is it STK's own?
#
# STK honours SUPERTUXKART_DATADIR / SUPERTUXKART_ASSETS_DIR
# (stk-code src/io/file_manager.cpp:179,241), and bash can set them (psh cannot).
# NOTE: use /tmp, NOT /ramtmp. After the NFS takeover nfs-fs splices the RAM-backed
# dummyfs onto /tmp (srv.c:662-670) and /ramtmp is a plain directory ON THE EXPORT,
# so an earlier version of this test wrote 194 MB back onto NFS and proved nothing.
# So extract the ONE staged archive to the tmpfs and point STK at it:
# every asset open then bypasses nfs-fs entirely. Only the ELF itself still comes
# from NFS.
#
#   still crashes  => nfs-fs is NOT the cause; the bug is STK's or the allocator's
#   loads clean    => the asset path is implicated and attribution flips
#
# This is also the owner's own suggested experiment (one archive -> RAM), which had
# never actually run: both earlier attempts wrote to /ramtmp, i.e. back onto NFS.
# Transfer with cp -r, NOT tar: the host tar emits GNU LongLink entries (typeflag
# 0x4c) for names over 100 chars, which the Pi's tar rejects -- it aborted at
# 162 of 194 MB, STK saw an incomplete tree and silently fell back to NFS.
# /tmp is the real tmpfs: df reports 262144 1K-blocks = 256 MiB
# (board_config.h:110), so the full 194 MB fits with room to spare.
echo "sr: tmpfs before"
df /tmp
mkdir -p /tmp/assets/supertuxkart
t0=$(date +%s)
cp -r /usr/share/supertuxkart/data /tmp/assets/supertuxkart/
echo "sr: cp data rc=$?"
cp -r /usr/share/supertuxkart/stk-assets /tmp/assets/supertuxkart/
echo "sr: cp stk-assets rc=$?"
t1=$(date +%s)
echo "sr: copied in $((t1 - t0)) s"
echo "sr: tmpfs after"
df /tmp
echo "sr: sanity -- the file STK looks for must exist on tmpfs:"
ls -l /tmp/assets/supertuxkart/data/stk_config.xml
echo "sr: stk-assets top-level entries:"
ls /tmp/assets/supertuxkart/stk-assets | wc -l

export SUPERTUXKART_DATADIR=/tmp/assets/supertuxkart
export SUPERTUXKART_ASSETS_DIR=/tmp/assets/supertuxkart/stk-assets
echo "sr: DATADIR=$SUPERTUXKART_DATADIR"
echo "sr: ASSETS_DIR=$SUPERTUXKART_ASSETS_DIR"
echo "sr: running stk off the tmpfs"
stk
echo "sr: stk exited rc=$?"
echo "sr: done"
