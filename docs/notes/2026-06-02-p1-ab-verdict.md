# P1 A/B test verdict — does the PCIe pre-init dump cause USB enum flakiness? (2026-06-02)

**Question:** the post-fix 10-boot study (`2026-06-02-p1p2p3-postfix-10boot.md`)
credited **P1** (deleting the USB-FIX-18 PCIe pre-init bridge-state register dump)
with USB enumeration going **3/10 → 10/10**. Controlled A/B to confirm.

**Method.** Same consolidated tree, same physical devices (VIA hub `2109:3431`,
Logitech kbd `046d:c31c`, PIXART mouse `093a:2510`). Two arms, 8 netboots each,
**arm B first** (so VL805/bridge cycle-degradation works *against* the
hypothesis):
- **Arm B** = dump **restored** (image `52a75094`, every boot verified `dump=1`).
- **Arm A** = dump **removed** = current `master` (image `c10502c1`, every boot
  verified `dump=0`).
Per-boot classifier (corrected mid-run — the original `enum_fail` grep was blind
to the HCD-init failure mode): **FAIL** if `New device` count == 0 **OR**
`Fail to init hcds`/`ops->init fail` present **OR** `Enumeration failed` present.

**Result.**

| arm | dump | PASS | FAIL | failure modes |
|-----|------|------|------|---------------|
| B   | yes  | 6/8  | 2/8  | abb06 HCD-init `rc=-110`; abb07 downstream enum-fail (`New device`=1) |
| A   | no   | 8/8  | 0/8  | — |

Fisher exact (2 vs 0 failures, n=8/arm): **two-tailed p ≈ 0.47 — NOT
significant.** HDMI confirmed (per the always-check-HDMI rule): the abb06 fail
frame shows the full kernel log *and* `usb: Fail to init hcds!` on screen
(networking still up); pass frames match the known-good enumeration screen
(`bbc74d82`).

## Verdict (honest)

1. **P1 is NOT the cause of the 3/10→10/10 swing the study credited it with.**
   Re-adding the dump produced only **2/8 (25%)** failures — nowhere near the
   pre-fix baseline's **7/10 (70%)**. If the dump were the dominant cause,
   restoring it should have reproduced ~70%. It didn't. So the dramatic
   baseline→postfix improvement was **confounded** (different pre-consolidation
   code state and/or session/cabling between the `boot01-10` baseline and the
   `fix01-10` run), not caused by P1.
2. **USB enumeration is intermittently flaky regardless of the dump** — arm A
   0/8, arm B 2/8. The flakiness is real but low-rate here (~0–25%), and the
   dump's contribution is at most marginal and statistically insignificant.
   Both intrinsic failure modes appeared (HCD-init `rc=-110` timeout; downstream
   LS-device-behind-TT enum-fail) — these are the FIX-14 / TD-10 family.
3. **P1's confirmed wins stand and are unchanged:** ~100 s faster boot (the
   ~10.8 s-per-register pre-`SERDES_IDDQ` abort stalls are gone) and the
   observability/cleanup of a disproved diagnostic. P1 was simply never an
   enumeration fix — that framing in `status.md` is corrected.
4. **The real USB wall is the intermittent bring-up/downstream completion**
   (FIX-14 #78) and the masked PCIe SError (TD-10 #144), not the dump.

## Caveats
- n=8/arm is small; "dump slightly raises failures" cannot be ruled out (it's
  just not significant). A definitive rate needs ~30+ boots/arm — not worth it,
  since the actionable conclusion (P1 ≠ enum fix; real wall = FIX-14/TD-10) holds
  either way.
- **Build-pipeline hazard found during this test:** `rebuild-rpi4b-fast.sh
  --scope auto` runs `project image` only when the source repos are *clean*, so
  after committing (or merging) a **core** change it can ship a **stale core
  image** (the first arm-A build did exactly this — `loader.disk` still had the
  dump). Always rebuild with **`--scope core`** after a committed core/devices/
  kernel change, and gate on `strings loader.disk | grep <expected-change>`.
