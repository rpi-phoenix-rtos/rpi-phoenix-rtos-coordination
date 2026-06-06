# Linux-on-Pi4 reference boot — inbound DMA window offset [REFUTED as our cause]

> **⛔ REFUTED ON HARDWARE (2026-05-30, later same day).** The fix proposed
> below (program `RC_BAR2` inbound base to bus `0x4_00000000` + offset every
> xHCI DMA descriptor by `+0x4_00000000` to match Linux's `dma-ranges`) was
> implemented and tested on the 4 GB board. Result: **byte-identical failure**
> — `first event @idx -1`, event ring still all `st=0xdeadbeef` sentinels,
> `rc=-110`. The register readback confirmed the change took effect
> (`RC_BAR2 LO=0x11 HI=0x4`, `cmd_phys=0x4033ae000`, `evRingPhys=0x4033bc000`,
> all offset), yet nothing changed. **The inbound-window base was never the
> discriminating variable.** The change was reverted (it would also have
> *broken* the diag rig, whose descriptors in `diag-udp.c` still use raw PA).
>
> **Why the headline was wrong:** the diag rig succeeds ~50% on the *exact
> same* RC inbound config (both rig and PoC run in the lwip-port process and
> inherit the boot daemon's `RC_BAR2`/`SCB0_SIZE_4G`; rings sit ~51 MB, inside
> both the bus-0 and bus-4G apertures). A working reference on the same config
> proves inbound writes *do* traverse it — so `RC_BAR2`/`SCB_SIZE`/3 GB-vs-4 GB
> /`UBUS_REMAP` cannot be the *differential* cause. (Independent Codex audit
> 2026-05-30 reached the same conclusion from the code.)
>
> **What this leaves:** the USB failure is a **code-path bug** between the rig
> (~50%, enumerates) and the PoC (`usb_init`, 0%, first No-Op never completes)
> — proven by the same-boot A/B (in-process embed worker ran the rig right
> after `usb_init` failed in the *same* boot; rig still succeeded 2/4 — see
> `docs/done/2026-05-28-usb-poc-exhaustion-session.md` item 10). NOT silicon,
> NOT cache (fresh-uncached re-read agreed), NOT the inbound window. Next:
> attack the rig-vs-PoC code-path differences (Codex composite / byte-level
> first-doorbell state diff). The secondary findings below (board is good;
> `0x6004–0x6020` fault) remain valid.

**2026-05-30.** Per the user's idea, we netbooted a stock Linux (RPi kernel
6.18.33-v8+ + a busybox initramfs, built out-of-tree under
`/home/houp/pi4-linux-diag/`) on the **same** test Pi 4B that Phoenix runs
on, to use a working USB stack as a reference oracle. Full UART capture:
`artifacts/rpi4b-uart/2026-05-30-linux-usb-ref-fullcap.txt`.

## Headline finding (strong root-cause candidate, overturns a prior conclusion)

Linux's `brcm-pcie` prints its PCIe RC address windows at boot:

```
brcm-pcie fd500000.pcie: host bridge /scb/pcie@7d500000 ranges:
brcm-pcie fd500000.pcie:      MEM 0x0600000000..0x063fffffff -> 0x00c0000000
brcm-pcie fd500000.pcie:   IB MEM 0x0000000000..0x00bfffffff -> 0x0400000000
brcm-pcie fd500000.pcie: link up, 5.0 GT/s PCIe x1 (SSC)
pci 0000:01:00.0: [1106:3483] type 00 class 0x0c0330 PCIe Endpoint   (VL805)
xhci_hcd 0000:01:00.0: new USB bus registered ...
usb 1-1: New USB device found, idVendor=2109, idProduct=3431          (VIA hub — WORKS)
```

The format is `<cpu_start>..<cpu_end> -> <pci_addr>`. So:

- **Outbound** (CPU → controller MMIO): CPU `0x6_00000000` → PCI `0xc0000000`.
  Phoenix's outbound works (it reads xHCI regs, CRR=1), so this side is fine.
- **Inbound DMA** (controller → DRAM): **CPU `0x0..0xbfffffff` (3 GiB) maps
  to PCI bus address `0x4_00000000`.** i.e. for the VL805 to DMA-write ARM
  physical address `X`, it must be programmed with bus address
  **`X + 0x4_00000000`** (the `dma-ranges` translation). The inbound window
  is **NOT identity** and is **NOT at bus 0**.

### Why this is very likely the Phoenix USB inbound-write-loss

- Phoenix programs the xHCI DMA descriptors (DCBAA, ERSTBA, ERDP, command
  ring, transfer rings, scratchpad pointers) with **`va2pa()` = the raw ARM
  PA** (identity, no offset), and sets `RC_BAR2` to map **PCI bus 0 → CPU 0**
  (`bcm2711SetRcBar2(bcm, 0, 0x100000000)`).
