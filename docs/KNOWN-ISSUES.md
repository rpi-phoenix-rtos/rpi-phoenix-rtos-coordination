# Known issues and limitations

Open items in the Phoenix-RTOS Raspberry Pi 4 port, as of 2026-09-12. This is
the user-facing summary; the exhaustive engineering registries are:

- [docs/pi4-hardware-support-matrix.md](pi4-hardware-support-matrix.md)
  — per-peripheral status with evidence.
- [docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md](TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
  — the `TD-NN` transitional-debt registry (its bottom "Tracking Checklist" is
  authoritative; most `TD` items are already resolved).

Resolved items are removed from this file as they are fixed, so it shrinks over
time; the git history keeps the record of what was fixed.

> **Editing note.** Each row below must be **one line with exactly three `|`
> cells**, and the table must contain **no blank lines** — a blank line ends the
> table and everything after it renders as plain text. Long analysis belongs in
> `docs/misc/` or `docs/done/`, linked from the row, not inlined here.

## Application bugs

These affect the showcase apps, not the base system.

| ID | Symptom | Status / workaround |
|---|---|---|
| #67 | **vkQuake wall torches (`progs/flame.mdl`) render intermittently on V3D.** quakespasm renders the same flames reliably, so it is vkQuake-specific. **Never close this on a screenshot** — it was declared fixed five times historically, each on a single frame, and one "proof" was a moving lavaball mistaken for a wall torch. Verify only with a pass *rate*: `./scripts/test-cycle-bench.sh 8 <label> -- "vkquake +map start"` then `./scripts/check-torch-rois.py --rate <label>`. Refuted: submit serialization, the alpha/scanout theory, `blend==0`, a stale shader cache, non-deterministic GPU archives. History: [`docs/misc/2026-09-03-quake-torch-regression-archaeology.md`](misc/2026-09-03-quake-torch-regression-archaeology.md). | Open. Current shipped build is **9/9 lit** — much improved, but a pass rate bounds rather than closes it and no fix was ever identified. |
| #64 | SD-side filesystem stack pressure under load (deep fs call chains). | Open. |
| #66 | Stale `/tmp/.X0-lock` prevents the X server restarting after an unclean exit. | Open; remove `/tmp/.X0-lock` before relaunching. |
| V3D-binner-wedge | Intermittent V3D binner wedge on long GPU runs, auto-recovered by a GPU reset that reports loudly (`RENDER TIMEOUT` / `DROPPED job`). Seen under both GL and Vulkan; `q3dm7` is the documented reproducer. One root cause was found and fixed in the interim: `MAP_CONTIGUOUS` returns **non-zeroed** DRAM, so unpopulated binner-overflow-pool slots held cold-boot garbage that CT1 followed as a wild pointer; the pool is now zeroed. | Open, not reproduced recently — **3 q3dm7 runs, 27605 frames, 0 wedges**, plus 12 six-app gate runs at 0; last actually observed 2026-08-22. ⚠ Kept open deliberately: clean runs also predate the pool fix, so for an intermittent this bounds the rate rather than proving it gone. |
| freebin-corruption | **The allocator is the victim, not the origin — mechanism now identified (2026-09-12).** A free bin ends up holding a pointer to memory that is no longer what it thinks, and a live heap header's `size` field reads back with its **high 32 bits overwritten** (`0x80000001_0000d000` where the true size is `0xd000`). The process then dies computing `heap + heap->size`. The corrupting values decode as **RGBA8 pixels** (`A=128`, exactly 50% alpha, twice), and the identified route is GPU buffer pages being returned to the kernel behind the winsys's back and re-taken by `malloc` — see the Mesa/winsys BO-ownership fix under test. Contained meanwhile: `heap->size` is now bounds-checked from above (libphoenix `11286c0`), and both bin hand-out paths validate their result. Eliminated, do not re-walk: insertion of a heap base, writes into a poisoned freed chunk (8 trials, 0 fires), intra-struct overflow, STK's drive-graph index, and a userspace buffer overflow (11/11 page-aligned siblings, p≈1e-27). | Open (first cause under test); **contained** — affected runs finish with 0 faults. Detail: [`docs/misc/2026-09-12-two-corruption-signatures.md`](misc/2026-09-12-two-corruption-signatures.md). |
| stk-highbits-pointer | SuperTuxKart dies ~1 run in 10 with `far = 0x80000001_<valid heap address>` — a pointer whose **upper 32 bits are garbage** (bit 63 set is never a valid aarch64 userspace pointer). Symbolised to `FontWithFace::render`, and the signature **predates and survived** the drive-graph guards (seen 09-08, 09-09, 09-11, 09-12), so it is distinct from the fixed AI crash. The faulting `{_M_start,_M_finish}` pair is **self-consistent** (finish−start equals the element count, both words share the same bogus high half), so the pointer *arrived* bad rather than being overwritten in place. | Open. **Believed the same root cause as `freebin-corruption`** — same high-word signature, same process-wide character. ⏭ At the next occurrence dump the whole page, not just registers. Analysis: [`docs/misc/2026-09-12-stk-highbits-pointer-analysis.md`](misc/2026-09-12-stk-highbits-pointer-analysis.md). |
| allocator-double-free | libphoenix's allocator reports a double free and the process exits **70** (`EX_SOFTWARE`). **Not AF_UNIX-specific**: the report appears in 21 logs from 2026-06-28 → 09-09 across xcalc (×7), Dillo, vkQuake (×2), the glamor desktop (×5) and `test-libc-unix-socket` (×4). A generic heap-corruption symptom with several causes, some already fixed separately (the `vasprintf` overflow behind glib2/mc/Dillo; `free()` of `.rodata` in X11). ⚠ Only **one** occurrence in the whole archive captured block detail, so inferences from it rest on a sample of one — and two readings this row used to assert were wrong and have been corrected against the allocator source. | Open, rare; **no field occurrence since 2026-09-09**. The report now names its caller plus both footers, which discriminate the mechanism on sight: `foot == size & ~3` means the allocator wrote the header (a genuine double free); a mismatch means an overrun or a planted footer. Resolve `caller=` with `addr2line` against the unstripped `prog/` copy. |
| premain-hang | ⚠ **Reopened 2026-09-11 — the V3D clock-race fix does not cover this.** The original report was a QuakeSpasm launch that produced **no output whatsoever** after psh echoed the command and never returned the prompt; a passing run prints `main() entered` on the very next line, so that failure was genuinely before `main()`. The V3D stall found while hunting it prints `main() entered` plus ~15 further lines, so it is a different bug, and this row was briefly (and wrongly) marked fixed on that basis. Localisation stands: `_init_array()` exonerated by disassembly, leaving `_libc_init()` where exactly one call can block — `isatty()` → `tcgetattr()`, an ioctl to the tty through a port. | Open; rate was ~1 in 30 boots, not seen since, but nothing has been done that would address it. ⏭ Cheap discriminator: storm `/usr/bin/quakespasm -loadbench` and classify any stall by whether `main() entered` appears. |
| q3-qvm-recipe | Quake III's `pak1.pk3` (three QVMs built from **ioquake3**, needed because the free demo's 1999 QVMs report UI API 3 while quake3e requires 6) is **staged from `assets/quake3-qvm/` rather than rebuilt from source** — the QVM build recipe is not yet in the repo. The game itself needs no retail content and no CD key; see `assets/quake3-qvm/README.md`. | Open (reproducibility gap, not a runtime bug). |
| shader-cache-speckle | A **stale Mesa shader-cache blob** renders as green speckle over an otherwise-valid frame. The cache carries no ELF build-id, so a host toolchain change does not invalidate it. | Known; clear the cache on any clean rebuild (`sync-netboot-tree.sh` does, which is why the first GL run after a re-stage is slow). |
| xterm-resize-artefacts | Owner-reported artefacts while dragging an xterm resize on the GPU X desktop. **Not reproduced** by a sound measurement; resize *correctness* is verified (server and client agree once settled). Most likely intermittent frames during a fast drag — this stack has no compositing and presents by GPU readback. | Open, unreproduced; re-check with the drag *released*. |

