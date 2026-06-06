# 2026-05-23 — USB BCM2711 bridge cooldown curve

## What I observed

After previous sessions left the BCM2711 PCIe bridge degraded (all
boots returning `rc=-19` from cap-space poison reads), I tracked
its recovery + re-degradation in this loop iteration:

| Cycle | Time vs prior test | rc                | Bridge state    |
|------:|--------------------|-------------------|-----------------|
| 1     | ~30 min idle       | -110              | healthy         |
| 2     | immediate          | -19               | re-degraded     |
| 3     | immediate          | -19               | degraded        |
| 4     | immediate          | -19               | degraded        |

## Conclusion

- 30 minutes of idle is enough to recover the bridge from a poison
  state to a "controller reachable but reset times out" state.
- Three back-to-back boot cycles re-degrades it.
- The threshold is < 3 cycles in a few minutes; an exact curve
  would need a longer test plan with controlled spacing.

## Implication for development cadence

Rapid iteration via `test-cycle-psh-interact.sh` is fighting the
hardware. To validate any USB-side change reliably we need:
  1. A long idle gap (≥30 min) before the first test of a build, and
  2. **At most one test per build**, otherwise the 2nd/3rd cycles
     report degraded-bridge rc=-19 instead of the actual code-side
     behaviour.

This rules out my preferred pattern of "run 3-4 cycles per change
to see the rate of rc=-110 vs rc=-19". The next iteration has to
accept one data point per change and commit to longer wall-clock
cadence.

## What I tried this iteration

Re-enabled the `bcm2711_pcie_resettleOutboundWindow()` call inside
`xhci_reset` (the public function added in commit `77fe93a`,
previously reverted because the bridge was degraded). Tested:
  - run 1: rc=-19
  - run 2: rc=-19
  - run 3: rc=-19

Inconclusive — bridge re-degraded across the 3 runs faster than I
could distinguish a code effect. Reverted the call. The resettle
primitive remains exported for the next iteration which will
follow the one-test-per-build rule.

## Status

USB code on `codex/upstream-sync-20260516` HEAD: same as previous
iteration (4 KiB BAR, HCH guard, 50 ms post-reprogram settling,
cap-probe in-loop retry, public resettle primitive — but not
called from xhci_reset).

Documented for the next session.
