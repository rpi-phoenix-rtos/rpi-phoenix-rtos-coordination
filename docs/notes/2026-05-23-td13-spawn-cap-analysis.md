# TD-13-spawn-cap analysis (2026-05-23)

Read-through of the spawn-loop bug to plan a future root-cause
attempt. No code changes this session; capturing the data so the
next iteration doesn't have to re-derive it.

## What we know

From the 2026-05-01 commit `c5c21c6e` (kernel) and the TD docs:

- Real Pi 4 spawn loop iterates correctly through all 9 progs.
- After psh is spawned, `psh->next != syspage_progList()` (the
  head) so the do-while terminator never fires.
- Loop body re-runs ~187 000 times before the spawn-cap (32) was
  added.
- Same image runs cleanly to `(psh)% help` in QEMU.
- The TD-13 user-mode-silence component is resolved (commit
  message at the END of the TD-13 entry in
  `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`).

## Plausible causes

1. **Cache coherency on the syspage destination page** —
   `_hal_syspageCopied` is at `VADDR_SYSPAGE` (high VA),
   bootstrap copies the bytes through that VA. The TD-04 NC
   override on that single TTL3 entry was deleted in Phase Z3
   (`2026-05-17`, commit referenced in `_init.S` line 505),
   under the rationale that plo now runs caches-on + does
   `dc civac` over all DDR on teardown. If the LAST prog's
   pointer falls on a still-stale cache line that the teardown
   missed, reads come back wrong. The most prog struct write is
   the most recently added, so it's the most likely to be in a
   dirty cache line at plo teardown.

2. **The kernel modifies `prog->argv` mid-loop** (main.c line 100,
   `++prog->argv;`). This is a write to the syspage page from
   user-mode-aware code. If the dest mapping is cacheable, the
   write goes to the kernel D-cache. Subsequent reads from a
   different VA (e.g. via plo's syspage_progIdResolve) would see
   the older value. Probably not the spawn-cap cause but worth
   noting.

3. **Stale destination bytes from a previous boot** that the
   bootstrap copy doesn't overwrite. The dest is
   `_hal_syspageCopied` which is `.zero SIZE_PAGE` in BSS, so it
   gets zeroed at link/load time. BSS zeroing happens... actually
   it depends on the loader. If kernel8.img's BSS isn't cleared
   by the kernel8-reloc trampoline, fields beyond the actual
   syspage size keep whatever was in DDR before.

## Smallest experiment that would actually localize this

In `main.c` right before the spawn loop, print:
- `syspage_progList()` address
- For each prog from head through head->next->...->head (with a
  loop cap), print prog's address, prog->next, prog->argv[:8].

This dump would reveal whether:
- `psh->next` points at a 4th-9th-prog clone (cache-stale)
- `psh->next` points at uninitialized memory (BSS-not-zeroed)
- The list is genuinely circular and `syspage_progList()` is
  shifting under our feet

Use `hal_consolePrint` direct-to-UART (not `lib_printf` → klog),
since the klog buffer doesn't drain to UART before psh starts on
Pi 4 boot path.

This requires hardware to validate; bridge currently in poison
state so deferred until a fresh power cycle.

## What to NOT do

- Don't re-add the TD-04 NC override on `_hal_syspageCopied`
  yet — Phase Z1's canonical pattern (plo caches-on + teardown
  flush) is the supposed-correct fix and the NC override was
  removed for a reason (re-introducing it risks a regression in
  whatever Phase Z1 closed).
- Don't lift the spawn cap before root-causing — 187K iterations
  would burn ~3 s of boot time and may interact badly with the
  `(void)posix_clone(-1)` line just above the loop if PIDs are
  exhausted.

## Status

Spawn-cap workaround at 32 iterations remains in place. Real
boot ships 9 progs so the cap never fires in practice
(`grep "TD-13 spawn-cap hit"` in any recent boot log returns
nothing — the natural do-while terminator wins on the current
build).

Captured for the next iteration. No code change.
