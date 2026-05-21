# Codex consultation: BCM2711 PCIe outbound-window 0xdead poison

Date: 2026-05-21
Question: why does the outbound translation seemingly age out between
pcie scan callback and xhci_init register reads?

## Empirical findings handed to Codex

1. Reading PCI_VENDOR_ID at bus=1, dev=1, fn=0 (empty slot past VL805)
   immediately tears down the outbound window. Fixed by capping the
   per-bus device sweep at 1 on every bus.
2. `cfgio.destroy()`'s munmap of the bridge config window (0xfd500000)
   also tears down the outbound window. Worked around by holding that
   mapping until process exit.
3. After the VL805 firmware-reset mailbox call returns, re-running
   `bcm2711SetOutboundWindow0()` is NOT enough — xhci's BAR0 reads
   still come back as 0xdead.
4. Inside the scan callback, a 4 KiB PROT_READ mmap of CPU PA
   0x600000000 reads valid 0x01000020. A 64 KiB PROT_RW mmap of the
   same PA, used later by xhci_init, often does not.

## Codex's ranked mechanisms (verbatim)

1. **Phoenix pmap / VM attribute aliasing bug** (most likely).
   PCIe outbound windows don't "arm" based on userspace PROT_WRITE;
   the CPU MMU does. Different PTE attributes on two mappings of the
   same PA could trigger this. **Diag**: map the VL805 BAR ONCE,
   early, with the final intended attributes (64 KiB, PROT_RW,
   MAP_DEVICE | MAP_PHYSMEM). Never create the 4 KiB alias. If
   failures disappear, it's a kernel pmap bug. We're now mostly doing
   this — single mmap inside scan callback handed off to xhci_init —
   but rc=-19 persists, so either the bug is subtler or one of the
   other mechanisms also applies.

2. **BCM2711 RC register mapping lifetime tied to outbound state in
   the kernel**. `munmap()` of the RC config aperture should not
   clear `MISC_CPU_2_PCIE_MEM_WIN0`; that it does on Phoenix points
   to teardown code doing more than removing a user VA. **Diag**:
   after `cfgio.destroy()` and after the mailbox reset, read back the
   RC registers through a permanently mapped RC window —
   WIN0_LO/HI, limit/base, BAR2, link status, inbound/outbound
   control. If they still hold the right values while BAR reads
   poison, it's CPU-side VM/TLB. If they changed, the bridge state
   was disturbed.

3. **Mailbox VL805 reset perturbs more than just WIN0**. The
   firmware reload may also reset VL805 BAR0, MEM-enable, bus-master,
   link LTSSM, or the RC bridge memory aperture. Re-running only
   `SetOutboundWindow0()` may not suffice. **Diag**: log the full
   set of PCIe registers immediately after notify, then reapply the
   complete `pcie_cfgInitBcm2711` + scanFunc-without-notify sequence
   instead of just WIN0.

Codex bets against per-master/cacheability tracking in the BCM2711
RC. The PROT_WRITE difference is much more likely changing ARM
page-table attributes than changing PCIe outbound decode behavior.

## Next concrete diagnostic to run

Implement Codex's diag for mechanism #2: after each suspected
invalidation point (notify return, cfgio.destroy if re-enabled),
read back `MISC_CPU_2_PCIE_MEM_WIN0_LO/HI` (offsets 0x400c/0x4010 in
the bridge config window) and log them. That tells us whether the
bridge state is intact (→ CPU-side pmap bug) or actually disturbed
(→ need to redo bridge programming after each disturb).