## System-level limitations

| Area | Limitation | Status |
|---|---|---|
| Board portability (TD-06) | The DTB parser assumes a single interrupt controller; only the 4 GB Pi 4B is validated (1/2/8 GB models untested). | Known limitation. |
| Early-console alias — cross-board port (B5) | The generic aarch64 early-console path (`hal/aarch64/generic/console.c`) hardcodes the UART at the fixed VA alias `0xffffffffffe00000`, because it must print *before* the DTB-discovered base is available. On the Pi 4B this alias **equals** the discovered pl011 base, so it is correct and intentionally left as-is. It only matters when porting to another aarch64 board whose console is elsewhere. | By design for the Pi 4; a to-do only for a future cross-board port. |
| SError masked (TD-10) | Asynchronous SError is masked in early kernel paths because a live PCIe/VL805 USB external-abort SError is not yet root-caused; unmasking regresses boot. A dump-and-halt handler is implemented and armed for when the abort is fixed. ⚠ This is why an MMIO read of an unclocked block **hangs silently** instead of aborting. | Known, HW-gated. |
| `hal_memset` DC-ZVA (TD-20) | The `dc zva` fast path is disabled on the Cortex-A72 pending proof of the EL2 DC-ZVA trap state (does not reproduce in QEMU). Performance-only; correctness-safe. | Known limitation. |
| Stack-exhaustion signal DoS | A userspace process that **exhausts its 1 MiB main-thread stack** takes down the kernel: fault-signal delivery (`hal_cpuPushSignal`, aarch64) writes the signal frame to the now-unmapped user stack and double-faults at EL1, with no recovery — so any program overflowing its stack crashes the box (register-confirmed via `tools/stack-bomb/`). Fix = a guarded signal-frame push, minding the demand-paged-stack case. **Attended**: highest-blast-radius kernel path, and invisible to a green-boot smoke. | Known; turnkey work-order in [docs/done/2026-08-22-signal-push-stack-exhaustion-dos-workorder.md](done/2026-08-22-signal-push-stack-exhaustion-dos-workorder.md). |
| Windowed GL in X11 is bandwidth-bound (~10 fps), by architecture | A GL application drawing into a **window** copies its pixels GPU → CPU → socket → CPU → GPU every frame. Measured on the 640×480 demo window: **96 ms/frame (10.4 fps)**, of which `draw` is 2.7 ms and the rest is the round trip (`glReadPixels` 12.5 ms, packing 6.7 ms, `XPutImage` **58 ms** pushing 1.23 MB through the X socket). Full-screen GL is unaffected — the games bypass X and render straight into the scanout buffer (QuakeSpasm ~37–48 fps, Quake III 25–46). **Root cause:** no DRI3/DMA-BUF equivalent, so a client can only hand the server pixels, never a buffer reference. Cheaper mitigations all measured and closed: a larger AF_UNIX ring (+12%), poller contention (refuted), a cached readback alias (refuted), deferred present (no effect). | **By design for now — owner decision, 2026-09-09.** A point fix was designed and half-proven on hardware but closed deliberately: its ceiling was only ~1.7× (≈17 fps) because the DDX present survives the change, and it would leave the port with no general buffer-sharing story. The right investment, if ever needed, is a Phoenix-specific DRI/DRM layer. Reference: [docs/misc/2026-09-09-gl-window-buffer-sharing-work-order.md](misc/2026-09-09-gl-window-buffer-sharing-work-order.md). |
| SD boot of the current image is unverified on this bench | The host card reader is **detached** (it shares the USB-C port with power/eth) and there is no card in the Pi, so the shipped image cannot be flashed or SD-booted here — the owner is the first to boot it from a card. Substitutes: the contents gate asserts the FAT boot partition (the six files the firmware needs, `arm_64bit=1`, and that every file `config.txt` names exists) and runs automatically inside `--variant sd`, plus a QEMU structural boot check. All other verification is netboot/NFS. | Known bench limitation; mitigated by gates, not by a boot. |
| Ethernet link IRQ (TD-Eth-LinkIRQ) | The PHY's `INT_B` line is not routed to a GIC SPI on the Pi 4 board, so link state is MDIO-polled at 1 Hz (as Linux and U-Boot also do). | By design for this board. |
| SD write throughput | Reads use UHS-I **DDR50** (1.8V) + **SDMA** + multi-block CMD18 (~38 MB/s, ~86% of Linux on the same card); writes are multi-block CMD25 but **PIO** (~17 MB/s DDR50 / ~13 MB/s HS50 — a BCM2711 controller quirk corrupts DMA writes, so they stay PIO). Multi-block writes verified 0-corruption. The 1.8V/DDR50 switch is reliable on netboot and best-effort on SD-boot (falls back to HS50). | Reads fast; writes correct but PIO-bound. Remaining (attended) levers: DMA writes, reliable DDR50 on SD-boot. |
| Audio | PWM audio over the 3.5 mm jack works with a streaming DMA ring and a Quakespasm mixer backend; an audible end-to-end sign-off on real headphones is still pending. | Partial. |
| Netboot NFS-root: one narrow boot-order race | A command issued *before* the NFS-root `takeover` completes can still hit the pre-takeover RAM root and report `not found`; the clean fix is a `plo` boot-order gate. The test harness already covers it with `--inter-cmd-secs`. ⓘ The broader "NFS root is unreliable, prefer SD" framing this row used to carry is **not supported by measurement**: across **297 NFS-root boots on 2026-09-11/12, 297 reached the psh prompt (100%)** and there were **zero** `ERANGE` / `exec -12` / `NF4ERR_EXPIRED` lease-reclaim failures. (A host-side `nfs-server` restart is still the fix if a stale export does appear after days of uptime.) | Known, narrow; NFS root is the routine lab configuration and is reliable in practice. |

