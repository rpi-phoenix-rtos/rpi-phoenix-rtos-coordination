# Known issues and limitations

Open items in the Phoenix-RTOS Raspberry Pi 4 port, as of 2026-09-09. This is
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
| #67 | **vkQuake: the Quake start-map wall torches (`progs/flame.mdl`) render INTERMITTENTLY on V3D.** Historic pass rates at a fixed viewpoint: default cvars **0/3**; `+r_lerpmodels 2` **2/8**. **Current shipped build: 9/9 runs LIT** — much improved and possibly resolved, but a pass rate bounds rather than closes it (this row's own argument), and no fix was ever identified, so it stays open. quakespasm renders the same flames reliably (401–548 lit px), so it is vkQuake-specific. **Do not close this on a screenshot** — it was declared fixed five times historically, each time on a single frame, and one of those "proofs" was a moving `misc_fireball` lavaball mistaken for a wall torch. Verify only with a pass RATE: `./scripts/test-cycle-bench.sh 8 <label> -- "vkquake +map start"` then `./scripts/check-torch-rois.py --rate <label>`. Refuted so far: **submit serialization** (the v3d-submit-race fix above eliminated the CSD stall and the capture flake but left the torch rate at 0/8 vs ~1/8, Fisher p=1.0, with `DROPPED job` still 2-5 per boot -- so #67 is NOT the race); the alpha/scanout theory (`d3e329c` is a no-op — vkQuake's nobright palette already sets alpha=255 on every index), `blend==0` (`v_shot.mdl` renders at `blend=0.000`), a stale V3D shader cache, and non-deterministic GPU archives (a forced rebuild is bit-identical). Full history: `docs/misc/2026-09-03-quake-torch-regression-archaeology.md`. | Open. Intermittent; no fix shipped. |
| #64 | SD-side filesystem stack pressure under load (deep fs call chains). | Open. |
| #66 | Stale `/tmp/.X0-lock` prevents the X server restarting after an unclean exit. | Open; remove `/tmp/.X0-lock` before relaunching the X server. |
| V3D-binner-wedge | An **intermittent V3D binner wedge on long GPU runs** (auto-recovered by a GPU reset). Not specific to any one engine or API — it is seen under the GL engines and under Vulkan. `q3dm7` is the most reliable reproducer. | Open; the heavy-fragment CT1 *render*-stage wedge class was root-caused and fixed 2026-08-27 (uncleared QPU-interrupt bits); a residual binner/CT0 wedge with a different signature remains. |
| STK-crash | **SuperTuxKart faults intermittently in its own code** — historically ~40% of runs, at *varying* sites. `addr2line` on four distinct fault PCs: `SkiddingAI::findNonCrashingPoint()`, `FontManager::loadFonts()` (startup), and **two inside libphoenix's allocator** (`lib_listRemove`, `_malloc_chunkJoin`) — so the allocator is the victim, and a 4.8 M-op harness exonerated it of any internal defect. Both allocator sites are now guarded (a corrupt free-bin link abandons the bin instead of being followed). A 6-trial bench after that came back **0/6**, which is suggestive (p≈0.05 against the 2-of-5 baseline) but **not** evidence the guards fixed it: no guard fired, so the corruption did not occur, and the relink perturbed heap layout. Gameplay itself is fine — the race renders correctly at `FPS: 8/9/9`. | Open; app-side corruption, cause not located. |
| mc/nano-interactive | `mc` and `nano` build and ship on the image, but neither has been **exercised interactively on hardware** — built and staged, not use-tested. | Open (verification gap, not a known defect). |
| q3-qvm-recipe | Quake III's `pak1.pk3` (three QVMs built from **ioquake3**, needed because the free demo's 1999 QVMs report UI API 3 while quake3e requires 6) is **staged from `assets/quake3-qvm/` rather than rebuilt from source** — the QVM build recipe is not yet in the repo. The game itself needs no retail content and no retail CD key; see `assets/quake3-qvm/README.md`. | Open (reproducibility gap, not a runtime bug). |

