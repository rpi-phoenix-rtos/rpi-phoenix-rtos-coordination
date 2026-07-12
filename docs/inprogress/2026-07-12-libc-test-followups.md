# libc test-suite follow-ups (from the 2026-07-12 HW run)

Discovered while running the full libc Unity suite on the real Pi 4 (SD boot).
This session's own work (memmem, getsubopt, string2mode/fopen, nl_langinfo) all
passed on HW — these are pre-existing / environment items, set aside for later.

## 1. `stat_mode` permission-bit discrepancy (real, pre-existing)

`test-libc-misc` → group `stat_mode`: 4 failures, all a default-permission mismatch —
created files come out `rw-rw-rw-` (0666) where POSIX expects `rw-r--r--` (0644):

```
libc/misc/stat.c:206  stat_mode/permissions_all  Expected 36845 (040755-ish) Was 36863
libc/misc/stat.c:236  stat_mode/reg_type         Expected 33188 (0100644)    Was 33206 (0100666)
libc/misc/stat.c:330  stat_mode/symlink_type     Expected 33188              Was 33206
libc/misc/stat.c:385  stat_mode/symlink_lstat    Expected 33188              Was 33206
```

Hypothesis: the umask is not being applied on create (open/creat `O_CREAT` mode &
`~umask`), or the ext2 fs / posixsrv create path ignores the requested mode and
stores 0666. To investigate: check libphoenix `open`/`creat`/`umask` handling and
the bcm2711-emmc/ext2 create path; confirm whether `umask(022)` then
`creat(...,0666)` yields 0644. Minor, but a genuine POSIX-conformance gap.

## 2. Run the network/socket libc tests under nfsroot/netboot

`test-libc-inet-socket`, `test-libc-poll`, and the tail of `test-libc-unix-socket`
(blocks after 5 tests) need the lwip network stack, which the **SD variant does not
start** (local ext2 root, no networking). Re-run them under `--variant nfsroot` (or
`netboot`), card removed, network up — via the `rpi4-run` skill recipe B. Expected
to be an environment fix, not a code defect.

## 3. Run the non-libc system test suites on HW

The card also carries ~20 system/lib test binaries not yet run:
`test-libalgo test-libcache test-libtinyaes test-libtrace test-libuuid test-setjmp
test-sys-cond test-sys-mutex test-sys-perf test-thread-local test-waitpid
test-ioctl test-ioctl-nested test-exec test-echo test-helloworld test-initfini
test-mprotect`. Run them (skill recipe A) for a broader libphoenix/kernel sanity
check. (Skip the `test-fail-*` / `test-*-fault` binaries — those are trunner
self-tests designed to crash.)
