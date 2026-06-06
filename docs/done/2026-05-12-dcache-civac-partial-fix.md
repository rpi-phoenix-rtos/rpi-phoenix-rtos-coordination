# 2026-05-12 — D-cache enable: civac partial fix, not yet complete

## Summary

Today's investigation of TD-plo-dcache (the `SCTLR_EL1.C=1` enable that
breaks plo's `exec kernel` path) confirmed the **architectural cause** is
a **mismatched-attributes hazard per ARM ARM B2.8.1** between firmware's
cacheable writes and plo's initial Non-Cacheable accesses. Adding
`dc civac` over plo image + initramfs eliminates the previous
wild-pointer crash in `syspage_entryAdd`. **But a second, distinct
corruption remains** — format-string-level garble during plo's user.plo
command execution — and we haven't yet located its root cause. Reverted
plo to M-only stable.

## Where it started

After today's other wins (EL1 drop, SMP smoke, 4 GB unlock, HDMI to
psh), the only blocker between us and a fast boot is enabling D-cache
(and I-cache, separately) in plo + kernel. With M-only the boot takes
multiple minutes; with caches on it should be seconds.

The pre-investigation symptoms recap (yesterday + earlier):

| Config | What happens |
|---|---|
| M only (caches off) | full boot, slow |
| M\|C (D-cache on, single-shot MSR) | hangs at MSR write |
| M\|C (D-cache on, staged: M then M\|C) | passes through hal_init OK, then wild-pointer fault inside `syspage_entryAdd` during `exec kernel` (FAR holding 0x39186575a0f184af / similar garbage) |
| M\|C\|I (all on) | "first I-fetch returns garbage" → EC=0x00 |

## Hypothesis the data supports

ARM ARM B2.8.1 ("Mismatched memory attributes"): "If the same memory
location is accessed with mismatched memory type or cacheability
attributes ... behavior is UNPREDICTABLE." The required mitigation is
`dc civac` over the affected lines before the second access type.

In our boot:

1. VC4 firmware writes plo's image into PA 0x200000-0x400000 and
   initramfs (loader.disk = plo + preinit.plo + user.plo + kernel ELF
   + apps) into PA 0x08000000+. These writes go through firmware's
   D-cache. Some cluster L2 lines for those PAs may still be resident
   when control passes to plo.
2. plo runs with `SCTLR.{M,C,I}=0`. Per ARM ARM B2.4.4 those accesses
   are treated as **Non-Cacheable** — plo's stores (BSS clear, heap
   zero, syspage init, mmu_init writes to ttl1/ttl2) go *direct to
   RAM*, NOT through cache.
3. plo flips `SCTLR.C=1`. Same PAs are now accessed as Normal-WB-
   Cacheable. This is exactly the mismatch the ARM ARM warns about.

The fix is `dc civac` (clean+invalidate to PoC) over every PA range
plo touches as cacheable, *before* the SCTLR.C write.

## What today's experiments showed

Image SHA256 + observed behavior:

| civac range | SCTLR write style | Result |
|---|---|---|
| none | staged M then M\|C | wild-pointer fault in syspage_entryAdd (the historical crash) |
| plo image (`__text_start..__stack_top`) + initramfs 0x08000000-0x08400000 (4 MB) | staged M then M\|C | **no wild-pointer fault**; banner, "alias: Setting relative base address" CLEAN, "call: opened user.plo on ram0" CLEAN, then format-string garble starting in subsequent prints; eventually plo's `exec` fails with EINVAL trying to read the kernel ELF; drops to (plo)% prompt |
| full low 1 GB (0..0x40000000) | staged M then M\|C | regression — garble appears *earlier* (mid-string in "Setting relative base addre…"); kernel ELF read still fails |
| 4 MB civac | single-shot M\|C in one MSR | hangs at the MSR write — confirms our prior "single-shot hangs" finding even with civac present |

## What I learned

1. **`dc civac` over plo image + 4 MB initramfs is necessary and
   eliminates the wild-pointer crash.** This is real progress and
   should be the foundation of any future attack.
2. **Wider civac (1 GB) made things worse.** Suggests the problem
   isn't just "more PA needs cleaning" — the extra time spent in
   civac may be giving the speculative prefetcher more window to
   re-pollute caches with stale data, OR the cleaning is touching
   PAs that are best left alone.
3. **Single-shot M\|C still hangs even with civac.** Whatever causes
   the single-shot hang is independent of the mismatched-attributes
   hazard. Likely an A72 implementation quirk we haven't named.
4. **The format-string garble pattern** (some prints clean, others
   mid-string corrupted) is consistent with *some* cache lines coming
   from correct RAM and *others* from stale L2. That points at the
   civac not reaching all the relevant cache lines for some reason —
   perhaps cluster L2 isn't fully participating in the inner-
   shareable broadcast domain on this part, perhaps the lines we
   need are getting re-allocated speculatively between civac and the
   actual access, perhaps A72 r0p3 has a bit we're missing.

## What to try next time (suggested attack plan)

1. **Confirm civac fully reaches L2.** Add a probe: write a known
   pattern to a DRAM PA via Non-Cacheable mapping, then re-read it
   via Cacheable mapping; if the value matches RAM (not the
   pre-civac state), L2 was invalidated. If it doesn't, we have a
   reliable repro to drive deeper investigation.
2. **Try `dc isw` set/way clean+invalidate of all levels up to LoC**
   right before SCTLR.C=1, in addition to the per-VA civac. ARM ARM
   says set/way is for power-down only and isn't a coherency
   primitive — but some implementations use it as an additional
   safety net at boot time.
3. **Run a Linux/Circle/Rust boot on the same hardware** with the
   same firmware/armstub and look at how far they get with caches
   on. They reportedly work on Pi 4 with caches enabled. Differential
   gives a concrete answer to "what bit are we missing".
4. **Investigate A72-specific bits** beyond 859971: 1319367
   (`DIS_LOAD_PASS_STORE`?), 853709, 852421, 855873. TF-A applies
   several CPUACTLR_EL1 bits at reset; standard rpi armstub applies
   none. The 859971 bit is set, but the others may matter.
5. **Build with a slim plo** that runs only `mem: pre-sctlr-MC`,
   `msr sctlr_el1`, `mem: post-sctlr-MC`, then HALT — no preinit
   script, no user.plo, no kernel exec. Verify the simplest possible
   MMU+D-cache transition. If the corruption happens at that level,
   it's a pure architectural issue; if it doesn't, it's something
   plo does between SCTLR.C and exec-kernel.

## Current state

Reverted plo's `hal_memoryInit` to M-only. The TODOs are tracked in
the code as TD-plo-dcache and TD-plo-icache. The M-only baseline
remains correct and boots to userspace (psh prompt on HDMI), just
slowly.

Image SHA256 of the M-only stable state: (next rebuild — see manifests)