| AF_UNIX-child-abort | An `accept_connect_liveness` child in `test-libc-unix-socket` occasionally exits **70** (`EX_SOFTWARE`) with libphoenix reporting `Double free detected`. **Cause unknown, and two earlier conclusions of mine about it were wrong** (first "no code path exits 70" — it comes from libphoenix; then "the pointer is garbage because heap=0x2000 is page 2" — a real process reports `heapLo=0x2000`, so page 2 is a legitimate first heap). Rate unclear: absent in 350 iterations, then one recurrence after a desktop session in the same boot. The allocator now prints its heap window, so the next occurrence can distinguish a wild pointer from a smashed real block from the log alone. | Open; rare, does not affect the desktop or games. |
| Q2-underwater-stopgap | Quake II's underwater Y-mirror is fixed by a **conditional correction on a conditional bug**, which over-corrects at `viewsize <= 71`. The clean fix is to replace Mesa's `>=1024x768` FlipY size gate with a real scanout predicate. ⓘ Its performance justification was **measured and refuted** (see the STK note in the changes doc), so what remains is code hygiene: five per-app compensators today, and the next port with a large offscreen FBO renders upside down until someone adds a sixth. | **Closed as a decision (owner, 2026-09-09): leave as is.** The stopgap ships; the real fix is not planned. |
| shader-cache-speckle | A **stale Mesa shader-cache blob** renders as green speckle over an otherwise-valid frame. The cache has no ELF build-id, so a host toolchain change does not invalidate it. **Clear the cache on any clean rebuild** (`sync-netboot-tree.sh` does this, which is why the first GL run after a re-stage is slow). | Known; clear the cache on rebuild. |
| xterm-resize-artefacts | Owner-reported artefacts while dragging an xterm resize on the GPU X desktop. **Not reproduced** by a sound measurement; resize *correctness* verified (server and client agree once settled). Most likely intermittent frames during a fast drag — this stack has no compositing and presents by GPU readback. | Open, unreproduced; needs to be re-checked with the drag *released*. |

## System-level limitations

