#!/bin/bash
# Verdict run for the nfs-fs attribute cache (phoenix-rtos-filesystems fe3979d).
#
# Directly comparable to the BEFORE table in docs/inprogress/WEEK-2026-W37.md
# section 4c: same flat 647-file directory, same 150-file sample, same tool.
# BEFORE: readdir 39 ms, stat 47 ms, open 49 ms, read 3.3 ms, close 1.2 ms per file.
echo "ac: 1/4 NFS flat dir, 150 files (compare to 4c BEFORE table)"
/bin/fileperf /usr/share/supertuxkart/stk-assets/textures 150

# The cache only removes NETWORK round trips. Whatever remains at each added
# component is the client-side cost (libphoenix's two messages per prefix plus
# kernel resolution) -- the #3 item, which no nfs-fs change can touch. A ramdisk
# has no network at all, so this is that cost on its own.
echo "ac: 2/4 per-component cost with NO network (dummyfs ramdisk)"
/bin/fileperf --mkdepth /ramtmp/dp

echo "ac: 3/4 per-component cost over NFS (same tool, real nested dirs)"
/bin/fileperf --mkdepth /root/dp

# Single paths at known depths. `first` is the cold cost, `warm_avg` the cost
# once the prefix is cached -- the STK case is the warm one (thousands of opens
# under one prefix), so both numbers matter and neither alone is the answer.
echo "ac: 4/4 stat at fixed depths"
/bin/fileperf --stat /usr /usr/share /usr/share/supertuxkart \
	/usr/share/supertuxkart/stk-assets \
	/usr/share/supertuxkart/stk-assets/textures \
	/usr/share/supertuxkart/data/stk_config.xml
echo "ac: done"
