# 2026-05-13 — plo D-cache enable: empirical pivot to kernel-side

## Summary

Today's investigation closed the **single biggest known hazard** in cache
enable (Cortex-A72 erratum 1319367 — speculative AT across translation-regime
boundary) by applying its workaround in our armstub at EL3 reset, before
control reaches plo. The 1319367 fix:

* survived the M-only baseline regression test cleanly,
* eliminated the historical wild-pointer fault inside `syspage_entryAdd`
  that had killed every staged M|C boot attempt to date,
* but **did NOT solve plo cache enable on its own.** Two further hazards
  showed up empirically:

| Failure | Cache state at hit | Notes |
|---|---|---|
| Single-shot `SCTLR.M\|C` MSR hangs | M\|C in one write | A72 quirk independent of 1319367. Linux/Circle/NetBSD all use staged. |
| `bg-fill` memset to framebuffer (800K Device-attr u32 writes) hangs | After SCTLR.C=1, FB at 0x3e87c000 mapped Device | Likely SoC fabric back-pressure on streaming Device writes after cache transition |
| Mid-string printf garble + ELF EINVAL | After bg-fill skip, banner clean | Cache contents diverge from RAM mid-print, intermittent position |

All three are **distinct** from 1319367 and each other.

## What we proved this session

1. **1319367 is necessary.** Adding `CPUACTLR2_EL1[0]=1` to the armstub
   reset path (after 859971 and before SMPEN) is the missing piece TF-A
   applies unconditionally and Phoenix's previous armstub had skipped.
2. **Staged M-then-MC works through `hal_memoryInit`.** Putting an `isb`
   between the SCTLR.M=1 and SCTLR.C=1 writes clears the single-shot hang
   that has shadowed all M|C attempts.
3. **Civac is counter-productive on A72 with unified L2.** A72's L2
   cache holds I-side AND D-side lines. Firmware-left dirty L2 lines for
   plo's PA range get *written back* by `dc civac`, OVERWRITING the
   correct plo code in RAM. This poisons subsequent fetches.
4. **`dc ivac` (invalidate-only) is the correct primitive** in principle
   but didn't fully clear the residual print garble in practice — there
   is at least one more hazard not yet pinned.
5. **Wide cache maintenance (944 MB) produces a translation-fault-L2 at
   the *next* dcacheClean of the framebuffer** — pathological. Narrow
   (plo image + initramfs only) doesn't.

## Why we're pivoting

The remaining print garble after `dc ivac` is sporadic and position-
dependent ("alias: Setting relative base addre…" garbles after ~34
chars, "call: magic ok " garbles after the space, etc) and isn't yet
pinned to a specific hardware primitive. Continuing to brute-force
fixes in plo costs many full rebuild+netboot cycles per attempt with
no convergence.

The **kernel runs at high VAs (kernel ELF, VADDR_KERNEL_INIT et al)
that firmware never wrote to**. The class of "firmware-cache-pollution
on plo's PAs" hazards we've been fighting is largely absent from the
kernel address space. Kernel cache enable (TD-16) is therefore a
cleaner attack surface, with the 1319367 erratum now already applied
upstream of it.

## Concrete next steps

1. Lock in: keep the armstub 1319367 fix (proven safe + improves base);
   revert plo + video.c to M-only stable (the configuration that boots
   end-to-end to psh).
2. Move to kernel-side cache enable in `phoenix-rtos-kernel/hal/
   aarch64/_init.S` (look for `TD-16-cache-enable`).
3. Once kernel cache works, total boot time drops by 10-100× even with
   plo still M-only — plo is a one-shot small image, kernel is the
   long-running workload.
4. With cache+SMP+kernel all functional, plo cache becomes
   a "minor optimisation, not on the critical path" item.

## Image SHAs from today (for archaeology)

* `f8c3343e…dc56c581` — armstub 1319367 + plo M-only (proves baseline)
* `163699e7…3057ac69` — plo single-shot M|C (hangs at MSR)
* `6af9e371…be8f4f8c` — plo staged M-then-MC (boots through hal_memoryInit, hangs at bg-fill)
* `370acdec…7f33c6e5` — staged M|C + bg-fill skipped (reaches banner, FAR=garbage on user.plo)
* `91ccb9fd…420dc71f` — narrow civac (clears FAR fault, leaves printf garble)
* `e42a6686…450130e8` — wide civac (translation-fault-L2 at FB dcacheClean)
* `d5b66470…3ff923d2` — wide ivac (same translation fault — invalidate vs clean doesn't change it)
