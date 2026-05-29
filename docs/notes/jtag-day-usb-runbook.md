# JTAG-day runbook — Pi 4 USB inbound-DMA-write loss

When the JTAG/SWD probe arrives, this is the ready-to-execute plan to
root-cause the USB failure. JTAG is the real unlock: it lets us watch the
VL805 inbound DMA write and the BCM2711 RC inbound path directly, which
no UART/UDP method could (see `2026-05-29-usb-reanalysis.md` for why every
software-only observability path failed).

## The failure (reliable repro)

Boot the normal image (embedded USB host in lwip-port, DRIVE_ONLY). At
~lwip+13 s the controller is brought up and the first command (NOOP) is
issued. Reliable UART signature via the kernel `debug()` channel:

```
xhci_reset: enter USBSTS=0x00000001
xhci_reset: post-HCRST USBSTS=0x00000011
xhci_cmdExec TIMEOUT USBSTS=0x00000010 USBCMD=0x0000000d CRCR=0x00000008
  ev[0] parm=0x0 st=0xdeadbeef ctrl=0xffff0000   <- event ring still pre-init sentinel
  ring-scan: first event @idx -1                 <- NO event ever written
usb-hcd: ops->init fail rc=-110
```

Interpretation: controller RUNS (USBSTS R/S, `CRCR` CRR=1 ⇒ it fetched the
command ring = inbound **read** works), but **zero events are
DMA-written** to the event ring (inbound **write** lost). `0xdeadbeef` is
our pre-init sentinel — the device never overwrote it.

## Key addresses (from captured logs; reconfirm live — PAs vary per boot)

- **BCM2711 PCIe RC host window:** `0xfd500000` (`PCIE_BCM2711_HOST_BASE`).
  - `MISC_CTRL` `+0x4008`, `MISC_STATUS` `+0x4068` (link: `0xb0`=DL_ACTIVE|PHYLINKUP).
  - `RC_BAR2_LO/HI` (inbound window), `UBUS_BAR2_REMAP`.
  - **Outbound error-status block `+0x6004..0x6020`** (Linux pcie-brcmstb
    dumps these: VALID / CFG-vs-MEM / R-W / offending address / cause).
    diag-udp `'P'` command reads these over UDP if the network is up.
- **VL805 xHCI MMIO (BAR0):** CPU PA `0x600000000` (outbound window) →
  PCIe `0xf8000000` → VL805 BAR0. CAPLENGTH=0x20, HCIVERSION=0x0100.
  USBSTS/USBCMD/CRCR/ERDP/ERSTBA live here.
- **DMA rings (low DRAM, ~32–54 MB, reconfirm at runtime):** event ring
  `evRingPhys` (e.g. `0x032f9000`), cmd ring (e.g. `0x032a0000`), DCBAA /
  scratchpad (e.g. `0x032a1000`). The driver prints all of these at the
  cmdExec timeout — read them from that log line for the live boot.

## Procedure (in priority order)

1. **Hardware watchpoint on the event-ring physical page** (`evRingPhys`,
   read it from the timeout dump). Re-run the bring-up.
   - Watchpoint **never fires** ⇒ the VL805's inbound write never reaches
     DRAM. The write is being dropped upstream (RC/SCB inbound decode).
     → go to step 2.
   - Watchpoint **fires** (write lands) but the polled CPU read still sees
     `0xdeadbeef` ⇒ coherency/visibility (unlikely — rings are MAP_UNCACHED;
     would contradict prior evidence, but JTAG settles it).
   - Watchpoint fires at a **different** address than `evRingPhys` ⇒
     inbound address-translation offset (RC_BAR2 / dma-ranges).
2. **Read the RC outbound error regs `0xfd500000+0x6004..0x6020` via JTAG**
   at the timeout. VALID=1 ⇒ exact offending address + CFG/MEM + cause
   (SLVERR/DECERR/UR/timeout) — classify the abort that also raises the
   masked SError (see TD-10 / `pi4-serror-pcie-source`).
3. **Inspect RC inbound decode live:** dump `MISC_CTRL` (SCB_ACCESS_EN,
   RCB_*_MODE, **SCB0_SIZE** = enc 17 = 4 GB), `RC_BAR2_LO/HI` (sz=0x11),
   `UBUS_BAR2_REMAP` (EN=1). Compare **byte-for-byte against Linux's 3 GB
   model** (`dma-ranges`=0xc0000000; Linux derives SCB sizes from it). The
   event ring at ~51 MB is inside both 3 GB and 4 GB apertures, so this is
   a secondary suspect — but a wrong SCB-size/aperture could still mis-route
   inbound writes (Codex's lead).
4. **Correlate the SError:** build with SError unmasked (remove `NO_SERR`,
   see TD-10), reproduce, and at the abort read `ESR_EL1` + `DISR_EL1` /
   `VDISR_EL1` via JTAG to get the precise (vs the masked, imprecise) abort
   syndrome and the access that raised it.

## Already ruled out — do NOT re-test on JTAG day

scratchpad present + DCBAA[0]; ERSTBA-written-last order; IMAN/IMOD;
USBCMD INTE|HSEE; MMIO attribute (nGnRnE); MAP_CONTIGUOUS; PA region
(same ~51 MB as working rig); va2pa correctness (fresh MAP_PHYSMEM agrees);
bridge re-settle (FIX-3/19); bridge-mapping presence (H-map); MSI (VL805
uses legacy INTA; events are DMA-written regardless); cache-alias (no
permanent cacheable kernel map of the ring page); register write order
(matched to rig); VL805 firmware (loaded, `fw_ver@0x50`≠0, mailbox
correctly skipped); MaxSlotsEn; **concurrent GENET DMA** (sustained TX
flood confirmed active → still @idx -1, 2026-05-29). The diag rig 'X'
succeeds ~50% doing the *same* register sequence in the *same* process —
its intermittency is most likely bridge flakiness, not a code difference.

## Likely outcomes → fixes

- **RC/SCB inbound decode rejects the write** (step 2 VALID + step 1
  no-fire): fix RC inbound config (RC_BAR2/SCB_SIZE/UBUS to the correct
  3 GB model), re-test.
- **Write lands, controller-side ring/cycle bug** (step 1 fires): then the
  non-spec event-ring engine in `xhci.c` (cycle state never toggled, ring
  memset/re-read at index 0 — see usb-reanalysis Bug #1) becomes the real
  target; rewrite it as a proper producer/consumer once events land.
- **No abort, no write** (step 1 no-fire, step 2 clear): VL805-side — the
  controller isn't issuing the write; inspect doorbell/CRCR/cmd-TRB cycle
  bit live, and the VL805's own bus-master/DMA enable.
