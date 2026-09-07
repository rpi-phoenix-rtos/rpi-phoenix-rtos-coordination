#!/bin/bash
# Is STK's slow asset load a per-FILE cost or a throughput cost?  Measure both
# on the Pi: N small asset files vs one large sequential read.  A per-file cost
# in the tens of ms with healthy throughput means a per-operation stall bug, not
# "NFS is slow".
echo "stkperf: begin"
t0=$(date +%s)
n=0
for f in /usr/share/supertuxkart/stk-assets/karts/*/*; do
	cat "$f" > /dev/null 2>&1
	n=$((n + 1))
	if [ "$n" -ge 200 ]; then break; fi
done
t1=$(date +%s)
echo "stkperf: SMALL $n files in $((t1 - t0)) s"

t2=$(date +%s)
cat /usr/share/quake2/baseq2/pak0.pak > /dev/null 2>&1
t3=$(date +%s)
echo "stkperf: BIG pak0.pak in $((t3 - t2)) s"

# stat-only cost, to separate open/read from metadata
t4=$(date +%s)
m=0
for f in /usr/share/supertuxkart/stk-assets/karts/*/*; do
	ls -l "$f" > /dev/null 2>&1
	m=$((m + 1))
	if [ "$m" -ge 200 ]; then break; fi
done
t5=$(date +%s)
echo "stkperf: STAT $m files in $((t5 - t4)) s"
echo "stkperf: done"
