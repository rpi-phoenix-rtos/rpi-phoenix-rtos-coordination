# 2026-05-13 — iter-7/8/9 test plan (Pi unavailable — awaiting return)

This note is for the next session when the Pi is reachable again. The
locked-in baseline (image SHA `c6fb8ab9…bacf1ead`) boots reliably to a
`(psh)%` prompt. The following iterations are **uncommitted** changes
sitting in the worktree, intended to push past the kernel D-cache-
enable barrier (TD-16).

## Current uncommitted state (as of 2026-05-13 evening)

```
sources/phoenix-rtos-kernel/hal/aarch64/_init.S    (iter-7 + iter-8 stacked)
sources/plo/hal/aarch64/generic/hal.c              (iter-9 plo-teardown fix)
```

Image SHAs available in `artifacts/rpi4b/`:

| Image SHA (head) | Iter | Tested |
|---|---|---|
| `c6fb8ab9…bacf1ead` | M-only stable + 1319367 armstub | ✅ boots to psh (locked-in baseline) |
| `680e83f1…c0cc97df` | Equivalent earlier rebuild | ✅ boots to psh |
| `d62b85a8…b16a4c19c1cc` | iter-6 (staged M\|C in kernel, both at the original SCTLR site) | ❌ Translation-fault-L3 @ `0xffffffffc0001890` |
| `06af0ead…d1107ae4` | iter-6 with C=1 deferred to just before `br x0` | ❌ SAME fault |
| `7dea6c3c…b15e5ae5` | iter-7 (defer C=1 + ivac kernel PT region pre-C=1) | ❌ SAME fault |
| `4c4e2bbe…21a041d3` | iter-8 (iter-7 + NC_BLOCK_ATTRS → Cacheable WBWA) | **NOT yet tested** |
| `064c10b2…3cc188cb` | iter-9 (iter-8 + plo teardown civac → ivac) | **NOT yet tested** |

## Why iter-9 might work where iter-7/8 didn't

Plo's `hal_cpuJump` teardown calls `dc civac` over the entire low DDR
range (~4 GB minus GPU reserve). Per the canonical pattern this is
meant to clean any dirty cache lines before handoff.

But: plo currently runs **M-only** (SCTLR.C=0). Every plo write went
DIRECT to RAM, bypassing cache. So plo has NO dirty cache lines at
teardown time. The only cache content is whatever firmware left in
cluster L2 from its own boot activity.

Cortex-A72 has a **unified L2** (I+D). `dc civac` on stale firmware-
dirty lines writes them BACK to RAM — overwriting the correct
kernel-image / syspage / page-table contents plo just wrote. Then it
invalidates the lines. Net result: RAM is corrupted with stale
firmware data exactly where it matters.

Symptom this matches:
- Kernel boots fine with **caches off** (no civac corruption observable
  because the walker reads from RAM each time — but RAM bytes for code
  are mostly the right bytes; corruption sits in specific spots).
- The moment kernel enables D-cache, the walker hits "translation
  fault L3" on kernel image page 1 (`FAR=0xffffffffc0001890`) —
  exactly the bytes civac may have overwritten.

iter-9 switches the teardown to `dc ivac` (invalidate-only). Plo has
no dirty data to lose; stale firmware lines are simply discarded.
RAM stays correct.

## Test plan when Pi is back

In order:

### Step 1 — Regression check on baseline

```sh
# Restore baseline image artifact (if needed):
./scripts/restore-integration-state.sh manifests/2026-05-13-armstub-1319367-final.md
./scripts/rebuild-rpi4b-fast.sh   # should produce c6fb8ab9…bacf1ead or equivalent
./scripts/test-cycle-netboot.sh --label baseline-recheck --capture-secs 240
```

Expect: log reaches `(psh)%` prompt. If it does not, fix that before
attempting iter-9.

### Step 2 — Run iter-9 (everything together)

The worktree currently has iter-9 staged. Image SHA `064c10b2…3cc188cb`
already built and sitting in `artifacts/rpi4b/`. To run:

```sh
# Use the already-built image OR rebuild for safety:
./scripts/rebuild-rpi4b-fast.sh
./scripts/test-cycle-netboot.sh --label iter-9 --capture-secs 240
```