- So the VL805 issues inbound writes to bus address = `ARM_PA` (e.g. the
  event ring at `~0x032f9000`). Linux's working inbound window only decodes
  bus `0x4_00000000..0x4_bfffffff`; bus `0x032f9000` is **below** it. On the
  real RC the bus-0 inbound window Phoenix programs evidently does not decode
  these transactions (the hardware wants the window at `0x4_00000000`, as the
  Pi firmware and Linux both use) → the writes are dropped → polled event
  ring stays at the pre-init sentinel → `first event @idx -1` / `rc=-110`.
- This **overturns** the earlier investigation's conclusion (recorded in
  memory `usb-dma-write-loss` and elsewhere) that "RC_BAR2 identity is
  correct and `va2pa` is verified correct for the failing buffers." That was
  never checked against Linux's *actual* inbound mapping; the Linux reference
  shows the real mapping is offset by `+0x4_00000000`. It is exactly the
  ARM↔bus translation that TD-15 phase 4/6 flagged and that the
  `dtb_armToBus` helper was meant for — but for the **PCIe** path, not GENET.
  (GENET is a native SCB master and correctly uses identity `va2pa`; that is
  why GENET works and masked this — see the AGENTS.md / TD-15 GENET hazard.)

## The fix to test (no JTAG; testable on the 4 GiB board)

Match Linux's working inbound model in the BCM2711/VL805 bring-up:
1. Program `RC_BAR2` inbound window as **PCI bus `0x4_00000000` → CPU `0x0`,
   size 3 GiB** (matching Linux's `dma-ranges`), instead of bus-0 / 4 GiB.
2. Program **every xHCI DMA address the controller dereferences** (DCBAA,
   DCBAA entries, scratchpad array + buffers, CRCR/command ring, ERSTBA, ERST
   seg base, ERDP, EP transfer rings, input/output device contexts, and any
   data-stage buffers) as **`ARM_PA + 0x4_00000000`**, not raw `ARM_PA`.
   Centralize as a helper, e.g. `xhci_dmaAddr(pa) = pa + PCIE_INBOUND_OFFSET`.
3. Leave the **outbound** path (BAR0 MMIO via CPU `0x6_00000000`) unchanged —
   it works.
4. Do NOT touch GENET addressing (native SCB, identity is correct).

Validate by: build → netboot Phoenix → does `usb_init` now post an event
(`@idx >= 0`) / enumerate? This is a direct, falsifiable hardware test.

Open nuance to keep honest: the diag-rig's historical ~50% "successes" used
the same identity addressing, which this model says should never post events
— so either those successes were misattributed/confounded (the forensic
re-analysis already rated that data shaky) or the firmware-left inbound
window (at `0x4_00000000`) was sometimes not fully overwritten by Phoenix's
bus-0 reprogram. The hardware test above resolves it either way.

## Secondary findings from the Linux boot

- **Board is GOOD (reinforces the permanent rule):** Linux fully enumerates
  USB on this board, and the Pi firmware's own USB stack also enumerates the
  hub (`DEV [01:00] ... VID 2109 PID 3431`, `HUB init`) during boot. Not a
  hardware fault.
- **`devmem 0xfd506000` (the assumed RC "error-status" 0x6004..0x6020 block)
  panicked Linux** with `SError Interrupt ... code 0xbf000002` — the SAME
  async external-abort SError Phoenix takes. So those offsets are not safe
  readable registers on this RC; the RC-error-register read approach (diag-udp
  'P', the JTAG-runbook step) is a dead end as written — reading them aborts.
- **MSI:** the device has `msi_irqs` populated (Linux uses MSI here), but this
  is not the inbound-write issue (events are DMA-written regardless).
- Linux uses **SWIOTLB** (`software IO TLB mapped 64MB`) for the >3 GiB /
  bounce case; not needed for our low-DRAM rings but confirms the dma-ranges
  model.

## Tooling notes (for repeating the Linux reference boot)

- Out-of-tree Linux netboot tree: `/home/houp/pi4-linux-diag/tftp/`
  (stock `start4.elf`/`fixup4.dat`/`kernel8.img`/`bcm2711-rpi-4-b.dtb` from
  raspberrypi/firmware; `config.txt` with `dtoverlay=disable-bt` so
  `ttyAMA0` lands on the captured GPIO14/15 UART; busybox-aarch64 initramfs
  with a diagnostic `/init`).
- Serve it by pointing the netboot server's TFTP root at it:
  `RPI4B_NETBOOT_TFTPROOT=/home/houp/pi4-linux-diag/tftp ./scripts/netboot-server-up.sh`
  then `pi_power_on` + `capture-rpi4b-uart.sh`. Restore Phoenix by bringing
  the server back up without that env (default Phoenix bootfs).
- `capture-rpi4b-uart.sh` has a bug (line 382/422 `[: integer expected`) that
  prevented it writing the picocom logfile for this run; the real output was
  the redirected stdout, preserved at the path above. Worth fixing later.
