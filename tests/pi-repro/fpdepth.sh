#!/bin/bash
# Decisive test of the per-component model: stat the SAME file through paths that
# differ ONLY in component count ("./" padding).  Same inode, same directory.
# Linear growth => path resolution does a round-trip per component.
echo "fpd: ramdisk target"
mkdir -p /ramtmp/x
cp /usr/share/supertuxkart/data/gui/icons/logo.png /ramtmp/x/f 2>/dev/null || echo hello > /ramtmp/x/f
/bin/fileperf --depth /ramtmp/x/f
echo "fpd: devfs target"
/bin/fileperf --depth /dev/zero
echo "fpd: nfs target"
/bin/fileperf --depth /usr/share/supertuxkart/data/stk_config.xml
echo "fpd: done"
