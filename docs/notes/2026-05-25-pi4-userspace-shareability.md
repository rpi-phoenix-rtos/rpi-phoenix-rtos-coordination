# Pi 4 userspace cross-CPU write visibility (2026-05-25)

Bonus finding from the SMP Phase E saturation harden. Worth its own
note because it affects every future multi-threaded userspace
program on Phoenix-on-Pi4, not just the test.

## What happened

The first Phase E saturation harden (lwip `b750d7e`) used:

```c
static volatile unsigned long long diag_burn_counters[4];

static void diag_burnThread(void *arg)
{
    unsigned slot = (unsigned)(uintptr_t)arg;
    ...
    while (gettime() < deadline) {
        for (int i = 0; i < 4096; ++i) {
            diag_burn_counters[slot]++;   /* plain volatile ++ */
        }
    }
    ...
}
```

Four threads, four distinct slots, no contention. Each thread should
write only to its own slot.

Result (10 s burn window, 4 burners):

| Reader sees    | burner0 | burner1 | burner2 | burner3 |
| -------------- | ------: | ------: | ------: | ------: |
| at +5s mid     |    4096 |       0 |       0 |       0 |
| at +11s post   |    8192 |       0 |       0 |       0 |

Yet each burner thread had accumulated ~5 s of kernel-side cpuTime,
confirming all 4 ran on separate cores at ~94% saturation each.
Writes happened — they just didn't reach cpu 0's reader.

## The fix that worked

`66e54a5`: change the increment to a C11 atomic store, and the
reader to a C11 atomic load:

```c
unsigned long long local = 0;
while (gettime() < deadline) {
    for (int i = 0; i < 4096; ++i) {
        local++;
    }
    __atomic_store_n(&diag_burn_counters[slot], local, __ATOMIC_RELEASE);
}
```

Reader:

```c
unsigned long long c = __atomic_load_n(&diag_burn_counters[i], __ATOMIC_ACQUIRE);
```

Result (10 s burn window, same 4 burners, fresh boot):

| Reader sees    | burner0       | burner1       | burner2       | burner3       |
| -------------- | ------------: | ------------: | ------------: | ------------: |
| at +5s mid     | 2,229,604,352 | 2,223,714,304 | 2,223,853,568 | 2,224,955,392 |

All four counters advancing in lockstep, within 0.26% spread.

## Why this matters

On aarch64, `__ATOMIC_RELEASE` store lowers to `stlr`, which on
Cortex-A72 publishes the value to the *inner-shareable* domain (and
also enforces release ordering against earlier accesses). A plain
`str` (what `volatile T*` writes generate) publishes only to the
local cache hierarchy. ARM hardware coherency only kicks in within
the inner-shareable domain; pages mapped outer-shareable or
non-shareable do not coherently propagate writes between cores.

So: **the lwip-port's BSS / data pages on Phoenix-Pi4 are NOT
inner-shareable**. The implication runs deeper than this one test:

- pthread mutexes are fine — `pthread_mutex_lock` is built on
  atomic ops with the proper memory ordering.
- C11 atomics (`<stdatomic.h>` or `__atomic_*`) are fine.
- `volatile`-only patterns (a common pattern in single-threaded
  ISR-vs-mainline code) are **broken** for cross-CPU shared state
  on Pi 4 userspace.
- Plain `static` shared globals are equally broken without barriers.

## Severity

Practical impact today: low (only the diag-udp burn handler was
affected, and it now uses atomics). Conceptual impact going forward:
high — anyone writing multi-threaded userspace code on Phoenix-Pi4
needs to know this. Logged as `TD-Pi4-UserspaceCacheShareability` in
the TD registry.

## Root cause hypothesis (unverified)

Phoenix's `pmap` on aarch64 sets userspace data-page attributes to
something like `MAIR_ATTR_NORMAL_INNER_WB_OUTER_WB | SH_OUTER` or
`SH_NONE`. The fix is to flip to `SH_INNER` for any pages that
might be cross-shared.

Linux/musl/glibc all assume `SH_INNER` for userspace; that's the
default on Linux's aarch64 mappings. Phoenix may have inherited a
single-core assumption from earlier ARMv7 ports.

To confirm: read `MAIR_EL1` and the relevant TTBR entries for a
userspace VA via gdbstub or QEMU. Or, simpler, write a small kernel
patch that switches `pmap`'s userspace ATTRIDX/SH bits and re-run
the diag-udp burn with the plain-volatile version — if counters
advance, the hypothesis is confirmed.

## Resolution path

This is **kernel work**, not lwip-port. The fix likely lives in
`sources/phoenix-rtos-kernel/hal/aarch64/pmap.c` and might be just
a few lines (the SH_INNER bit at PT-leaf granularity). Tractable
once someone scopes it; not blocking on anything.

## Linked

- Discovery commit: lwip `b750d7e` (initial burn — `volatile ++`
  broken).
- Fix commit: lwip `66e54a5` (C11 atomics — works).
- Phase E saturation note: `docs/notes/2026-05-25-smp-phase-e-saturation.md`.
- TD entry: `TD-Pi4-UserspaceCacheShareability` in
  `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`.
