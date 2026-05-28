# 2026-05-28 — USB PoC source-side exhaustion + code-quality session

This session worked the Pi 4 USB PoC (host the Phoenix USB host stack
inside the lwip-port process) through many single-variable
investigations and adjacent cleanup. The PoC remains non-functional at
the end of the session, but the failure mode is now well-characterized
as either silicon-bounded or living in territory beyond source-code
comparison with the working `X` diag rig.

## Starting state (handoff from prior sessions)

- Boot-time `usb` daemon did its own bridge bring-up + xhci_init,
  failed partway, exited. This left the VL805 with a stale internal
  CRCR latch — observed at the start of this session as a Command
  Completion Event whose `parameter` field referenced the daemon's
  cmd-ring PA (0x032f5000) instead of the lwip-port-embedded's
  (0x032fc000).
- `X` diag rig in `port/diag-udp.c` PROVEN to drive a USB hub through
  No-Op → Enable Slot → Address Device → Get Descriptor cc=1 from
  the lwip-port process.
- PoC integration (real `usb_init` from lwip-port worker thread)
  failing with `rc=-110 / first event @idx -1` every cycle.

## Substantive runtime fixes delivered

### Bridge bring-up / controller drive split (BRIDGE_ONLY)

Root cause for the stale-CRCR class: HCRST in lwip-port-embedded was
not reliably clearing the VL805's internal cmd-ring pointer latch.
Fix: split bridge bring-up from controller drive — boot-time
`usb;--bridge-only` daemon does only `bcm2711_pcie_initVL805` and
parks (sleep 3600), never touches the xHCI controller. lwip-port-
embedded then sees a fresh power-on controller and its CRCR write
lands cleanly.

Commits (across 4 sibling repos):
- `phoenix-rtos-devices` `28bc814`: `xhci/bcm2711: BRIDGE_ONLY mode`
- `phoenix-rtos-devices` `600081d`: park instead of `_exit(0)` to
  preserve the leaked bridge config mapping
- `phoenix-rtos-usb` `29f8cea`: parse `--bridge-only` argv
- `phoenix-rtos-project` `cfe8aec`: syspage launches `usb;--bridge-only`
- `phoenix-rtos-lwip` `77f31bb`: worker uses DRIVE_ONLY
- coordination `bcd6297`: status doc

Effect on the failure signature: ERDP readback at cmdExec timeout
now matches `eventRingPhys` exactly (was off by ~7 KB before the
split — `parm=0x032f5000` ≠ `cmd_pa_lo=0x032fc000`). The whole
stale-pointer class is gone; the residual `@idx -1` failure mode is
clearly a different bug.

Memory: `pi4-xhci-crcr-stale-after-hcrst.md`.

## Hypotheses tested and ruled out (PoC stayed 0/N every time)

1. `usb_allocUncached` flags (added `MAP_PRIVATE | MAP_CONTIGUOUS`
   to match the rig and the kernel `drivers/physmmap.c::dmammap`
   helper). Kept the flags as code hygiene.
2. `xhci_reset` skipping `bcm2711_pcie_resettleOutboundWindow`. Negative.
3. `bcm2711_pcie_initVL805` DRIVE_ONLY path: skip the leaked bridge
   probe mmap entirely. Negative. The leaked mmap is the original
   workaround for `munmap-invalidates-translation`; restored.
4. `CONFIG.MaxSlotsEn = 1` (rig writes 1, xhci_init wrote `nslots=32`).
   Negative — also broke `programCommandSpace`'s register-readback
   validation, which had to be relaxed. Reverted.
5. Contiguous scratchpad: allocate the 31 scratchpad buffers as ONE
   `n × PAGE_SIZE` block instead of 31 separate allocs. Negative.
6. `bcm2711_pcie_initVL805` parks the BRIDGE_ONLY daemon (not `_exit`)
   to preserve the leaked bridge mapping past process teardown.
   Kept (correct preservation of the documented mapping leak).
7. `programCommandSpace` register-write order: match the rig's
   CONFIG-first then LO-then-HI-for-DCBAAP-and-CRCR. Negative on
   the symptom but kept as code hygiene.
8. Removed `xhci_cmdExec`'s FRESH-uncached event-ring re-read (the
   disproved cache-aliasing hypothesis). -25 lines.
9. `bcm2711_pcie_initVL805` DRIVE_ONLY now explicitly returns
   `-ENODEV` if `PCIE_DL_ACTIVE | PHYLINKUP` doesn't come up in 10 s
   (was silently returning EOK and letting xhci flail on a dead bus).
