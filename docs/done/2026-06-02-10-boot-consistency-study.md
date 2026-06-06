# 10-boot consistency study (2026-06-02)

Goal (user request): add UART timestamps, run **10 boots on the unchanged image**
(no rebuild between boots), capture timestamped UART + HDMI, and systematically
compare logged-line content, per-phase timing, and HDMI frames — to settle
whether the Pi4 boot is non-deterministic at the base-OS level.

Image: current `agent/rpi4-program-reloc` build with the kernel **klog mirror**
(log/log.c writes each byte direct to console while no libklog reader is
draining) + 64 KB ring + libklog open-retry. NOT rebuilt across the 10 boots.

Tooling added this session:
- `scripts/capture-rpi4b-uart.sh --timestamp` (picocom | `stdbuf -oL ts`; fixed a
  block-buffering drop where `ts`'s writes to the log *file* were 4 KB-buffered
  and the tail was lost on watchdog kill).
- `scripts/boot-consistency-study.sh <count> [start]` — runs N netboot cycles,
  no rebuild, timestamped UART + periodic HDMI per boot.
- `scripts/compare-boots.py` — strips timestamp prefix + ANSI, then compares
  landmark offsets, stall structure, event counts, and HDMI md5s.

## Headline conclusion

**The base-OS early boot is DETERMINISTIC.** The original "boot logs long on some
boots, short on others / fbcon non-deterministic" symptom was a *measurement*
artifact of the broken klog→console drain, and the kernel mirror fixed it. With
clean timestamps:

- All 10 boots reach the **same landmarks** on UART (kernel banner → page alloc
  → vm done → scheduler → syscalls → primary-ready → syspage start → dummyfs →
  pcie settled → xhci → genet link up → `(psh)%`).
- Line counts cluster at **506–529 (±2 %)**.
- **HDMI final frame is byte-identical** (one md5 `f49c022d…`) across all 10 —
  but "identical" here means *consistent*, not *rich*. The frame (verified by
  eye) shows **only**:
  ```
  Phoenix-RTOS HDMI console
  fbcon: ok
  (psh)%
  ```
  i.e. pl011-tty's own banner + the psh prompt, on an otherwise-black screen.
  **None of the kernel boot log appears on HDMI, on any boot.** So HDMI is
  deterministic *at the floor*: the klog→fbcon drain never delivers, it just
  fails the same way every time now. The earlier random `a381f415…` vs this
  `f49c022d…` difference was the cursor/prompt position, not content.
- `dummyfs: initialized` = **exactly 2 every boot** → the long-suspected
  "duplicate dummyfs" is the **two expected instances** (dummyfs-root + devfs),
  NOT a bug.

The Phoenix-internal timeline is constant to <0.1 s. Every interval measured
from plo-start is identical regardless of boot:

| interval                       | boot01 | boot02 | boot06 | boot08 |
|--------------------------------|--------|--------|--------|--------|
| pcie_settled − plo_banner (s)  | 109.9  | 109.9  | 109.9  | 109.9  |
| genet_linkup − plo_banner (s)  | 116.7  | 116.7  | 116.7  | 116.7  |

## Where the boot-to-boot variance actually lives

1. **Pre-kernel firmware / DHCP / netboot timing (host + network side).** The
   only landmark spread (Δ ≈ 11.6 s, total span 165.9–177.5 s) is a *constant
   per-boot offset applied before plo even starts*. It is dominated by:
   - VideoCore firmware **`USB MSD stopped. Timeout: 25 seconds`** (~23 s every
     boot — firmware probing USB-boot before falling through to netboot), and
   - variable `LINK STATUS` / `DHCP src` settling (boots 06/08 add ~8 s here,
     and our own cycle sometimes needs a dnsmasq bounce).
   None of this is Phoenix.

