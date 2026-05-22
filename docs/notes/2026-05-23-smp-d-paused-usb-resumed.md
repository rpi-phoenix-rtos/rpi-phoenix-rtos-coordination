# 2026-05-23 — SMP Phase D paused, USB resumed

## SMP Phase D investigation — paused

### Hardware ground truth (multi-run UART-marker capture)

Multi-byte unique markers (`<Z<digit>`, `>Y<digit>`, `?Q<digit>`) in
the armstub `in_el2`, armstub `secondary_spin` post-WFE, and plo
`secondary_smoke_entry` paths. Single-byte markers from earlier
rounds were swamped by firmware/kernel printf noise that happened to
re-use the same letters — useless signal. With unique 3-byte
patterns, the result across 3 cold-boot cycles was:

| Run | Markers seen                    |
|-----|----------------------------------|
| 1   | `<Z3` (1×)                       |
| 2   | none                             |
| 3   | `<Z1` (1×)                       |

`<Z0` (cpu0 entering armstub `in_el2`) **never fired in any run**.
Combined with the fact that cpu0 must be running (boot reaches psh
prompt), this means the **Pi 4 firmware boots cpu0 directly to the
kernel-image entry point (skipping the armstub)**, while
intermittently delivering one of cpu1/cpu2/cpu3 into the armstub.

The standard armstub spin-table protocol (plo writes the kernel
entry PA to `spin_cpu1/2/3` + `sev`, secondaries wake from their
`wfe` and `br x4`) cannot be relied on as the primary SMP-bring-up
mechanism on Pi 4 without firmware cooperation.

### Kernel-side machinery preserved

The Phase D kernel/plo/armstub work is preserved on the agent branch:
- per-CPU stacks (`_set_up_vbar_and_stacks` indexes by `cpu_id`),
- SEV after `nCpusStarted` bump in `_hal_cpuInit` (generic.c),
- banked-PPI enable in `_hal_interruptsInitPerCPU` (interrupts_gicv2.c),
- per-CPU CNTV arm in `_hal_timerInitPerCPU` (gtimer_timer.c),
- per-CPU progress + tick counters dumped via direct UART
  (`hal_consolePrint`, bypassing the klog buffer),
- x9 reload trampoline at `el1_entry` (kernel _init.S commit
  `979e05c0`) + plo publishing syspage PA at PA 0xD8 (plo commit
  `750b7fd`),
- `_other_core_virtual` parks secondaries in DAIF-masked WFI when
  `NUM_CPUS==1` so the assembly-side cbnz can't route them into C
  helpers that would race primary's setup.

`NUM_CPUS` is set back to **1** in `hal/aarch64/generic/config.h`
(kernel commit `be058044`). Boot is stable at psh prompt with `ps`
working. K120 keyboard visible to firmware (`VID 046d PID c31c`).

### Open question for the next SMP push

Need a wakeup mechanism that targets cpu1/2/3 specifically without
relying on the armstub spin-table:
- **PSCI CPU_ON**: would need an EL3 secure monitor in
  phoenix-armstub8-rpi4.S that traps `smc` and powers up the
  requested core. ATF and Linux already implement this pattern for
  BCM2711.
- **BCM2711 ARM_LOCAL_MAILBOX**: per-core start-address registers
  in the BCM2711 ARM_LOCAL block at PA 0x4c000000. Simpler than
  PSCI but Pi-specific.
- **Pre-firmware spin loop**: have the armstub place
  cpu1/2/3 into a known WFE loop at first entry and keep them
  there; rely on the firmware to start them once at boot rather
  than on the every-boot reproducibility we don't currently have.

## USB resumed

### XHCI_MAP_SIZE 64 KiB → 4 KiB