10. Embed worker called the rig directly (A/B test in same process):
    rig STILL succeeded 2/4 times with the new BRIDGE_ONLY split,
    while real `usb_init` continues 0/4. Confirms the bug is NOT
    in the split.
11. xhci_init restructured: allocate every DMA buffer up-front
    BEFORE any MMIO writes (matches rig's pattern). Negative on
    the symptom but kept for cleaner allocate-then-program separation.

Multi-trial bench totals across all variants: **PoC 0/21 trials**,
rig 2/4 trials.

## External-research validation

Both major walls have known hardware-level corollaries in mainline
Linux. Saved as `docs/known-limits.md` with citations:

- **USB VL805**: Linux issue #5060, RPi forums #380969 — per-board
  silicon variability; some Pi 4s need an RC delay between 3.3V and
  nPONRST. Mainline Linux also hits VL805 reliability issues.
- **WiFi BCM43455**: OpenWrt #23069 — HT Avail timeout on firmware
  boot, software recovery FAILS, only PSU power-cycle restores.

Our observed ~50 % rig flakiness almost certainly matches the
silicon-side variability documented above. The PoC's distinct 0 %
is a SEPARATE Phoenix-specific bug on top of the silicon issue.

## Code-quality wins delivered

- `docs/known-limits.md` (NEW) — Phoenix-internal-fix vs hardware-
  bounded distinction.
- `docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md` tracking-checklist
  synced: TD-01/TD-11/TD-13 marked RESOLVED matching body sections.
- `scripts/test-cycle-bench.sh` (NEW) — multi-trial pass-rate harness
  delegating to `uart-summary.sh`.
- `scripts/uart-summary.sh` — added net-stage checks
  (lwip started / genet link up / netif has IP).
- `_init.S` — dropped 55 lines of resolved TD-16-cache-enable
  iteration log + TODO tag from a still-useful helper.
- `xhci_cmdExec` — dropped 25 lines of FRESH-uncached disproved-
  hypothesis instrumentation.
- `port/main.c` — dropped dead diag-rig declarations.
- CLAUDE.md — documents `test-cycle-bench.sh` + `diag-udp-probe.sh`.

## Memories added or updated

- `pi4-xhci-crcr-stale-after-hcrst.md` (NEW) — root cause for the
  stale-CRCR class, now fixed.
- `vl805-known-silicon-flakiness.md` (NEW) — Linux #5060 + RPi-
  forums citations for the hardware-level VL805 reliability issue.
- `usb-dma-write-loss.md` — rig-is-flaky correction added.

## What's NOT possible from inside the loop

The remaining serious paths to crack the residual PoC bug all sit
outside the autonomous-loop's scope:

- Kernel-side instrumentation (`printk` in `pmap.c` during
  MAP_CONTIGUOUS, trace which PAs the allocator hands out).
- JTAG-attached VL805 internal state capture during PoC failure
  vs rig success runs.
- Upstream Phoenix-RTOS maintainer input on whether something in
  the lwip-port process state perturbs VL805 inbound DMA writes.

## Status of the four pending tasks tagged into this session

- USB FIX-7 (#70 skip HCRST if controller already usable) — still
  pending; not worth chasing while the inbound-write asymmetry is
  unresolved.
- USB FIX-14 (#78 VL805 inbound DMA write asymmetry) — well
  characterized as silicon+Phoenix-internal; documented in known-
  limits.md.
- USB FIX-17 (#80 MSI/HARD_DEBUG/chip-side bus-master) — already
  refuted in earlier session (Circle uses INTA, our config matches).
- USB #99 (flaky bridge state pre USBSTS=0x00) — same root cause
  as VL805 silicon flakiness; documented in known-limits.md.
- WiFi #91 (firmware-execution gate) — silicon cold-reset
  limitation, documented in known-limits.md.

## Next session's options

1. **Try a fresh angle on USB** — kernel-side `printk` instrumentation
   in the kernel allocator to compare PAs handed to lwip-port's
   usb_init vs the rig. Substantial kernel work.
2. **TD-Eth-DHCP** — `dhcp_start` returns ERR_OK from
   `genet_dhcpStartCb` (verified this session). Full exchange
   unverified due to test-cycle capture-window cutoffs. Unblock by
   teaching `test-cycle-netboot.sh` to keep the Pi powered past the
   capture window long enough for a manual `nc -u 10.42.0.99 9999`
   probe via diag-udp.
3. **SMP soak** — the 'b' burn command in diag-udp already exists.
   Could add long-running variants or a host-side stress-test runner.
4. **TD-10 SError handler** — substantial kernel work; would close
   a real observability gap.
5. **Pivot to a new feature initiative** (SD storage, real persistent
   FS, BT bring-up) — requires user direction.