2. **USB enumeration outcome (the genuine, already-parked hardware problem).**
   This is the only Phoenix-side metric that varies across identical boots:

   | metric        | 01 | 02 | 03 | 04 | 05 | 06 | 07 | 08 | 09 | 10 |
   |---------------|----|----|----|----|----|----|----|----|----|----|
   | xhci_timeout  | 2  | 0  | 0  | 0  | 2  | 3  | 2  | 2  | 2  | 0  |
   | enum_fail     | 1  | 1  | 0  | 0  | 1  | 1  | 1  | 1  | 1  | 0  |

   So 3/10 boots (03, 04, 10) showed no enumeration failure and 7/10 logged one.
   This is the flaky VL805/PCIe enumeration we already know about — confirmed
   here as the real boot-to-boot non-determinism, isolated to USB.

## Two real defects surfaced (both in the parked USB/PCIe path)

### A. ~10.8 s-per-register PCIe pre-init bridge dump dominates boot time
Every boot spends **~136–150 s "stalled"** in inter-line gaps ≥5 s, and almost
all of it is a diagnostic dump titled *"--- Pi-firmware bridge state (pre-Phoenix
init) ---"* that reads `RC_BAR1_LO, RC_BAR2_LO/HI, RC_BAR3_LO, UBUS_REMAP_LO,
MISC_STATUS, MEM_WIN0_LO, MEM_WIN0_BL, HARD_DEBUG, SYS_REV_CTRL` — **one register
per ~10.8 s**, all reading `0x00000000`. After the bridge is brought up the same
registers read instantly (`RC_BAR2 LO=0x11`). The 10.8 s is the PCIe
external-abort / SError recovery path firing on each pre-init config access
(matches `project_pi4_serror_pcie_source`). This is **diagnostic-only code**
(added under TD/USB-FIX-18) and is the single biggest boot-time cost — removing
it should cut ~90–110 s off every boot.

### B. Unlocked console → character-level log interleaving
Once userspace starts, the smp ticker, the usb/pcie daemon, and lwip all write
the UART through an **unlocked console**, producing pervasive mid-line garble
(e.g. `smp: tick+15s cpu 0 =R9C6_ BcApRu21_=L1O5`). It does not break boot, but
it corrupts the log and makes line-for-line diffing impossible by construction.
This is exactly the Phase-1 "serialize console writes through one lock" item.

## What this means for the user's thesis

The user's hypothesis was that a *fundamental base-system defect* (kernel / HAL /
plo / memory / SMP / early init / device creation) was the hidden cause behind
USB/SD/WiFi all resisting fixes. On the dimensions this study can measure — boot
*timeline*, UART log *content*, and HDMI final-*state* reproducibility — the boot
is now deterministic; the kernel boots the same way every time. (The mirror is a
pure-observability change and cannot alter kernel logic, so this is about what we
can now *see*, honestly, rather than a code fix to boot behaviour.)

But the user specifically named "console and fbcon do not bind deterministically."
That has NOT been fixed — it has been made *consistent*: the klog→fbcon delivery
path (libklog draining `/dev/kmsg` into pl011-tty) **still never attaches on any
boot** (its reader never registers — that is exactly why the kernel UART mirror
has to fire). So fbcon shows only its own banner + the psh prompt, every boot.
That is a **real residual defect**, now deterministic rather than random.

Net, the residual issues are:
1. pre-kernel firmware / DHCP / netboot timing (host + network side);
2. USB enumeration flakiness (the genuine boot-to-boot variance);
3. the klog→fbcon drain never attaching (HDMI carries no kernel log);
4. cleanup: the ~10.8 s/register PCIe pre-init diagnostic dump, and the unlocked
   multi-writer console garble.

Thread to pull later (parked): `usbpool_alloc` is 48 on boots 03 & 10 vs 40–41
elsewhere, and 03/10 are among the `enum_fail=0` boots — a hint the USB variance
correlates with DMA-pool allocation count. Not chased here.

## Suggested next steps (for the user to choose)
- **Quick win:** delete the pre-Phoenix-init bridge-state diagnostic dump
  (defect A) → ~90–110 s faster boots and far less SError-abort churn.
- **Correctness/readability:** serialize `hal_consolePrint`/`hal_consolePutch`
  under one lock (defect B) so multi-writer UART stops garbling.
- USB enumeration flakiness (the real variance) remains the substantive open
  problem and is tracked under the USB re-architecture work.