**Look for:**
- New armstub markers `1`, `3`, `2` (859971 / 1319367 / SMPEN, unchanged from baseline)
- Plo prints `mem: post-sctlr-M` (still M-only)
- Plo prints `hal: jump exit el1` (teardown beginning)
- Kernel prints `M1` then `M2` (kernel's staged M then C)
- Kernel reaches `Phoenix-RTOS microkernel`, then psh.

**If iter-9 succeeds:** we have kernel D-cache enabled end-to-end.
Move to commit and then attempt I-cache enable similarly.

**If iter-9 fails:** record the exact FAR / ELR / ESR. Then proceed to
diagnostic Step 3.

### Step 3 — Bisect what's needed (if iter-9 fails)

Two changes are stacked in iter-9: plo teardown (`civac` → `ivac`) and
kernel staging (`ivac` PT region + deferred C=1 + cacheable TTBR0
blocks). To find out which is necessary, run:

```sh
# Variant A — kernel changes only (plo civac restored)
cd sources/plo && git checkout hal/aarch64/generic/hal.c
cd ../..
./scripts/rebuild-rpi4b-fast.sh
./scripts/test-cycle-netboot.sh --label iter-9-no-plo --capture-secs 240

# Variant B — plo change only (kernel restored to baseline)
cd sources/plo && git diff   # confirm hal.c is back to civac
cd ../sources/phoenix-rtos-kernel && git checkout hal/aarch64/_init.S
cd ../..
# Then re-apply plo change manually (the civac → ivac edit) or
# cherry-pick from the worktree's reflog
```

Outcomes to record in the next research note:
- Both required → both fixes were necessary
- Only kernel needed → plo teardown was a red herring (revert plo)
- Only plo needed → kernel changes weren't the issue; was plo
  corrupting RAM all along

### Step 4 — Forward path on success

If anything in 2 or 3 boots to psh with caches on:
1. Time how long boot takes (vs minutes-long M-only baseline).
2. Commit plo and/or kernel changes with the iteration-number naming.
3. Refresh `manifests/` and `docs/research/`.
4. Move to I-cache enable (SCTLR.I=1) by the same staged pattern.
5. Then USB + keyboard work (which becomes practical at cache-on speed).

## Drafted iter-10 (only if iter-9 fails)

If iter-9 doesn't unblock, the deeper restructure is to move all
kernel PT setup BEFORE `SCTLR.M=1`, matching Linux's `__cpu_setup`
pattern. Notes:

- `ldaxr` / `stlxr` (used for the `nCpusStarted` atomic in
  `_init.S`) require Normal Cacheable memory for the exclusive
  monitor; can't happen pre-MMU. Move that init AFTER the SCTLR
  flip.
- Stack setup currently follows MMU enable; either move it earlier
  (using PA directly) or accept a no-stack window until SCTLR.M=1.
- Stage 1 cache-enable would then look like:
  1. TCR/MAIR/TTBR0/TTBR1 setup (no walker active).
  2. Page table construction (no walker active).
  3. `dsb sy` + `ic ialluis` + `tlbi vmalle1is` + `dsb sy` + `isb`.
  4. SCTLR `orr` M | C | I in ONE write (staged-M may be needed if
     A72 still hangs on M|C single-shot at EL1 — we have evidence
     this happens at EL1 just like plo at EL2).
  5. `isb` + `br x0` to high VA.

This is several hours of restructure plus careful interrupt-vector
movement. Hold off until iter-9 result is known.

## Files touched (worktree state at session end)

* `sources/plo/hal/aarch64/generic/hal.c` — `hal_cpuJump` teardown
  switches from `hal_dcacheFlush` (civac) to `hal_dcacheInval` (ivac).
* `sources/phoenix-rtos-kernel/hal/aarch64/_init.S` —
  - `NC_BLOCK_ATTRS` macro changed from `0xf05` (NC) to `0xf01`
    (Cacheable WBWA)
  - Added `_inval_dcache_range` over TTL2..STACK and `SCTLR.C=1`
    enable after the `tlbi vmalle1is`, just before `uart_putc_virt
    78` and the high-VA branch.

To revert wholesale: `git checkout hal/aarch64/_init.S` in the kernel
repo and `git checkout hal/aarch64/generic/hal.c` in the plo repo.
