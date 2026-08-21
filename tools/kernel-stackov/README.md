# stackov — user main-thread stack-overflow reproducer

Deliberately overflows the main-thread user stack (infinite recursion, 4 KiB
per frame). Used to exercise the kernel's signal-delivery-on-exhausted-stack
path.

## Build + run
```
.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc \
  --sysroot=.buildroot/_build/aarch64a72-generic-rpi4b/sysroot -O0 -o stackov stackov.c
sudo cp stackov /srv/phoenix-rpi4-nfs/root/stackov
./scripts/test-cycle-psh-interact.sh --label stackov --idle-secs 10 -- /root/stackov
```

## Current behavior (2026-08-21, BEFORE the signal-push fix)
Overflow → EL0 Data Abort (SIGSEGV) → the kernel double-faults (EL1 Data Abort
#37) pushing the signal frame below the exhausted user SP (`hal_cpuPushSignal`
→ `hal_memcpy`), corrupting the crash dump. Confirmed:
`Exception #36: Data Abort (EL0)` immediately followed by
`Exception #37: Data Abort (EL1)`.

## Expected behavior AFTER the fix
A single, clean process termination (proc_kill) with a RELIABLE EL0 dump and NO
EL1 #37. See memory project_coreutils_cksum_od_dataabort (FIX #1).
