# Known issues and limitations

Open items in the Phoenix-RTOS Raspberry Pi 4 port, as of 2026-09-03. This is
the user-facing summary; the exhaustive engineering registries are:

- [docs/pi4-hardware-support-matrix.md](pi4-hardware-support-matrix.md)
  — per-peripheral status with evidence.
- [docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md](TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
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
| V3D-binner-wedge | An **intermittent V3D binner wedge on long GPU runs** (auto-recovered by a GPU reset). Not specific to any one engine or API — it is seen under the GL engines and under Vulkan. `q3dm7` is the most reliable reproducer. | Open; the heavy-fragment CT1 *render*-stage wedge class was root-caused and fixed 2026-08-27 (uncleared QPU-interrupt bits); a residual binner/CT0 wedge with a different signature remains. |
| STK-nfs-assets | **SuperTuxKart has not been verified in-game on the clean image.** `stk` ships on the card (`/usr/bin/supertuxkart`) and its GPU-drawn UI renders with **0 wedges and 0 faults**, but its 194 MB of assets served over **NFS** does not finish loading inside a ~5 minute window, so the in-game race is not yet confirmed on the clean image. This is asset throughput, not rendering. | Open; test from the SD card, where the asset roots are local. |
| X-root-black | With the GPU-accelerated X server (`startx_gpu`), the **root window paints black instead of mauve**. Cosmetic only — the window manager, xterm, `xclock` and `xcalc` all render and the xterm shell is live (`artifacts/hdmi/20260903-053119-final-xgpu-tick.png`). | Open, cosmetic (`wmsetbg` path). |
| mc/nano-build | **`mc` and `nano` currently fail to build**, so they are not on the image. Both are convenience ports; nothing else depends on them, and `build-showcase-apps.sh` records them as soft failures rather than aborting the run. | Open. |
| q3-qvm-recipe | Quake III's `pak1.pk3` (three QVMs built from **ioquake3**, needed because the free demo's 1999 QVMs report UI API 3 while quake3e requires 6) is **staged from `assets/quake3-qvm/` rather than rebuilt from source** — the QVM build recipe is not yet in the repo. The game itself needs no retail content and no retail CD key; see `assets/quake3-qvm/README.md`. | Open (reproducibility gap, not a runtime bug). |

## System-level limitations

| Area | Limitation | Status |
|---|---|---|
| Board portability (TD-06) | The DTB parser assumes a single interrupt controller; only the 4 GB Pi 4B is validated (1/2/8 GB models untested). | Known limitation. |
| Early-console alias — cross-board port (B5) | The generic aarch64 early-console path (`phoenix-rtos-kernel/hal/aarch64/generic/console.c`, `_hal_consoleEarlyPutch`) hardcodes the UART at the fixed VA alias `0xffffffffffe00000` — it must print *before* the DTB-discovered base is available. On the **Pi 4B this alias equals the discovered pl011 base, so it is correct** and is intentionally left as-is. It only matters when porting to **another aarch64 board** (Pi 5, Pi Zero 2 W, other SoCs) whose console isn't at that alias: there, the early-print path must be conditionalized to use the board's discovered base, or early boot output goes to the wrong address. A source comment marks the exact spot. | **By design for the Pi 4; a to-do only for a future cross-board port.** |
| SError masked (TD-10) | Asynchronous SError is masked in early kernel paths because a live PCIe/VL805 USB external-abort SError is not yet root-caused; unmasking regresses boot. A dump-and-halt handler is implemented and armed for when the abort is fixed. | Known, HW-gated. |
| `hal_memset` DC-ZVA (TD-20) | The `dc zva` fast path is disabled on the Cortex-A72 pending proof of the EL2 DC-ZVA trap state (does not reproduce in QEMU). Performance-only; correctness-safe. | Known limitation. |
| Stack-exhaustion signal DoS | A userspace process that **exhausts its 1 MiB main-thread stack** takes down the kernel: fault-signal delivery (`hal_cpuPushSignal`, aarch64) writes the signal frame to the now-unmapped user stack and double-faults at EL1, with no recovery — so any program overflowing its stack crashes the box (register-confirmed via a stack-bomb repro, `tools/stack-bomb/`). Fix = a guarded signal-frame push (validate the target is in a writable mapped region before writing; terminate the process cleanly on failure), minding the demand-paged-stack case. **Attended** (highest-blast-radius kernel path — invisible to a green-boot smoke). | Known; turnkey work-order in [docs/done/2026-08-22-signal-push-stack-exhaustion-dos-workorder.md](done/2026-08-22-signal-push-stack-exhaustion-dos-workorder.md). |
| Ethernet link IRQ (TD-Eth-LinkIRQ) | The PHY's `INT_B` line is not routed to a GIC SPI on the Pi 4 board, so link state is MDIO-polled at 1 Hz (as Linux/U-Boot also do). | By design for this board. |
| SD write throughput | Reads use UHS-I **DDR50** (1.8V) + **SDMA** + multi-block CMD18 (~38 MB/s, ~86% of Linux on the same card); writes are multi-block CMD25 but **PIO** (~17 MB/s DDR50 / ~13 MB/s HS50 — a BCM2711 controller quirk corrupts DMA writes, so they stay PIO). Multi-block writes verified 0-corruption. The 1.8V/DDR50 switch is reliable on netboot and best-effort on SD-boot (falls back to HS50). Remaining (attended) levers: DMA writes and reliable DDR50 on SD-boot. | Reads fast; writes correct but PIO-bound. |
| Audio | PWM audio over the 3.5 mm jack works with a streaming DMA ring and a Quakespasm mixer backend; an audible end-to-end sign-off on real headphones is still pending. | Partial. |
| Netboot NFS-root reliability | Over NFS root, an early command can hit the pre-takeover RAM root and report `not found` if `psh` starts before the NFS-root `takeover` completes (a boot-order race; the clean fix is a `plo` boot-order gate), and asset-heavy loads can see transient read failures in the NFS lease-reclaim window. Booting from the **SD/eMMC ext2 root** avoids both, and a **gigabit link** (now the default lab setup) greatly reduces the transient reads. | Known; use SD-boot or gigabit for asset-heavy workloads. |

