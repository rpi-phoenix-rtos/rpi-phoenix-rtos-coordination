#!/bin/bash
# Does the filesystem hand back the RIGHT BYTES and the RIGHT SIZE?
#
# STK died inside the allocator (malloc_chunkSize, malloc_dl.c:78) on a garbage
# chunk header, and the garbage looked like file content -- the signature of a
# buffer sized from one value and then filled with more. The attribute cache
# could in principle serve a stale st_size and produce exactly that, so this
# checks both at once: cksum prints a CRC *and* a byte count, over 107 files
# spread across path depths 2..8. Every line must match the host reference.
echo "cc: begin (107 files, depths 2-8)"
cd /usr/share/supertuxkart || exit 1
while read -r f; do
	echo "$(cksum "$f" | awk '{print $1"_"$2}') $f"
done < /root/sample.lst
echo "cc: done"
