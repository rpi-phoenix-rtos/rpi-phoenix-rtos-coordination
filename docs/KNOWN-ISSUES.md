# Known issues and limitations

Open items in the Phoenix-RTOS Raspberry Pi 4 port, as of 2026-07-02. This is
the user-facing summary; the exhaustive engineering registries are:

- [docs/inprogress/pi4-hardware-support-matrix.md](inprogress/pi4-hardware-support-matrix.md)
  — per-peripheral status with evidence.
- [docs/inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md](inprogress/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
  — the `TD-NN` transitional-debt registry (its bottom "Tracking Checklist" is
  authoritative; most `TD` items are already RESOLVED).

## Application bugs

These affect the showcase apps, not the base system.

| ID | Symptom | Status / workaround |
|---|---|---|
| #64 | SD-side filesystem stack pressure under load (deep fs call chains). | Open. |
| #66 | Stale `/tmp/.X0-lock` prevents the X server restarting after an unclean exit. | Open; remove `/tmp/.X0-lock` before relaunching the X server. |
| #67 | Quake torch/flame rendering artifacts. | Open; cosmetic — gameplay unaffected. |
| #68 | Quake multiplayer hangs at the `LOADING` screen. | Open; single-player and demos work. |
| #69 | `xbill` exits silently instead of running. | Open; other Athena/X apps (xterm, xcalc, xedit) render fine. |
| #70 | Dillo has no TLS/HTTPS support. | Open; plain-HTTP pages load; HTTPS is unavailable. |

## System-level limitations

| Area | Limitation | Status |
|---|---|---|
| SMP | 4 cores enumerate, but the scheduler runs on cpu0 only (bringing the CNTV timer up on a secondary core breaks the primary). | Known; single-core scheduling is stable. |
| Board portability (TD-06) | The DTB parser assumes a single interrupt controller; only the 4 GB Pi 4B is validated (1/2/8 GB models untested). | Known limitation. |
| SError masked (TD-10) | Asynchronous SError is masked in early kernel paths because a live PCIe/VL805 USB external-abort SError is not yet root-caused; unmasking regresses boot. A dump-and-halt handler is implemented and armed for when the abort is fixed. | Known, HW-gated. |
| `hal_memset` DC-ZVA (TD-20) | The `dc zva` fast path is disabled on the Cortex-A72 pending proof of the EL2 DC-ZVA trap state (does not reproduce in QEMU). Performance-only; correctness-safe. | Known limitation. |
| Ethernet link IRQ (TD-Eth-LinkIRQ) | The PHY's `INT_B` line is not routed to a GIC SPI on the Pi 4 board, so link state is MDIO-polled at 1 Hz (as Linux/U-Boot also do). | By design for this board. |
| SD throughput | Reads/writes are single-block PIO; high-throughput multi-block DMA is not yet implemented. | Functional, not fast. |
| GPU Vulkan (V3DV) | Full Vulkan init and shader execution work on hardware; vkQuake is paused at the no-WSI texture-upload gap (buffer→image copy). | Partial. |
| Audio | PWM audio over the 3.5 mm jack works with a streaming DMA ring and a Quakespasm mixer backend; an audible end-to-end sign-off on real headphones is still pending. | Partial. |

## Not started

WiFi (BCM43455 — **blocked** on a firmware-execution gate that needs deeper
hardware visibility), Bluetooth, USB mass storage, I²C / SPI / PWM general
drivers, and camera (CSI-2) / DSI display are not implemented.
