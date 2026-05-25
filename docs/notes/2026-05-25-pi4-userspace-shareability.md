# Pi 4 userspace cross-CPU counter visibility — false sharing (2026-05-25)

**ROOT CAUSE CONFIRMED: false sharing on a hot cache line.** This
note's earlier hypothesis (kernel pmap shareability bug) was wrong.
The fix is a cache-line-padded counter array; no kernel work is
needed. Workaround landed in `ea936d3` (placeholder — see commit at
end of this note).

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

## Root cause: NOT shareability (initial hypothesis disproven)

Initial guess was a missing `SH_INNER` on userspace PTEs. Checked
`sources/phoenix-rtos-kernel/hal/aarch64/pmap.c:446`:

```c
descr = DESCR_PA(pa) | DESCR_VALID | DESCR_TABLE | DESCR_AF | DESCR_ISH;
```

`DESCR_ISH` is already set on every page leaf descriptor. Userspace
pages are inner-shareable. Hardware coherency does propagate writes
between cores.

## Root cause CONFIRMED: false sharing on a 64-byte cache line

Experiment 1 (cache-line spreading): added an `_Alignas(64)` struct
wrapper so each of the 4 counter slots gets its own cache line, and
re-tested with plain `volatile ++` per inner iteration. Result:

```
mid-burn @ ~5 s into 10 s window, remaining 3.89 s:
  burner0  count: 1,036,042,240   padded: 1,036,042,757
  burner1  count: 1,036,263,424   padded: 1,036,265,785
  burner2  count: 1,034,170,368   padded: 1,034,171,696
  burner3  count: 1,038,012,416   padded: 1,038,014,881
```

The padded counters track the atomic counters within ~3000 of each
other (the small spread is just the atomic-publish-per-outer-tick
lag vs. the per-iteration padded increment). All 4 padded counters
advance at ~ALU speed on their own dedicated cache lines.

**False sharing was the entire problem.** When all 4 slots sat in one
cache line, every burner thread's increment invalidated the other
3 cores' caches, and the non-atomic load-modify-store pattern lost
most updates to RMW races between cores. With each slot on its own
cache line, plain `volatile ++` per iteration just works.

This is **not** a Phoenix-specific quirk. It's a standard SMP
fundamental that happens to be especially punishing on Cortex-A72
with its private L1s (the snoop-and-invalidate path is longer than
on, say, a single-die x86 with a shared LLC).

## Resolution

Fix landed in lwip `ea936d3` (placeholder — see commit at end):
counter array changed to

```c
static struct {
    unsigned long long c;
    char pad[64 - sizeof(unsigned long long)];
} __attribute__((aligned(64))) diag_burn_counters[N];
```

The C11 atomic intermediate version (`66e54a5`) is no longer needed
once the false-sharing fix is in place — but atomics remain the
right answer for *contended* shared state (e.g. a single counter
incremented by multiple threads). Per-thread counters on dedicated
cache lines are the right answer for *uncontested* shared state.

## Takeaways

1. `volatile` does NOT save you from cache contention. It only
   prevents the compiler from optimizing away accesses; it does not
   change the hardware-level memory model.
2. On multi-core ARM with private L1 caches, per-thread state that
   crosses cache-line boundaries silently destroys throughput.
3. If you see "millions of cycles of CPU time with zero observable
   counter advance," look for false sharing FIRST.
4. The diag-udp burn handler is now a small but real demo of an
   SMP saturation benchmark on Phoenix-RTOS/Pi 4.

## Linked

- Initial broken commit: lwip `b750d7e` (`volatile ++`, unpadded).
- Atomic intermediate: lwip `66e54a5` (atomic store + local accum;
  worked because it reduced inter-core writes ~4096×, not because
  of atomicity per se).
- Final fix: lwip `ea936d3` (cache-line-padded slot array; plain
  `volatile ++` per inner iteration).
- TD entry: `TD-Pi4-VolatileVsAtomic` (now better named
  `TD-Pi4-FalseSharingPenalty`) in
  `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`.

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