## Partial / not started

- **WiFi (BCM43455) — data plane proven at FULL MTU in both directions; the
  lwip netif is the remaining integration step.** The driver
  joins a real WPA2-PSK access point, completes the 4-way key handshake, **and
  carries real traffic over the air**: it obtains a full DHCP IP lease
  (DISCOVER → OFFER → REQUEST → ACK, confirmed by the AP's `DHCPACK`) via
  `tools/wifi-probe jointxcnt`. Beyond that lease, the frame path now carries
  **full-size ethernet frames both ways**, verified byte-for-byte: the Pi sends
  400/1000/**1472**-byte UDP payloads that a host socket receives bit-exact, and
  reads back a host-sent 1472-byte tagged probe unchanged. Getting there needed
  two fixes — SDIO byte-mode CMD53 caps a transfer at 512 bytes (larger requests
  silently wrapped the 9-bit count field, so 525..2048 was a corruption band),
  and function 2's block size had never been programmed, which made block mode
  unusable on the data path. The driver now exposes `/dev/wifidata` (write a
  frame / read a frame) as the seam for an lwip netif. What is **not** wired up
  yet is that netif, so arbitrary sockets do not use WiFi and for everyday
  networking you should still **use wired Ethernet** (fully working).

  *Measurement note for anyone debugging this:* `tcpdump` on the host's AP
  interface does **not** see frames sent by the Pi — not even the DHCP that the
  host's own dnsmasq answers — so it is useless as an egress detector on this
  rig and has twice produced a false "no egress" conclusion. Use an
  application-level detector (a UDP socket, or dnsmasq's own logs);
  `scripts/wifi-air-monitor.sh` wraps the ones that work. (The earlier "TX reaches the firmware but
  not the air" report was a measurement artifact — a link-layer counter that
  doesn't count broadcast/pre-lease frames; the application-layer DHCP exchange
  is the ground truth.) The proprietary Cypress firmware blobs are **not
  vendored** in this repository (copyright/EULA hygiene);
  `scripts/stage-bcm43455-firmware.sh` stages them locally into a gitignored
  `.firmware/`.
- **Bluetooth (BCM43455) — driver-level bring-up only.** `/dev/hci0` comes up, the
  firmware patchram loads (323/323), a real BD_ADDR is read, and an HCI Inquiry
  completes. There is **no host Bluetooth stack** — no pairing, profiles, or audio
  — so it is not usable for real Bluetooth work yet.
- USB mass storage, I²C / SPI / PWM general-purpose drivers, and camera (CSI-2) /
  DSI display are not implemented.