| Area | Limitation | Status |
|---|---|---|
| Board portability (TD-06) | The DTB parser assumes a single interrupt controller; only the 4 GB Pi 4B is validated (1/2/8 GB models untested). | Known limitation. |
| Early-console alias — cross-board port (B5) | The generic aarch64 early-console path (`phoenix-rtos-kernel/hal/aarch64/generic/console.c`, `_hal_consoleEarlyPutch`) hardcodes the UART at the fixed VA alias `0xffffffffffe00000` — it must print *before* the DTB-discovered base is available. On the **Pi 4B this alias equals the discovered pl011 base, so it is correct** and is intentionally left as-is. It only matters when porting to **another aarch64 board** (Pi 5, Pi Zero 2 W, other SoCs) whose console isn't at that alias: there, the early-print path must be conditionalized to use the board's discovered base, or early boot output goes to the wrong address. A source comment marks the exact spot. | **By design for the Pi 4; a to-do only for a future cross-board port.** |
| SError masked (TD-10) | Asynchronous SError is masked in early kernel paths because a live PCIe/VL805 USB external-abort SError is not yet root-caused; unmasking regresses boot. A dump-and-halt handler is implemented and armed for when the abort is fixed. | Known, HW-gated. |
| `hal_memset` DC-ZVA (TD-20) | The `dc zva` fast path is disabled on the Cortex-A72 pending proof of the EL2 DC-ZVA trap state (does not reproduce in QEMU). Performance-only; correctness-safe. | Known limitation. |
| Stack-exhaustion signal DoS | A userspace process that **exhausts its 1 MiB main-thread stack** takes down the kernel: fault-signal delivery (`hal_cpuPushSignal`, aarch64) writes the signal frame to the now-unmapped user stack and double-faults at EL1, with no recovery — so any program overflowing its stack crashes the box (register-confirmed via a stack-bomb repro, `tools/stack-bomb/`). Fix = a guarded signal-frame push (validate the target is in a writable mapped region before writing; terminate the process cleanly on failure), minding the demand-paged-stack case. **Attended** (highest-blast-radius kernel path — invisible to a green-boot smoke). | Known; turnkey work-order in [docs/done/2026-08-22-signal-push-stack-exhaustion-dos-workorder.md](done/2026-08-22-signal-push-stack-exhaustion-dos-workorder.md). |
| **Windowed GL in X11 is bandwidth-bound (~10 fps), by architecture** | A GL application drawing into a **window** on the X desktop copies its pixels **GPU → CPU → socket → CPU → GPU every frame**. Measured on the 640×480 demo window: **96 ms/frame (10.4 fps)**, of which `draw` is **2.7 ms** and the rest is the round trip — `glReadPixels` 12.5 ms, packing 6.7 ms, and **`XPutImage` 58 ms** pushing 1.23 MB through the X socket. Full-screen GL is *not* affected: the games bypass X entirely and render straight into the scanout buffer, which is why QuakeSpasm manages 37 fps and Quake III 34 fps in the same build. **Root cause:** this port has no equivalent of DRI3/DMA-BUF, so there is no way for a client to hand the X server a *reference* to a rendered buffer — only the pixels. Contributing detail: there is no `resource_get_handle` (export) in this Mesa build, and MIT-SHM is not available (no shm, extension not built). Every cheaper mitigation has been measured and closed: a larger AF_UNIX ring (+12%, throughput flat at ~12 MB/s), poller contention (refuted), a cached alias for the readback (refuted — uncached DRAM is only 1.3× off cached), and deferring the present (no effect). | **By design for now — owner decision, 2026-09-09.** A point fix (passing a buffer handle over an X property) was designed, and two of its four pieces were proven on hardware, but it was **closed deliberately**: it would leave the port with no general buffer-sharing story, and its ceiling was only **~1.7×** (≈17 fps) because the DDX present — 1.74 ms + 0.07 ms/row, ≈35 ms for a 480-row window — survives the change. If this is ever worth paying for, the right investment is a **Phoenix-RTOS-specific DRI/DRM layer** that all clients and the server share, not a bespoke channel. Reference (not a plan): [docs/misc/2026-09-09-gl-window-buffer-sharing-work-order.md](misc/2026-09-09-gl-window-buffer-sharing-work-order.md). |
| **`std::chrono` has 1-second resolution** (toolchain) | libstdc++ here is built with **no time backends**, so `std::chrono::steady_clock` falls back to whole-second `time()`. It capped SuperTuxKart at exactly 1 fps until a per-port `CLOCK_MONOTONIC` patch, and it **fails silently** everywhere else — any C++ timing on this port is suspect. General fix = rebuild libstdc++ with `--enable-libstdcxx-time=rt` (a whole-system C++ rebuild). | **Being fixed** (owner, 2026-09-09) — libstdc++ is being rebuilt with the time backends enabled. Note it is broader than `chrono`: `_GLIBCXX_USE_NANOSLEEP` and `_GLIBCXX_USE_SCHED_YIELD` are off too, so `this_thread::sleep_for` and `yield` also degrade. |
| **SD boot of the current image is unverified** | The host card reader is **detached** (it shares the USB-C port with power/eth) and there is no card in the Pi, so the shipped image cannot be flashed or SD-booted on this bench — the owner is the first to boot it from a card. Substitutes in place: the contents gate now asserts the FAT boot partition (the six files the firmware needs, `arm_64bit=1`, and that every file `config.txt` names exists) and runs automatically inside `--variant sd`; plus a QEMU structural boot check. All other verification is netboot/NFS. | Known bench limitation; mitigated by gates, not by a boot. |
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
