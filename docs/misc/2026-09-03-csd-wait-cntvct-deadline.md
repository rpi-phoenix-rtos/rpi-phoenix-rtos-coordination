# Prepared change: CSD completion wait -> CNTVCT_EL0 deadline

Apply AFTER the running full-clean build finishes (editing
sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/v3d_phoenix_winsys.c mid-build
would make that build non-reproducible: build-v3d-phoenix.py compiles the glue
straight out of the source tree).

## Why the current form is wrong even though it works

`ioc_submit_csd()` waits with a fixed spin count (now 80,000,000). A spin count
is not a timeout: it means a different wall-clock budget on every core clock and
every compiler revision, so the value cannot be reasoned about or ported. The 10x
raise was the right *mitigation* -- it stopped vkQuake rendering a black world --
but the shape is wrong.

## Reference point

Linux's v3d scheduler declares a job dead after **500 ms**
(`external/linux/drivers/gpu/drm/v3d/v3d_sched.c:859`, `.timeout =
msecs_to_jiffies(500)`).

Note the implication for our old 8M budget: if real dispatches were exceeding it,
they were taking longer than Linux's entire job timeout, OR the loop is much
faster per iteration than assumed. We do not currently know which -- which is
exactly why the new code must MEASURE, not just bound.

## The change

Read `CNTVCT_EL0` directly. It is a real monotonic clock, readable from EL0 since
the CNTKCTL_EL1.EL0VCTEN/EL0PCTEN fix, and needs no syscall, so it is safe inside
a poll loop (the MMIO read of INT_STS dominates the cost anyway).

    /* Fixed at 2 s, not Linux's 500 ms: this driver waits synchronously in the
     * calling thread, and a false timeout does not retry -- it silently drops the
     * dispatch's output, which is what rendered vkQuake's world black. Better to
     * be slow once than wrong quietly. Still bounded, so a wedged dispatch cannot
     * hang the process. */
    #define CSD_WAIT_MS 2000u

    static inline uint64_t cntvct_now(void)
    {
        uint64_t v;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
        return v;
    }

in ioc_submit_csd(), replacing the spin loop:

    uint64_t freq, deadline = 0;

    __asm__ volatile("isb" ::: "memory");
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (freq != 0) {
        deadline = cntvct_now() + (freq / 1000u) * CSD_WAIT_MS;
    }

    for (spins = 80000000u; spins; spins--) {
        sts = c0[CTL_INT_STS / 4];
        if (sts & INT_CSDDONE) {
            break;
        }
        /* freq == 0 means the counter is unusable; the spin count is then the
         * only bound, so keep it as the fallback rather than spinning forever. */
        if (deadline != 0 && cntvct_now() >= deadline) {
            break;
        }
    }
    if (!(sts & INT_CSDDONE)) {
        timed_out = 1;
    }

plus a high-water mark so the budget stops being a guess:

    static uint64_t csd_wait_max;   /* file scope, ticks */
    ...
    uint64_t waited = cntvct_now() - t0;
    if (waited > csd_wait_max) {
        csd_wait_max = waited;
        /* env-gated so it cannot spam UART in a normal run */
        if (getenv("V3D_CSD_WAITSTAT") != NULL) {
            fprintf(stderr, "v3d-winsys: CSD new max wait %llu us\n", ...);
        }
    }

and include the measured wait in the existing TIMEOUT line.

## Verification required (not optional)

1. `--scope core` rebuild -- committed devices change, stale-core hazard.
2. One Pi cycle running vkQuake with `V3D_CSD_WAITSTAT=1`: confirm the world
   renders AND read off the max observed dispatch time. That number is what tells
   us whether 2 s is generous or barely enough, and whether the old 8M budget was
   ~0.16 s or ~0.6 s.
3. Only then is the timeout value defensible in a commit message.
