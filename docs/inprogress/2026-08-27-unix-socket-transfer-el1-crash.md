# EL1 kernel crash in test-libc-unix-socket `transfer` (found 2026-08-27)

**Status: OPEN — real EL1 (kernel-mode) crash, attribution UNCONFIRMED (merge regression vs pre-existing race). Needs a dedicated dig.**

Found by a broad post-merge libc validation sweep (label `merge-sweep2`, netboot; the
netboot export-drift fix means the binaries are fresh vs the merged kernel).

## Repro
`/bin/test-libc-unix-socket` over netboot. **10 tests PASS** (zero_len_send/recv, close,
msg_data_only, stream/dgram_sock_data_and_fd, stream/dgram_sock_fd_flags,
stream/dgram_sock_msg_fork), then the **`transfer`** case (runner line 1806) crashes the
kernel with a repeated `Exception #37: Data Abort (EL1)`.

## The crashing test
`TEST(test_unix_socket, transfer)` → `unix_transfer(SOCK_STREAM/DGRAM)` in a loop
(unix-socket.c:751): creates an `AF_UNIX` `socketpair(type | SOCK_NONBLOCK)`, **forks**,
parent `send()`s random-length chunks of a global `data[]` buffer while the child
`recv()`s into `buf[]` and compares — a concurrent bidirectional transfer across a fork.

## Crash detail (UART log rpi4b-uart-20260827-060636-merge-sweep2.log)
```
Exception #37: Data Abort (EL1)
 x0=00000000004130c0 x1=ffffffffc43080f0 x2=00000000000002f5 x3=00000000004130c0
 x4=ffffffffc43083e5 x5=00000000004133b5 x6=f356380adea10ef8 x7=adb2c6ad5466f9a2
 ... pc=ffffffffc000a3d8 (also c000a388/a30c/a2f0/a358) esr=0x9600004f/0x96000046
 far=0x4130c0 (also 0x413100)
in thread 193, process "/bin/test-libc-unix-socket" (PID: 161)
```
Interpretation: the kernel is running a **copy loop** (`x0`=dst/src `0x4130c0` user, `x1`=
`0xffffffffc43080f0` kernel, `x2`=`0x2f5`=757 bytes, `x6/x7`=copied random payload) that
touches the **sender's user buffer `data`** (`0x4130c0`) and faults — both a translation
fault (esr `..46`, DFSC 0x06 L2, at a region boundary `0x413100`) and a permission fault
(esr `..4f`, DFSC 0x0f L3). pc `0xffffffffc000a3d8` = kernel offset `0xa3d8` from base
`0xffffffffc0000000` (low .text → HAL/user-copy area).

## Narrowing done
- **Not `posix/unix.c`**: the merge did NOT change it (`78a42efb..HEAD` empty; last change
  2026-08-12). AF_UNIX logic unchanged.
- **Merge barely touched this path**: only `vm/map.c` (2 lines: `LIB_ASSERT`→`LIB_ASSERT_VM`
  in `vm_mapBelongs`, both no-ops in release — likely a RED HERRING) + `vm/vm.h` (+8 decls).
  No `hal/aarch64`, no `proc/msg.c`, no user-copy changes.
- X11 (a heavy AF_UNIX user) runs 0-fault → the COMMON AF_UNIX path is fine; this is a
  fork + concurrent-transfer + SOCK_NONBLOCK edge.
- Binary is FRESH (05:14, merged build) → not a stale-binary artifact.

## Hypotheses (to test)
1. **Merge scheduler/lock-refactor regression** (599-line proc/threads.c) exposing a
   process-context/address-space bug in the AF_UNIX send-copy across the fork (kernel copies
   from the wrong process's `data` mapping). Most likely if `transfer` passed on 2026-08-21.
2. **Pre-existing intermittent race**: the test uses `rand()` lengths + SOCK_NONBLOCK + fork,
   so it may crash intermittently on ANY kernel (not merge-specific). Must run multiple trials.

## Next steps (dedicated dig)
1. **Confirm attribution**: restore the pre-merge kernel+libphoenix via
   `manifests/2026-08-27-kernel-libphoenix-upstream-merge.md` rollback, rebuild, run
   test-libc-unix-socket ×N trials. Pass pre-merge + crash post-merge ⇒ merge regression.
   Also run post-merge ×N to check if it's intermittent (pre-existing race).
2. **Symbolize**: the reloc ELF lacks kernel symbols; get/build a symbolized kernel (main
   kernel .elf linked at 0xffffffffc0000000) and addr2line `0xa3d8`; or QEMU+gdbstub
   (`scripts/qemu-debug.sh --gdb`) to break on the fault and inspect the AF_UNIX send/recv
   copy + which process's page table is active.
3. Inspect the kernel AF_UNIX send-copy path (how it copies the sender's user buffer into
   the socket queue, and in which process context) vs the fork/COW mapping of `data[]`.

Severity: real EL1 crash but a concurrency edge (common AF_UNIX / X11 path unaffected).
