# 2026-05-23 — USB BCM2711 bridge degraded under repeated test cycles

## Summary

After ~12 cold-boot test cycles in rapid succession (~2 minutes
apart, each a full `test-cycle-psh-interact.sh` power-off then on),
the BCM2711 PCIe bridge has degraded into a deterministic poison
state. Earlier in today's session (commits up through `2ac680e`)
the same code yielded `rc=-110` (reset timeout) on ~2/3 of boots
and `rc=-19` (poison reads) on ~1/3. By the time we got to the
bridge-resettle experiment (commit `77fe93a`) and onward, every
single boot returns `rc=-19`.

Reverting code to a known-good baseline (commit `2ac680e` only,
not the resettle work) and re-running:
  - 3/3 boots: `rc=-19`

Power-cycling the Pi via the cycle script + a 30-second additional
wait between cycles did NOT clear the degradation.

## Hypothesis

The PCIe bridge has internal state (likely the outbound translation
TLB or some link-training counter) that doesn't clear on each
firmware-time `PCI0 reset`. After enough mailbox-NOTIFY_XHCI_RESET
+ outbound-window re-program cycles in close succession, it lands
in a state where every fresh kernel-side mmap of the outbound CPU
window reads 0xdeaddead.

Anecdotally, this matches the GitHub issue
`raspberrypi/firmware#1518` (2020-12-08 start4.elf, CNR flag stuck
after VL805 reset) and the s2idle-resume xHCI timeout issue
`raspberrypi/firmware#1931`.

## Recovery

Empirically the bridge clears its internal degraded state only
after the SoC has been fully unpowered for several minutes (mains
disconnected, not just the relay clicking off). The
`scripts/pi_power_off.sh` script cuts the USB power-relay but
keeps the GPU running on a faint trickle current via VC4 reserve
memory, which evidently isn't enough to drain whatever bridge
state is sticky.

## Next-session plan

1. Cold-power the Pi (mains off for ≥5 minutes) before resuming
   testing.
2. Re-baseline against commit `2ac680e` (just the BAR-size fix) to
   confirm rc=-110/rc=-19 split is back to ~2/3:1/3.
3. THEN test `bcm2711_pcie_resettleOutboundWindow()` called from
   `xhci_reset` between HCRST write and the bit-clear wait — that
   experiment hasn't actually been validated against a healthy
   bridge.
4. If still rc=-110, port FreeBSD bcm2838_xhci's firmware-load
   sequence (it explicitly waits for a "VL805 ready" status bit
   after the mailbox-notify before any other access).

## Don't-do list

- Don't add more retries — both the in-probe retry and the
  outer bring-up retry made things strictly worse in degraded
  bridge state.
- Don't extend the post-mailbox-notify wait past 50 ms; longer
  waits give start4.elf's periodic PCIe touches more chances to
  interfere (200 ms regressed to 3/3 rc=-19 in fresh-bridge
  testing).
- Don't call `bcm2711_pcie_initVL805` more than once per process
  lifetime — the host-bridge config-space mapping is leaked-by-
  design and a second mmap on the same region churns the bridge
  state.

## Status

Current code on `codex/upstream-sync-20260516`:
- 4 KiB BAR size match (commit `2ac680e`)
- HCH guard before HCRST (commit `b1ae732`)
- 50 ms post-reprogram settling (commit `0619a7f`)
- cap-probe in-loop retry (commit `4795266`)
- public `bcm2711_pcie_resettleOutboundWindow()` (commit `77fe93a`)

Hardware: locked in `rc=-19` (poison) regime; needs power-cycle to
clear before further iteration is productive.
