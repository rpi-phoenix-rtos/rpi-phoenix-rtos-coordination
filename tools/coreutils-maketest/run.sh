#!/bin/bash
# coreutils 9.5 test-suite driver for Phoenix-RTOS (RPi4). Runs individual gnulib
# tests/*.sh directly (NOT the automake harness, which needs make+perl). Launched
# from psh as a single command: `bash /root/ct/run.sh` (psh has no redirection).
#
# Results are written to FILES on the NFS export so the host can read them in full
# regardless of UART capture truncation:
#   /root/ct/out.txt        one `CTEST <name> PASS|FAIL|SKIP|ERROR` line + CTSUMMARY
#   /root/ct/logs/<t>.log   full stdout+stderr of each test (for diagnosing FAILs)
# gnulib exit codes: 0=pass, 77=skip (require_ gate), 99=hard/framework error.
set -u
CU=/root/ct/cu
OUT=/root/ct/out.txt
LOGD=/root/ct/logs
[ -d "$CU/tests" ] || { echo "CTFATAL no $CU/tests"; exit 1; }
rm -rf "$LOGD"; mkdir -p "$LOGD"
: > "$OUT"

# Run tests with CWD on tmpfs (/tmp) + ABSOLUTE srcdir: each gnulib test makes its
# scratch dir "gt-<name>.XXXX" in CWD and churns files; keeping that off NFS avoids
# NFS-client consistency noise. init.sh is still found via the absolute srcdir.
export PATH=/usr/bin:/bin
export srcdir="$CU"
export VERBOSE=no
# require_built_ gates each test on this list; without it every test skips.
export built_programs="[ b2sum base32 base64 basename basenc cat chcon chgrp chmod chown chroot cksum cmp comm cp csplit cut date dd df dir dircolors dirname du echo env expand expr factor false fmt fold getlimits groups head hostid id join kill link ln logname ls md5sum mkdir mkfifo mknod mktemp mv nice nl nohup nproc numfmt od paste pathchk pr printenv printf ptx pwd readlink realpath rm rmdir runcon seq sha1sum sha224sum sha256sum sha384sum sha512sum shred shuf sleep sort split stat stdbuf stty sum sync tac tail tee test timeout touch tr true truncate tsort tty uname unexpand uniq unlink uptime users vdir wc who whoami yes"

echo "BASHOK $(seq -s, 1 5) nl=$(nl --version 2>&1 | head -1)" >> "$OUT"

TESTS="
tests/misc/sleep.sh
tests/misc/false-status.sh
tests/misc/printenv.sh
tests/misc/echo.sh
tests/misc/nl.sh
tests/misc/sync.sh
tests/misc/pathchk.sh
tests/misc/realpath.sh
tests/seq/seq-precision.sh
tests/seq/seq-extra-number.sh
tests/head/head-c.sh
tests/tr/tr-case-class.sh
tests/cksum/cksum.sh
tests/cksum/sum-sysv.sh
tests/cksum/md5sum-bsd.sh
tests/split/lines.sh
tests/od/od-endian.sh
"

pass=0; fail=0; skip=0; err=0; nofile=0
for t in $TESTS; do
  base=$(echo "$t" | tr '/' '_')
  if [ ! -f "$CU/$t" ]; then echo "CTEST $t NOFILE" >> "$OUT"; nofile=$((nofile+1)); continue; fi
  # `9>&2` is REQUIRED: coreutils' init.cfg sets stderr_fileno_=9 (paired with the
  # automake harness's `exec 9>&2`). Without fd 9 open, init.sh's warn_ hits
  # "Bad file descriptor" and tests skip/fail spuriously. Match the harness.
  ( cd /tmp && srcdir="$CU" bash "$CU/$t" 9>&2 ) > "$LOGD/$base.log" 2>&1
  rc=$?
  case $rc in
    0)  r=PASS;       pass=$((pass+1));;
    77) r=SKIP;       skip=$((skip+1));;
    99) r=ERROR;      err=$((err+1));;
    *)  r="FAIL rc=$rc"; fail=$((fail+1));;
  esac
  echo "CTEST $t $r" >> "$OUT"
done
echo "CTSUMMARY pass=$pass fail=$fail skip=$skip err=$err nofile=$nofile total=$((pass+fail+skip+err+nofile))" >> "$OUT"
echo "CTDONE" >> "$OUT"
