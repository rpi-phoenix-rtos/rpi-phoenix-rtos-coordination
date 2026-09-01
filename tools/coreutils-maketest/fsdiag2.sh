#!/bin/bash
# Isolate the `rm -rf <dir> -> Directory not empty` bug: is single-file unlink
# broken, or only rm's recursive traversal (openat+fdopendir+unlinkat)?
O=/root/ct/fsdiag2.txt
export PATH=/usr/bin:/bin
{
echo "===== 1. single-file rm (no -r) on tmpfs ====="
cd /tmp && rm -rf d && mkdir d && cd d && printf x > f1
echo "  before: $(ls)"
rm f1; echo "  rm f1 rc=$? after: [$(ls)]"

echo "===== 2. coreutils 'unlink' builtin path ====="
printf x > f2; unlink f2; echo "  unlink f2 rc=$? after: [$(ls)]"

echo "===== 3. rmdir empty dir ====="
cd /tmp/d && mkdir sub && rmdir sub; echo "  rmdir sub rc=$? after: [$(ls)]"

echo "===== 4. rm -r (NO -f) so per-file errno prints ====="
cd /tmp && rm -rf e && mkdir e && cd e
printf a > g1; printf b > g2; mkdir g3 && printf c > g3/h1
cd /tmp
rm -r e 2>&1; echo "  rm -r e rc=$? still=[$([ -d e ] && echo YES)]"
echo "  leftover tree: $(ls -R e 2>&1)"

echo "===== 5. rm -r a dir with ONE file ====="
cd /tmp && rm -rf s && mkdir s && printf z > s/only
rm -r s 2>&1; echo "  rm -r s rc=$? still=[$([ -d s ] && echo YES)]"

echo "===== 6. does 'find e -delete' / manual loop work? ====="
cd /tmp && rm -rf m && mkdir m && printf 1 > m/a && printf 2 > m/b
for f in m/a m/b; do rm "$f"; echo "  rm $f rc=$?"; done
rmdir m; echo "  rmdir m rc=$? still=[$([ -d m ] && echo YES)]"
echo "DONE"
} > "$O" 2>&1
