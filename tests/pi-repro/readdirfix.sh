#!/bin/bash
# nfs-fs used to re-list a whole directory on EVERY readdir() call (667a41b).
# Two things to establish, in this order:
#
#   CORRECTNESS FIRST. The snapshot is now carried between calls and positioned
#   by a cumulative-name-length cookie, so a mis-step would silently skip or
#   duplicate an entry -- which in a game shows up as a missing texture, not as
#   an error. A wrong count here invalidates any timing below it.
#
#   THEN speed.
echo "rf: 1/6 COUNT stk-assets/textures  expect 649 (2 of them subdirs)"
ls /usr/share/supertuxkart/stk-assets/textures | wc -l
echo "rf: 2/6 COUNT /root/rdmix  expect 45 visible / 47 with dotfiles"
ls /root/rdmix | wc -l
ls -a /root/rdmix | wc -l
echo "rf: 3/6 the 45 names, sorted -- must be f1..f40 + sub1 sub2 + lnk1 lnk2 lnk3"
ls /root/rdmix | sort | tr '\n' ' '
echo
echo "rf: 4/6 no duplicates (uniq count must equal the count above)"
ls /root/rdmix | sort | uniq | wc -l
echo "rf: 5/6 COUNT /bin expect 165, /etc expect 22"
ls /bin | wc -l
ls /etc | wc -l
echo "rf: 6/6 SPEED: same flat dir + sample as before (readdir was 39 ms/file)"
/bin/fileperf /usr/share/supertuxkart/stk-assets/textures 150
echo "rf: and the end-to-end asset read (was 32 s, then 5 s)"
t0=$(date +%s)
tar cf /dev/null -C /usr/share/supertuxkart data/gui 2>/dev/null
t1=$(date +%s)
echo "rf: data/gui 243 files in $((t1 - t0)) s"
t2=$(date +%s)
tar cf /dev/null -C /usr/share supertuxkart 2>/dev/null
t3=$(date +%s)
echo "rf: READ-ALL 5127 files / 194 MB in $((t3 - t2)) s"
echo "rf: done"
