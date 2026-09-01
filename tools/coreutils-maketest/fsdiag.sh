#!/bin/bash
# Root-cause the coreutils-test "file vanishes after create / rm: Directory not
# empty" symptom. Captures to a file on NFS so the host reads it in full.
O=/root/ct/fsdiag.txt
export PATH=/usr/bin:/bin
{
echo "===== df (space) ====="
df -k /tmp / 2>&1
echo "===== /tmp tmpfs: create 5 small files ====="
cd /tmp && rm -rf d && mkdir d && cd d
for i in 1 2 3 4 5; do
  printf 'content-%s\n' "$i" > "f$i"
  echo "  wrote f$i rc=$? bytes=$(wc -c < f$i 2>&1)"
done
echo "  --- ls -la ---"; ls -la 2>&1
echo "  --- stat each name returned by ls (readdir vs lookup) ---"
for n in $(ls 2>&1); do printf '  stat %s: ' "$n"; stat -c '%s' "$n" 2>&1; done
echo "  --- chmod -R u+rwx . ---"; chmod -R u+rwx . 2>&1; echo "  chmod rc=$?"
echo "  --- rm -rf the dir ---"; cd /tmp && rm -rf d 2>&1; echo "  rm rc=$? still_exists=$([ -d d ] && echo YES || echo no)"
echo "===== one bigger write to probe ENOSPC ====="
cd /tmp && { seq 1 100000 > big.txt; echo "  seq>big rc=$? bytes=$(wc -c < big.txt 2>&1)"; }
rm -f big.txt
echo "===== NFS /root/ct: same create+stat+rm ====="
cd /root/ct && rm -rf dd && mkdir dd && cd dd
printf 'hi\n' > f; echo "  wrote f rc=$? bytes=$(wc -c < f 2>&1)"
ls -la 2>&1
printf '  stat f: '; stat -c '%s' f 2>&1
cd /root/ct && rm -rf dd 2>&1; echo "  nfs rm rc=$? still_exists=$([ -d dd ] && echo YES || echo no)"
echo "DONE"
} > "$O" 2>&1