VL805 BAR0 is 4 KiB (FreeBSD bcm2838_xhci / Linux xhci_pci_setup /
Circle USBStandardHub / NetBSD xhci_pci_attach all walk this size
from PCI cfg and arrive at 4 KiB). Mapping 64 KiB spills 60 KiB
past the BAR into unmapped PCIe outbound territory; BCM2711 returns
0xdeaddead and the BCM2711 SError absorbs the abort, surfacing as
our `usb-hcd: ops->init fail rc=-19`.

phoenix-rtos-devices commit `2ac680e` reduces `XHCI_MAP_SIZE` in
`usb/xhci/xhci.c` and `BCM2711_PCIE_XHCI_MMIO_SIZE` in
`usb/xhci/bcm2711-pcie.c` from `0x10000` to `0x1000`.

Hardware result before: `usb-hcd: ops->init fail rc=-19` (poison).
Hardware result after:  `usb-hcd: ops->init fail rc=-110` (timeout)
on most boots, `rc=-19` on ~1/3 of boots. The error code change
is the expected step-down: rc=-110 means the cap-space reads
succeed and `xhci_reset`'s wait for `HCRST` to clear is the
timeout site.

### HCH guard before HCRST

phoenix-rtos-devices commit `b1ae732`: read `USBSTS.HCH` at entry
to `xhci_reset`; if the controller isn't already halted, clear
`USBCMD.R/S` and wait for `HCH=1` before writing `HCRST`. xHCI
spec 5.4.1 says writing HCRST while HCH=0 leaves the controller
in undefined state — our `rc=-110` symptom matches "HCRST bit
that never clears". Linux and FreeBSD both gate HCRST on HCH=1.

### Bridge settling window

phoenix-rtos-devices commit `0619a7f`: 50 ms `usleep` after
`bcm2711SetOutboundWindow0` in `bcm2711_pcie_initVL805`. The
mailbox notify only acknowledges that VC firmware accepted the
xhci-reset request, not that the VL805 firmware reload has
completed — the bridge translation needs time to settle before
xhci's cap-space reads. Empirical sweep:

| Wait                | rc=-19 rate | rc=-110 rate |
|---------------------|-------------|--------------|
| no wait             | 1/3         | 2/3          |
| 50 ms before reprog | 3/4         | 1/4          |
| 50 ms after reprog  | 1/3         | 2/3          |

Post-reprogram wait keeps rc=-110 as the dominant failure
(controller reachable but reset times out — clearer next target).

### Next USB step

The remaining `rc=-110` means xhci's `HCRST` write doesn't clear
within `XHCI_HCRST_TIMEOUT_MS=20u`. Increasing the timeout to 1 s
did NOT clear the bit — VL805 isn't actually completing the reset
on its side after `HCRST` is written. Likely root causes:

1. VL805 firmware-load completion isn't fully synchronized with
   our re-program-then-read sequence — even with the 50 ms wait,
   sometimes the controller still isn't loaded by the time HCRST
   is issued.
2. The BCM2711 outbound-window translation gets invalidated
   *again* by HCRST-driven activity, the same way it was
   invalidated by the original mailbox notify; we'd need to
   re-program it once more between mailbox notify and HCRST.
3. The VL805 isn't a fully-spec-compliant xHCI on the HCRST
   handshake — empirically Circle waits ~100 ms post-firmware
   load before any operational register access; we could try
   the same.

Next iteration plan:
- Add a `bcm2711SetOutboundWindow0` call AGAIN inside `xhci_reset`
  immediately after the HCRST write, before the bit-clear wait.
- Try a longer post-firmware-notify wait (200 ms, mirroring
  Circle's USBStandardHub init path).
- If neither helps, port the explicit VL805 firmware-load
  sequence from FreeBSD `bcm2838_xhci_attach`.

## Status snapshot

- SMP: paused at NUM_CPUS=1. Documented and committed.
- USB: rc=-19 → rc=-110 (~2/3 of boots); chain to next-step
  `xhci_reset HCRST handshake` clearly identified.
- Boot stability: 3/3 cold cycles reach psh prompt, `ps`
  works, no fault patterns.