## Partial / not started

- **WiFi (BCM43455) — data plane proven at FULL MTU in both directions; the
  lwip netif is the remaining integration step.** The driver joins a real
  WPA2-PSK access point, completes the 4-way key handshake, **and carries real
  traffic over the air**: it obtains a full DHCP lease (DISCOVER → OFFER →
  REQUEST → ACK, confirmed by the AP's `DHCPACK`) via `tools/wifi-probe
  jointxcnt`. Beyond that lease the frame path carries **full-size ethernet
  frames both ways**, verified byte-for-byte: the Pi sends 400/1000/**1472**-byte
  UDP payloads that a host socket receives bit-exact, and reads back a host-sent
  1472-byte tagged probe unchanged. Two fixes were needed — SDIO byte-mode CMD53
  caps a transfer at 512 bytes (larger requests silently wrapped the 9-bit count
  field, so 525..2048 was a corruption band), and function 2's block size had
  never been programmed, which made block mode unusable on the data path. The
  driver exposes `/dev/wifidata` (write a frame / read a frame) as the seam for
  an lwip netif. That netif is **not** wired up, so arbitrary sockets do not use
  WiFi — for everyday networking use wired Ethernet (fully working).

  *Measurement note for anyone debugging this:* `tcpdump` on the host's AP
  interface does **not** see frames sent by the Pi — not even the DHCP that the
  host's own dnsmasq answers — so it is useless as an egress detector on this
  rig and has twice produced a false "no egress" conclusion. Use an
  application-level detector (a UDP socket, or dnsmasq's own logs);
  `scripts/wifi-air-monitor.sh` wraps the ones that work. (The earlier "TX
  reaches the firmware but not the air" report was a measurement artifact — a
  link-layer counter that does not count broadcast/pre-lease frames.) The
  proprietary Cypress firmware blobs are **not vendored** here
  (copyright/EULA hygiene); `scripts/stage-bcm43455-firmware.sh` stages them
  into a gitignored `.firmware/`.
- **Bluetooth (BCM43455) — driver-level bring-up only.** `/dev/hci0` comes up,
  the firmware patchram loads (323/323), a real BD_ADDR is read, and an HCI
  Inquiry completes. There is **no host Bluetooth stack** — no pairing,
  profiles, or audio — so it is not usable for real Bluetooth work yet.
- USB mass storage, I²C / SPI / PWM general-purpose drivers, and camera (CSI-2) /
  DSI display are not implemented.
