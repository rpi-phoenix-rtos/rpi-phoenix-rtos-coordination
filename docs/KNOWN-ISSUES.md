# Known issues and limitations

Open items in the Phoenix-RTOS Raspberry Pi 4 port, as of 2026-08-27. This is
the user-facing summary; the exhaustive engineering registries are:

- [docs/inprogress/pi4-hardware-support-matrix.md](inprogress/pi4-hardware-support-matrix.md)
  — per-peripheral status with evidence.
- [docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md](inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
  — the `TD-NN` transitional-debt registry (its bottom "Tracking Checklist" is
  authoritative; most `TD` items are already resolved).

Resolved items are removed from this file as they are fixed, so it shrinks over
time; the git history keeps the record of what was fixed.

## Application bugs

These affect the showcase apps, not the base system.

| ID | Symptom | Status / workaround |
|---|---|---|
| #64 | SD-side filesystem stack pressure under load (deep fs call chains). | Open. |
| #66 | Stale `/tmp/.X0-lock` prevents the X server restarting after an unclean exit. | Open; remove `/tmp/.X0-lock` before relaunching the X server. |
| vkQuake-input | **vkQuake keyboard/mouse input is not wired** — events don't reach the game, so it renders but is not interactive. An intermittent V3D GPU binner wedge is also under investigation. | Open (input is owner-attended). |

## System-level limitations

| Area | Limitation | Status |
|---|---|---|
| Board portability (TD-06) | The DTB parser assumes a single interrupt controller; only the 4 GB Pi 4B is validated (1/2/8 GB models untested). | Known limitation. |
| SError masked (TD-10) | Asynchronous SError is masked in early kernel paths because a live PCIe/VL805 USB external-abort SError is not yet root-caused; unmasking regresses boot. A dump-and-halt handler is implemented and armed for when the abort is fixed. | Known, HW-gated. |
| `hal_memset` DC-ZVA (TD-20) | The `dc zva` fast path is disabled on the Cortex-A72 pending proof of the EL2 DC-ZVA trap state (does not reproduce in QEMU). Performance-only; correctness-safe. | Known limitation. |
| Stack-exhaustion signal DoS | A userspace process that **exhausts its 1 MiB main-thread stack** takes down the kernel: fault-signal delivery (`hal_cpuPushSignal`, aarch64) writes the signal frame to the now-unmapped user stack and double-faults at EL1, with no recovery — so any program overflowing its stack crashes the box (register-confirmed via a stack-bomb repro, `tools/stack-bomb/`). Fix = a guarded signal-frame push (validate the target is in a writable mapped region before writing; terminate the process cleanly on failure), minding the demand-paged-stack case. **Attended** (highest-blast-radius kernel path — invisible to a green-boot smoke). | Known; turnkey work-order in [docs/inprogress/2026-08-22-signal-push-stack-exhaustion-dos-workorder.md](inprogress/2026-08-22-signal-push-stack-exhaustion-dos-workorder.md). |
| Ethernet link IRQ (TD-Eth-LinkIRQ) | The PHY's `INT_B` line is not routed to a GIC SPI on the Pi 4 board, so link state is MDIO-polled at 1 Hz (as Linux/U-Boot also do). | By design for this board. |
| SD write throughput | Reads use UHS-I **DDR50** (1.8V) + **SDMA** + multi-block CMD18 (~38 MB/s, ~86% of Linux on the same card); writes are multi-block CMD25 but **PIO** (~17 MB/s DDR50 / ~13 MB/s HS50 — a BCM2711 controller quirk corrupts DMA writes, so they stay PIO). Multi-block writes verified 0-corruption. The 1.8V/DDR50 switch is reliable on netboot and best-effort on SD-boot (falls back to HS50). Remaining (attended) levers: DMA writes and reliable DDR50 on SD-boot. | Reads fast; writes correct but PIO-bound. |
| Audio | PWM audio over the 3.5 mm jack works with a streaming DMA ring and a Quakespasm mixer backend; an audible end-to-end sign-off on real headphones is still pending. | Partial. |
| Netboot NFS-root reliability | Over NFS root, an early command can hit the pre-takeover RAM root and report `not found` if `psh` starts before the NFS-root `takeover` completes (a boot-order race; the clean fix is a `plo` boot-order gate), and asset-heavy loads can see transient read failures in the NFS lease-reclaim window. Booting from the **SD/eMMC ext2 root** avoids both, and a **gigabit link** (now the default lab setup) greatly reduces the transient reads. | Known; use SD-boot or gigabit for asset-heavy workloads. |

## Partial / not started

- **WiFi (BCM43455) — control-plane only; do NOT rely on wireless networking
  yet.** The **control-plane works**: the driver associates to a real WPA2-PSK
  access point and completes the 4-way key handshake. The **data-plane does not
  carry traffic yet** — this is under active debugging, not abandoned (see
  `docs/inprogress/wifi-bcm43455-impl.md`). Until it lands, **use wired Ethernet**
  (fully working). The proprietary Cypress firmware blobs are **not vendored** in
  this repository (copyright/EULA hygiene); `scripts/stage-bcm43455-firmware.sh`
  stages them locally into a gitignored `.firmware/`.
- **Bluetooth (BCM43455) — driver-level bring-up only.** `/dev/hci0` comes up, the
  firmware patchram loads (323/323), a real BD_ADDR is read, and an HCI Inquiry
  completes. There is **no host Bluetooth stack** — no pairing, profiles, or audio
  — so it is not usable for real Bluetooth work yet.
- USB mass storage, I²C / SPI / PWM general-purpose drivers, and camera (CSI-2) /
  DSI display are not implemented.
