# Phoenix-RTOS on Raspberry Pi 4 — Master Plan

Synthesis of the eight subsystem implementation plans in
`docs/plans/` and the gap-analysis in
[`docs/research/scope-pi4-uncovered.md`](../research/scope-pi4-uncovered.md).
This document is the integration point: critical path, dependency DAG,
parallel work streams, milestone roadmap, total effort, top risks,
decision points, Pi-5 forward-compat, and a 5-session sequencing
suggestion. It does **not** restate per-subsystem detail; each claim
cites the source plan.

---

## 1. Project state today

The kernel runs on real BCM2711 hardware on branch
`agent/rpi4-program-reloc` in `sources/phoenix-rtos-kernel`. Recent
validated milestones, in order: plo loads kernel and syspage; the
EL2→EL1 drop landed and `_init.S` now reaches `_hal_init` with the MMU
on (`SCTLR_EL1.M=1`) but caches off (TD-04 cache-coherency hack —
all DRAM mapped Normal Non-Cacheable; see
[cache-mmu-smp-impl.md §1](cache-mmu-smp-impl.md)). The kernel
banner and first klog lines render on UART and HDMI; psh starts
(`(psh)%`). USB phase 2 is in flight: VL805 BAR programming, PCI
BME/MSE, `xhci_capProbe` retry loop, and the cmd-ring 4 KB fix have
landed; `xhci_validateRuntime` passes; `xhci_init` cascade still has
steps 2.4–2.8 unverified on real silicon
([usb-xhci-impl.md §2](usb-xhci-impl.md)). What works: serial console,
HDMI text fbcon, kernel reaching userspace, partial USB host. What
does not: caches (so everything is DRAM-bound), > 1 GiB DRAM, SMP,
ethernet, WiFi, BT, GPIO API, SDHCI/SD-card boot, persistent
filesystems, watchdog, thermal read.

---

## 2. Vision: "Phoenix-RTOS on Pi 4 v1.0"

A v1.0 release of the Pi 4 port ships a system that boots cold from an
SD card, runs all four A72 cores cached at full DRAM bandwidth,
presents an interactive psh on HDMI/UART or a USB keyboard, talks
gigabit Ethernet, and is observable via thermal + watchdog telemetry.
Public artifacts: a Pi 4B image plus reproducible build scripts; one
public ABI per major peripheral (`/dev/fb0`, `/dev/gpiochip0`,
`/dev/watchdog0`, `/dev/thermal`, `/dev/hci0`, `/dev/sdaN`); a CI
loop running per-commit on real hardware.

**Mandatory v1.0 features (MUSTs).** Drawn from
[`scope-pi4-uncovered.md` §4](../research/scope-pi4-uncovered.md):
Stage 1 cache enable; SDHCI/EMMC2 storage controller; FAT + ext2
filesystem integration; SD-boot path; Generic-Timer audit on SMP4;
GIC-400 hardening for Pi 4 SPI routing; mailbox-OTP tag wiring (MAC,
serial, revision); userspace running on real hardware (psh + minimal
coreutils + USB keystrokes); CI loop. From the eight subsystem plans:
Stage 1 cache enable
([cache-mmu-smp-impl.md §2](cache-mmu-smp-impl.md));
USB host complete through HID keystrokes
([usb-xhci-impl.md §6](usb-xhci-impl.md)); GENET Ethernet at lwIP
DHCP/ICMP/TCP tier
([ethernet-genet-impl.md §1 Tier 3](ethernet-genet-impl.md));
GPU/VC6 Tier 1 (`/dev/fb0` mailbox-driven KMS shim)
([gpu-vc6-impl.md §1](gpu-vc6-impl.md)); GPIO/pinctrl Tiers 0–3
([gpio-pinctrl-impl.md §1](gpio-pinctrl-impl.md)); RTC-thermal-power
mailbox + watchdog + thermal + poweroff
([rtc-thermal-power-impl.md §1](rtc-thermal-power-impl.md));
4 GiB DRAM unlock and SMP4
([cache-mmu-smp-impl.md §3, §4](cache-mmu-smp-impl.md)).

**Explicitly deferred features.** WiFi
([wifi-bcm43455-impl.md](wifi-bcm43455-impl.md)) and Bluetooth
([bluetooth-bcm43455-impl.md](bluetooth-bcm43455-impl.md)) are large
multi-month efforts; they are post-v1.0 unless externally driven. GPU
Tier 2 (direct HVS scanout), GPU Tier 3 (V3D/Mesa), CSI camera, DSI
display, HDMI/I²S audio, Compute Module 4, Pi 400, Pi 5/RP1, secure
boot, A/B updates, persistent crash dump, V3D 3D, 40-bit DMA channels,
multi-bus I²C/SPI, USB-OTG device mode — all out of scope for v1.0
([scope-pi4-uncovered.md §5](../research/scope-pi4-uncovered.md)).

**Success criteria for shipping.** Cold-boot Pi 4B 4 GB from SD card
to interactive psh on HDMI in < 10 s; type into USB keyboard, see
echo; `dhclient` on `eth0` obtains a lease; `ping -c100` to the
gateway is 0 % loss; `cat /dev/thermal` returns sane mC; `reboot`
syscall actually reboots without operator power-cycle; CI loop runs
the same flow for every commit on `main`; all 4 A72 cores online;
≥ 3.5 GiB usable DRAM
([cache-mmu-smp-impl.md §6](cache-mmu-smp-impl.md)).

---

## 3. Dependency DAG

Subsystem-level dependencies derived from the "Inter-dependencies"
section of each source plan. Arrows point from prerequisite to
dependant.

```
                   EL2→EL1 drop (DONE)
                          │
                          ▼
                Stage-1 cache enable  ◄────────────────────────┐
              (cache-mmu-smp-impl §2)                          │
                  │       │      │                             │
                  │       │      ├─► USB throughput (xhci §9)  │
                  │       │      ├─► GPU Tier 1 (gpu §8)       │
                  │       │      ├─► GENET line rate (genet §10)
                  │       │      └─► WiFi DMA buffers (wifi §10)
                  │       │                                    │
                  │       ▼                                    │
                  │   4 GiB DRAM unlock ─► userspace > 1 GiB   │
                  │   (cache-mmu §3)                           │
                  │                                            │
                  ▼                                            │
              SMP cores 1–3                                    │
              (cache-mmu §4) ────────► spinlock-correct        │
                                       LDAXR/STLXR             │
                                                               │
   Mailbox driver (rtc-thermal §2) ────┬──► OTP/MAC/rev (scope §2.13)
                                       ├──► Thermal (rtc §4)
                                       ├──► expgpio for BT/WiFi
                                       │      WL_REG_ON / BT_REG_ON
                                       │      (bt §3, wifi §10)
                                       ├──► GPU mode-set (gpu §3)
                                       └──► Clock manager (scope §2.5)
                                              │
                                              ├─► I²C/SPI/PWM (scope §2.6–2.8)
                                              └─► SDHCI clk (scope §2.1)

   Watchdog (rtc §3) — independent of everything above

   GIC-400 hardening (scope §2.12) ────► Generic Timer audit (scope §2.4)
                                                │
                                                └─► all per-CPU IRQs
                                                    (genet §4, gpio §1, etc.)

   GPIO/pinctrl Tier 1–3 (gpio §1) ──┬──► I²C/SPI/PWM
                                     ├──► WiFi WL_REG_ON (mailbox alt)
                                     └──► HAT discovery

   USB host phase 1 (xhci §2)
       └─► HID keystrokes (xhci §4) ──► userspace input on real HW
                                              │
                                              └─► Userspace demo apps
                                                  Tier A psh applets
                                                  Tier B lua REPL
                                                  (userspace-demo-apps §1)
       └─► USB Hub class (scope §2.18)
       └─► USB MSC class (scope §2.17)

   SDHCI/EMMC2 (scope §2.1) ──► FAT + ext2 (scope §2.2)
                                       │
                                       ├─► SD boot (scope §2.16)
                                       └─► Persistent rootfs

   GENET (ethernet-genet §1) ──► lwIP integration ──► Netboot e2e
                                                       NTP, ssh-class
                                                       services (future)

   WiFi BCM43455 (wifi §1) needs:
       ├─ SDIO bus driver (NEW)
       ├─ GPIO + mailbox for WL_REG_ON
       ├─ Stable lwIP (after GENET)
       └─ Firmware blob distribution

   BT BCM43455 (bt §1) needs:
       ├─ Mailbox + expgpio (BT_REG_ON, WL_REG_ON sequencing)
       ├─ Second pl011-tty instance
       ├─ HCI server + .hcd patchram
       └─ License decision (nimBLE vs BTstack)

   DTB consumption (scope §2.14) — independent; unblocks RAM-size
                                   matrix and per-SKU config

   CI loop (scope §2.21) — independent of feature work; needs
                           SDHCI+FAT+SD-boot for full e2e

   Tinyx X11 demo (tinyx-x11-demo.md) needs:
       ├─ Stage-1 caches (M2)        — uncached pixman is unwatchable
       ├─ USB HID keyboard (M3)      — Phase 4 input
       ├─ Persistent rootfs (M4)     — Phase 6/7, ~10 MB assets
       ├─ /dev/fb0 Tier 1 (M8)       — Xfbdev display target
       ├─ NEW: evdev shim            — translates HID → input_event
       └─ NEW: usbmouse driver       — Phase 5 only
```

No cycles. Longest path:
**EL drop (done) → Stage-1 cache → SMP → SDHCI → FAT → SD-boot → real-HW
userspace → CI**, ~7 nodes deep. Stage-1 cache sits at the throat.

### Dependency matrix (compact)

| Subsystem | Hard prereqs | Unblocks |
|---|---|---|
| Stage-1 cache | EL drop | SMP, performance everywhere, GPU/GENET/USB throughput |
| Stage-2 4 GiB DRAM | Stage-1 (for usable BW) | userspace > 1 GiB, real FB carveouts |
| Stage-3 SMP | Stage-1 | spinlocks, scheduler tests, CI soak |
| Mailbox | (none — refactor from plo) | thermal, OTP, expgpio, clock-mgr, WiFi/BT/GPU init |
| Watchdog | (none) | reboot/poweroff, kernel panic recovery |
| GIC-400 hardening | EL drop | every IRQ-driven driver, SMP4 |
| Generic Timer audit | GIC, SMP | scheduler correctness |
| GPIO/pinctrl | GIC (for Tier 3 IRQ) | I²C, SPI, button input, HATs |
| USB host (xhci) | EL drop, pcie | HID keys, USB MSC, hubs |
| GPU Tier 1 | mailbox refactor, Stage-1 cache | `/dev/fb0` userspace |
| GENET | lwIP target enable, GIC | DHCP, TCP, NTP |
| SDHCI/EMMC2 | clock manager (or mailbox), DMA optional | FAT, ext2, SD-boot |
| FAT/ext2 fs | SDHCI | persistent rootfs |
| WiFi | SDIO bus (NEW), mailbox, GPIO, lwIP, GENET as shakedown | wireless networking |
| Bluetooth | mailbox+expgpio, second pl011-tty, GPIO 30–33 mux | BLE host stack |
| OTP/MAC/serial | mailbox | GENET MAC, telemetry |
| DTB consumption | (none) | multi-SKU support |
| Userspace on HW | SDHCI+FAT, USB HID, GENET | CI, soak |
| Demo apps (Tier A/B) | USB HID | first interactive demo image |
| CI loop | userspace on HW | per-commit gating |
| Tinyx X11 demo | M2 caches + M3 USB HID + M4 rootfs + M8 /dev/fb0 + new evdev shim + new usbmouse driver | public graphical demo (xclock, xeyes, twm + st + psh) |

---

## 4. Critical path

The single ordered chain that determines minimum project duration to
v1.0. Each step cites its source plan.

1. **Stage-1 Phase A: I-cache enable.** Almost-zero risk, hours of
   work. Source: [cache-mmu-smp-impl.md §2 Phase A](cache-mmu-smp-impl.md).
2. **Stage-1 Phase B: D+I cache enable** with full RES1 SCTLR
   baseline, SMPEN, A72 erratum 859971, MMIO XN/PXN hygiene. **Highest
   single risk in the project** (TD-04 may resurface). Source:
   [cache-mmu-smp-impl.md §2 Phases B–E](cache-mmu-smp-impl.md). Until
   this lands, every other subsystem under-performs by 10–30×.
3. **Mailbox driver refactor** from plo into kernel-internal helper.
   Source: [rtc-thermal-power-impl.md §2](rtc-thermal-power-impl.md).
   Unblocks thermal, OTP/MAC, GPU mode-set, expgpio.
4. **GIC-400 hardening + Generic Timer audit on SMP4.** Source:
   [scope-pi4-uncovered.md §2.4, §2.12](../research/scope-pi4-uncovered.md).
   Bundled with Stage-3 SMP work.
5. **Stage-3 SMP cores 1–3.** Requires Phase B caches. Source:
   [cache-mmu-smp-impl.md §4](cache-mmu-smp-impl.md).
6. **SDHCI/EMMC2 storage driver.** PIO-mode acceptable for v1.0.
   Source: [scope-pi4-uncovered.md §2.1](../research/scope-pi4-uncovered.md).
7. **FAT + ext2 filesystem integration on aarch64-rpi4b.** Source:
   [scope-pi4-uncovered.md §2.2](../research/scope-pi4-uncovered.md).
8. **SD-boot integration.** Boot from SD without netboot crutches.
   Source: [scope-pi4-uncovered.md §2.16](../research/scope-pi4-uncovered.md).
9. **Userspace on real hardware**: psh + coreutils sweep on aarch64,
   USB HID keystrokes feeding it. Sources:
   [usb-xhci-impl.md §6 Phases 3–4](usb-xhci-impl.md);
   [scope-pi4-uncovered.md §2.20](../research/scope-pi4-uncovered.md).
10. **CI loop** on real hardware. Source:
    [scope-pi4-uncovered.md §2.21](../research/scope-pi4-uncovered.md).

Any slip on **Phase B (step 2)** moves the whole project right by the
same amount. Mitigations are in §8 below.

---

## 5. Parallel work streams

Once the critical-path chokepoints (Phase B caches; mailbox refactor;
GIC hardening) land, the remaining work fans out into mutually
independent streams.

| Stream | Starts after | Deliverables | Plan refs |
|---|---|---|---|
| **A. Storage** | Phase B + clock-mgr | SDHCI driver, FAT/ext2 wiring, SD-boot integration | scope §2.1, §2.2, §2.16 |
| **B. USB completion** | (already in flight) | xhci_init steps 2.4–2.8, HID keys end-to-end, then Hub + MSC class | usb-xhci §2, §6, §8 |
| **C. Display** | mailbox + Phase B | fbcon library extract, `/dev/fb0` server, dynamic mode set, dual-display | gpu-vc6 §6 Phases 1–4 |
| **D. Networking** | GIC + lwIP enable | GENET MMIO + PHY + DMA + IRQ + DHCP/ICMP/TCP | ethernet-genet §8 |
| **E. GPIO + headerable peripherals** | GIC | GPIO Tier 1–3, then I²C BSC1, then SPI0/PWM | gpio-pinctrl §7; scope §2.6–2.8 |
| **F. Power/Telemetry** | mailbox | thermal `/dev/thermal`, watchdog `/dev/watchdog0`, poweroff via partition-63 | rtc-thermal §6 Phases 2–4 |
| **G. SMP/Memory** | Phase B | Stage-2 4 GiB DRAM, Stage-3 SMP4, broadcast-TLBI hygiene | cache-mmu §3, §4 |
| **H. DTB / SKU matrix** | (none — independent) | DT discovery layer, RAM-size auto-detect, Pi 400 support | scope §2.14, §2.15 |
| **I. CI / lab automation** | userspace on HW | self-hosted runner, hardware power relay, per-commit boot test | scope §2.21 |
| **L. Userspace demo apps** | USB HID (M3) | psh applets sweep, ports.yaml + lua REPL, optional coremark + small game | userspace-demo-apps |
| **J. WiFi** (post-v1.0) | mailbox + GPIO + GENET shakedown + SDIO | bwfm port, WPA2, lwIP integration | wifi-bcm43455 §6 |
| **K. Bluetooth** (post-v1.0) | mailbox + expgpio + 2nd pl011-tty | HCI server, .hcd patchram, nimBLE port | bluetooth-bcm43455 §6 |
| **M. Tinyx X11 demo** (stretch) | M2 caches + M3 USB HID + M4 rootfs + M8 /dev/fb0 | PR phoenix-rtos-ports#82 cherry-picked + aarch64 build, evdev shim, usbmouse driver, X11 assets, twm + st + psh-in-window | tinyx-x11-demo |

With one full-time engineer, streams must serialise on
calendar time but can interleave by tier — e.g. ship Storage
Phase 1 (driver skeleton) → switch to USB Phase 1 → switch to
Display Phase 1, etc. With 2–3 engineers, A+B+C run truly parallel
once the chokepoints clear.

---

## 6. Milestone roadmap

Suggested progression from current state to v1.0 and beyond. Each
milestone has success criteria (observable, gateable by the existing
UART-summariser harness), primary deliverable, plans involved,
prereqs, and a calendar duration estimate at 1 dev-FTE.

### M1 — Bring-up baseline (current)

- **Status:** mostly DONE. EL drop landed; kernel reaches `(psh)%`
  on real hardware; HDMI banner renders; USB host phase 1 working.
- **Plans:** [cache-mmu-smp-impl.md §1](cache-mmu-smp-impl.md);
  [usb-xhci-impl.md §1](usb-xhci-impl.md).
- **Open subtasks:** xhci_init phase 1 cleanup; cycle stability.

### M2 — Stage 1 cache enable

- **Success:** `dd if=/dev/zero ...` shows ≥ 1 GB/s; `(psh)%` arrives
  visibly faster; TD-04 NC override removed (or kept for one syspage
  page only with documented justification).
- **Deliverable:** `_init.S` walks SCTLR_EL1.{M,C,I}=1 with full RES1
  baseline, SMPEN=1, errata 859971 applied.
- **Plans:** [cache-mmu-smp-impl.md §2 all phases](cache-mmu-smp-impl.md).
- **Prereqs:** M1.
- **Duration:** 1–3 weeks; Phase A is hours, Phase B carries the
  TD-04 risk band (cache-mmu §9).

### M3 — USB keyboard end-to-end

- **Success:** Plug a USB-2 keyboard; type `a`; UART and HDMI both
  echo `a`; cleanup phase complete (debug markers removed).
- **Deliverable:** xhci_init steps 2.4–2.8 verified; usbkbd insertion
  fires; libtty path validated; Phase 5 cleanup committed.
- **Plans:** [usb-xhci-impl.md §6 Phases 1–5](usb-xhci-impl.md).
- **Prereqs:** M1; not gated on M2 (works at caches-off speed).
- **Duration:** 1–2 weeks of bench-iteration cycles
  (usb-xhci §10).

### M3.5 — Userspace demo experience (Tier A + Tier B)

- **Success:** at the `(psh)%` prompt on real Pi 4 the user can type
  `ls /`, `ps`, `cat /etc/platform`, `echo hi` and see correct output;
  `lua` launches an interactive REPL; `print(2^32+1)` returns
  `4294967297`; `os.exit(0)` returns to psh cleanly.
- **Deliverable:** `_projects/aarch64a72-generic-rpi4b/ports.yaml`
  with at least `lua` enabled; per-project `luaconf_local.h` if
  required; psh applet hardlinks verified for the rpi4b path; small
  `rootfs-overlay/etc/rc.psh` with a one-screen ANSI banner.
  Optional: `coremark` for the post-M2 speed-up showcase, one
  small ANSI game (~200–300 LoC) under `phoenix-rtos-utils/games/`.
- **Plans:** [userspace-demo-apps.md §5](userspace-demo-apps.md).
- **Prereqs:** M3 (USB HID end-to-end). Soft prereq M2 for comfort
  (lua works pre-M2, just slow).
- **Duration:** Tier A ~1 day, Tier A+B ~1.5 weeks
  (userspace-demo-apps §8). Off the critical path — runs as Stream L
  in parallel with M4–M9 work.

### M4 — Persistent boot + filesystems

- **Success:** Boot Pi from SD card with no netboot; `mount /dev/sda1
  /boot` succeeds; `mount /dev/sda2 /` succeeds; readback survives
  power-cycle.
- **Deliverable:** SDHCI/EMMC2 driver (PIO mode); FAT + ext2 wired
  into image; clock-manager mailbox tags integrated; rootfs pivot.
- **Plans:** scope §2.1, §2.2, §2.5, §2.16; spawned research +
  plan agents per scope §6.
- **Prereqs:** M2 (cache for usable I/O bandwidth); mailbox driver
  from rtc-thermal §2.
- **Duration:** 5–7 weeks (scope §4 MUSTs subtotal).

### M5 — Network online

- **Success:** `eth0 up`, DHCP lease obtained, `ping -c100` 0 % loss,
  TCP connect to a host echo server succeeds.
- **Deliverable:** bcmgenet driver, MDIO + BCM54213PE PHY init, GIC
  SPI 157/158 wired, lwIP netif, MAC from mailbox-OTP.
- **Plans:** [ethernet-genet-impl.md §8 Phases 1–5](ethernet-genet-impl.md).
- **Prereqs:** M2 (preferred for line rate; works without);
  GIC hardening; mailbox for MAC.
- **Duration:** 4–6 sessions (ethernet-genet §11).

### M6 — SMP

- **Success:** all 4 cores online; `nCpusStarted == 4`; spinlock
  stress passes; broadcast-TLBI works; per-core GIC-CPU init runs.
- **Deliverable:** Stage-3 SMP entry path; per-core stack reservation;
  GICv2 per-core init in `_other_core_virtual`.
- **Plans:** [cache-mmu-smp-impl.md §4](cache-mmu-smp-impl.md).
- **Prereqs:** M2 (Phase B caches, hard requirement for LDAXR/STLXR).
- **Duration:** 1.5–3 weeks (cache-mmu §9).

### M7 — 4 GiB DRAM

- **Success:** psh `meminfo` reports ≥ 3.5 GiB usable; 3 × 1 GiB
  malloc loop passes pattern check.
- **Deliverable:** DTB reserved-memory parsing, multi-block TTBR1
  high-VA mappings, page allocator extension to full physical map.
- **Plans:** [cache-mmu-smp-impl.md §3](cache-mmu-smp-impl.md).
- **Prereqs:** M2.
- **Duration:** 1–2 weeks.

### M8 — HDMI dynamic + GPU mailbox

- **Success:** `/dev/fb0` registered; userspace mmaps and draws a
  gradient; mode-set 1024×768 ↔ 1920×1080 round-trip works; pan
  buffer flips work.
- **Deliverable:** `rpi4-vc6-fb` server (Tier 1); fbcon library extracted;
  `vc6-mbox` shared transport; Linux-fbdev-shaped devctl ABI.
- **Plans:** [gpu-vc6-impl.md §6 Phases 1–4](gpu-vc6-impl.md).
- **Prereqs:** M2 (gradient infeasible without caches); mailbox refactor.
- **Duration:** 7–13 dev-days (gpu-vc6 §9 Tier 1 total).

### M9 — GPIO + watchdog/thermal/poweroff

- **Success:** `gpioget gpiochip0 21` returns level; LED blinks
  via userspace; button press fires IRQ; `cat /dev/thermal` returns
  35–60 °C; `reboot` syscall actually reboots; `wdog_trigger_poweroff`
  parks Pi at red-LED.
- **Deliverable:** `rpi4-bcm2711-gpio` server Tiers 1–3; rtc-thermal
  Phases 1–4 complete.
- **Plans:** [gpio-pinctrl-impl.md §7](gpio-pinctrl-impl.md);
  [rtc-thermal-power-impl.md §6](rtc-thermal-power-impl.md).
- **Prereqs:** mailbox (M4-shared); GIC hardening for IRQs.
- **Duration:** 2–3 weeks GPIO + 1.5 weeks rtc-thermal in parallel.

### M10 — WiFi (post-v1.0)

- **Success:** WPA2 association; DHCP lease via WiFi; ping host on
  WLAN.
- **Plans:** [wifi-bcm43455-impl.md §6 P0–P7](wifi-bcm43455-impl.md).
- **Prereqs:** M5 (GENET shakedown), mailbox, GPIO.
- **Duration:** 3–6 calendar months (wifi §11).

### M11 — Bluetooth (post-v1.0)

- **Success:** BLE scan reports a known peer; (Tier 5) GATT
  client/server interop with `bluetoothctl`/`nRF Connect`.
- **Plans:** [bluetooth-bcm43455-impl.md §6 Phases A–E](bluetooth-bcm43455-impl.md).
- **Prereqs:** mailbox+expgpio, second pl011-tty, GPIO 30–33 mux.
- **Duration:** 4–5 months to Tier 4, 1–2 more to Tier 5
  (bluetooth §10).

### M12 — Production polish

- **Success:** CI loop runs the netboot+SD-boot cycle for every
  commit on `main`; soak harness runs > 24 h; doc set published.
- **Plans:** scope §2.21, §2.20, §2.22, §2.23.
- **Prereqs:** M4 + M5 + M9.
- **Duration:** 4–6 weeks.

### M13 — tinyx X11 graphical demo (stretch / post-v1.0)

- **Success:** Pi 4 boots from SD card; `Xfbdev :0` reaches its
  event loop; first X client (xeyes / xclock / xlogo) is visible
  on HDMI; pressing keys on a plugged-in USB keyboard produces
  visible X events. Stretch: twm + st + `psh` running inside an X
  terminal window — the public-facing "this is a real desktop"
  asset.
- **Deliverable:** PR phoenix-rtos/phoenix-rtos-ports#82 cherry-
  picked and built for `aarch64-rpi4b`; new evdev shim under
  `phoenix-rtos-devices/input/evdev/` (or as a new endpoint inside
  `usbkbd`); new `phoenix-rtos-devices/usb/usbmouse/` driver
  (mirrors `usbkbd` structure); X11 binaries + bitmap fonts
  packaged into the SD-card rootfs.
- **Plans:** [tinyx-x11-demo.md](tinyx-x11-demo.md). Cross-cuts
  [gpu-vc6-impl.md §6 Phase 2](gpu-vc6-impl.md) (`/dev/fb0` ABI
  Tier 1) and [usb-xhci-impl.md §6 Phases 3–4](usb-xhci-impl.md)
  (USB HID keyboard).
- **Prereqs:** M2 (caches — uncached pixman is unwatchable),
  M3 (USB HID keys), M4 (persistent rootfs for ~10 MB X assets),
  M8 (`/dev/fb0` Tier 1).
- **Duration:** 4–8 weeks once prereqs land (best 3.5 wk, worst
  ~12 wk; see [tinyx-x11-demo.md §8](tinyx-x11-demo.md)). Most of
  the work is plumbing — evdev shim, USB mouse driver, image
  packaging — not tinyx itself.

---

## 7. Total effort estimate

Summed from each plan's "Effort estimate" section and scope §4.

| Component | Best | Likely | Worst |
|---|---|---|---|
| Stage-1 cache (cache-mmu §9) | 2 d | 1–2 wk | 3 wk |
| Stage-2 4 GiB (cache-mmu §9) | 1 wk | 1.5 wk | 2 wk |
| Stage-3 SMP (cache-mmu §9) | 1.5 wk | 2 wk | 3 wk |
| USB phase 1–5 (usb-xhci §10) | 3 d | 1–2 wk | 3 wk |
| USB phase 6 MSC (usb-xhci §8) | — | 2–3 wk | 4 wk |
| GPU Tier 1 (gpu-vc6 §9) | 7 d | 10 d | 13 d |
| GENET (ethernet-genet §11) | 4 sess | ~1 wk | 2 wk |
| GPIO Tier 0–3 (gpio §10) | 2 wk | 2.5 wk | 3 wk |
| Mailbox + WD + thermal + PO (rtc §9) | 7 d | 8 d | 12 d |
| Userspace demo Tier A+B (userspace-demo-apps §8) | 5 d | ~1.5 wk | 3 wk |
| **Subtotal in-flight plans** | **~7 wk** | **~12 wk** | **~19 wk** |
| Scope MUSTs (SDHCI+FS+UI+CI; scope §4) | 11.5 wk | ~13 wk | 14.5 wk |
| Scope SHOULDs (DMA+I²C+DTB+USB-Hub+MSC+...) | 18.5 wk | ~22 wk | 25.5 wk |
| Scope NICEs (SPI+PWM+telemetry+crashdump) | 4 wk | 4 wk | 4 wk |
| WiFi (wifi §11) | 3 mo | 4–5 mo | 6 mo |
| Bluetooth (bt §10) | 4 mo | ~5 mo | 7 mo |
| Tinyx X11 demo (tinyx §8) | 3.5 wk | 5–6 wk | 12 wk |

**v1.0 (in-flight + scope MUSTs only):** ~18.5–33.5 dev-weeks ≈
**4–8 months at 1 FTE** (the demo-apps row adds ~1.5 wk likely),
with TD-04 contingency adding 20–40 % (scope §4 closing note).

**v1.0 + SHOULDs:** ~37–59 dev-weeks ≈ **9–15 months at 1 FTE**.

**Calendar parallelism:**

- **N=1 dev:** v1.0 in 4–7 months, v1.0+SHOULDs in 9–14 months.
- **N=2 devs:** Streams A+B+C+D parallelise post-Phase-B; v1.0 in
  ~3–4 months calendar, v1.0+SHOULDs in ~5–8 months.
- **N=3 devs:** Add Streams E+F+G+H; v1.0 in ~2.5–3.5 months,
  v1.0+SHOULDs in ~4–6 months. Diminishing returns past N=3 because
  M2 (Phase B caches) is on a single hot file (`_init.S`) and the
  hardware-iteration loop serialises.

**Tinyx X11 demo (M13)** is a post-v1.0 stretch and is *not*
included in either v1.0 figure. Adds 4–8 calendar weeks once its
prereqs (M2 + M3 + M4 + M8) land — see
[tinyx-x11-demo.md §7-§8](tinyx-x11-demo.md). Most cost is plumbing
(evdev shim, USB mouse driver, asset packaging), not tinyx itself.

WiFi and Bluetooth add 6–11 months even with two engineers in
parallel — they are post-v1.0 multi-month commitments.

---

## 8. Top risks (ranked)

| # | Risk | P | Blast radius | Mitigation |
|---|---|---|---|---|
| 1 | **Phase B cache enable still hangs after EL drop** — TD-04 was a class-of-problem on BCM2711 cache coherency, only one failure mode known fixed | M | Whole project, every milestone shifts right | Stage-1 has a ladder of fallbacks (cache-mmu §8): I-only first, then C+I, then C-only, then NC override only on syspage source page. Re-run TD-04 probe (cache-mmu §10 Q1). Have rollback manifest pre-staged. |
| 2 | **VL805 OTG / PCIe edge cases** — xhci_init steps 2.4–2.8 may surface scratchpad-buffer or MSI quirks | M | M3 slips weeks; usb-xhci §11 calls out scratchpad as open question | Per-step debug markers already in tree (usb-xhci §1); decode HCSPARAMS2 properly; allocate scratchpad before RS if MaxScratchpadBufs > 0 |
| 3 | **BTstack license fit** — non-commercial-only; LIC-BT-01 is a hard release blocker (bluetooth §9) | H if Classic BT desired | Bluetooth release blocked | Adopt nimBLE for public release; use BTstack only as internal cross-check during bring-up |
| 4 | **Mailbox kernel-vs-userspace decision regret** — kernel-internal recommended (rtc §2), but if a userspace-only consumer surfaces post-impl, retrofit is real cost | L | Refactor cost ~1 week | Mirror as thin libphoenix passthrough at Phase 1.5 (rtc §2 userspace pass-through) |
| 5 | **GIC SPI off-by-32 bug** — first non-timer SPI driver on Pi 4; ethernet-genet §11 risks list this; gpio §11 calls out same | M | One debug session per first-driver-using-IRQ | Half-day spike before each new IRQ-driven driver; instrument GICD_ITARGETSRn programming |
| 6 | **GPU Tier 2 license risk** — direct HVS knowledge originates in GPL `drivers/gpu/drm/vc4` (gpu-vc6 §10 Q5) | H | Tier 2 may be unshippable | Stay at Tier 1 for v1.0; Tier 2 gated on counsel review |
| 7 | **DMA framework scope creep** — scope §2.3 bills 4–6 weeks for Phoenix-wide DMA API design | M | SHOULD slips; performance impact only | Defer; ship PIO-mode SDHCI for v1.0 and accept perf ceiling |
| 8 | **WiFi NVRAM blob silent dead-radio** — wifi §12 calls out Pi-4 specific NVRAM as the marginal-RF risk | H within WiFi only | M10 slips weeks | Use Pi-Foundation Pi-4 NVRAM verbatim; bench-RF-test before any NVRAM mod |
| 9 | **Stage-3 broadcast TLBI** — secondary cores need SMPEN + per-core GIC-CPU init; cache-mmu §8 risks list it | M | M6 slips | Verify SMPEN per-core in secondary entry; mirror ZynqMP `nCpusStarted` pattern |
| 10 | **A72 erratum 859971** — pre-existing CPUACTLR_EL1 stanza is currently disabled; cache-mmu §2 Phase C needs to re-enable | L | Latent; unlikely to bite at Stage-1 but a debug rabbit-hole if it does | Re-enable conditionally on `__TARGET_AARCH64A72`; document in `_init.S` lines 313–320 |

---

## 9. Decision points for the human integrator

Forks where a person needs to decide before code lands. Each is
actionable now or imminently.

1. **Mailbox driver: kernel-internal vs userspace server.**
   Recommendation in [rtc-thermal-power-impl.md §2, §10 Q1](rtc-thermal-power-impl.md):
   kernel-internal with a thin libphoenix passthrough later.
   **Decide before M4 starts.**

2. **WiFi stack choice: bwfm port vs Pico-SDK cyw43-driver vs WHD
   salvage.** Recommendation in
   [wifi-bcm43455-impl.md §7](wifi-bcm43455-impl.md): NetBSD bwfm.
   **Decide before M10 even researches.**

3. **BT stack choice: nimBLE (Apache 2.0) vs BTstack (non-commercial
   without paid licence).** Recommendation in
   [bluetooth-bcm43455-impl.md §5, §9](bluetooth-bcm43455-impl.md):
   nimBLE for public release. **Decide before any BTstack source
   enters the tree, even privately.**

4. **GPU tier ceiling: stop at Tier 1 (mailbox KMS) or aim for
   Tier 2 (direct HVS scanout)?** Recommendation in
   [gpu-vc6-impl.md §10 Q1, Q5](gpu-vc6-impl.md): stop at Tier 1
   for v1.0; Tier 2 only after counsel review of GPL exposure.

5. **DTB consumption depth.** Hardcode Pi-4-specific tables vs full
   `compatible`-style matching? Recommendation:
   [scope §2.14](../research/scope-pi4-uncovered.md): hardcode for
   v1.0, design for extensibility.

6. **Public ABI shape for `/dev/fb0`, `/dev/gpiochip0`,
   `/dev/watchdog0`.** Linux-fbdev-shaped (Mesa-ready),
   libgpiod-compatible (upstream-tooling-ready), Linux-watchdog-ish
   ioctls. Recommendations:
   - fbdev: yes, match Linux numbers
     ([gpu-vc6 §4](gpu-vc6-impl.md));
   - gpiochip: ship Zynq-style Tier 1–3, decide libgpiod after first
     real consumer ([gpio §11 first open question](gpio-pinctrl-impl.md));
   - watchdog: skip Linux ioctls for now; zero-userspace driver
     covers operational need ([rtc §10 Q2](rtc-thermal-power-impl.md)).

7. **Pi 4 SKU matrix for v1.0.** Pi 4B 4 GB only, or 1/2/4/8 GB,
   plus Pi 400 / CM4? Recommendation in
   [scope §2.15](../research/scope-pi4-uncovered.md): 4 GB MUST,
   1/2/8 GB SHOULD post-DTB-layer, Pi 400 NICE, CM4 OUT-OF-SCOPE.

8. **CI hardware setup.** Self-hosted runner with power relay vs
   shared lab Pi. **Decide before M12 scoping.**

9. **TD-04 NC-override fate.** After Phase B caches: re-run TD-04
   probe (cache-mmu §10 Q1). If clean, remove TD-04 hack. If not,
   keep `NC_ATTRS = 0x707` for syspage source page only and document
   why caches still don't help there.

10. **Tier-D demo choice: lua, micropython, or both?**
    Recommendation in
    [userspace-demo-apps.md §9 Q1–Q2](userspace-demo-apps.md): ship
    lua for v1.0 (architecture-agnostic, ~250 KB, MIT); defer
    micropython aarch64 enablement to post-v1.0. Decide before M3.5
    starts.

11. **busybox in the public demo image?** Recommendation in
    [userspace-demo-apps.md §5.8](userspace-demo-apps.md): no — psh
    + lua already cover the demo surface and busybox is GPL-2.0-only,
    which would extend a per-binary license boundary into the public
    image. Keep busybox available for internal soak only. Decide
    before M3.5 ships.

12. **Tinyx vs Wayland for the v1 graphical demo.** Recommendation
    in [tinyx-x11-demo.md §9 Q4](tinyx-x11-demo.md): tinyx, because
    PR `phoenix-rtos-ports#82` already exists as the implementation
    vehicle and Wayland on Phoenix would be a from-scratch port. A
    Wayland future is not foreclosed — `/dev/fb0` Tier 1 in
    [gpu-vc6-impl.md](gpu-vc6-impl.md) is shape-compatible with
    a future Weston backend — but the decision **for v1.0 is tinyx**.
    Decide formally before M13 scoping.

13. **Tinyx input-stack design: extend usbkbd or new evdev shim?**
    Two designs in [tinyx-x11-demo.md §4.2](tinyx-x11-demo.md);
    plan recommends the standalone evdev shim
    (`phoenix-rtos-devices/input/evdev/`) for separation of concerns
    and reuse with a future usbmouse driver. Decide in M13 Phase 4
    kickoff.

14. **Tinyx window manager: twm vs dwm.** twm is MIT-licensed and
    classical; dwm is GPL-2.0 and one C file. Public-image release
    license boundaries argue for twm. See
    [tinyx-x11-demo.md §9 Q3](tinyx-x11-demo.md). Decide before M13
    Phase 7.

---

## 10. Pi 5 / RP1 forward-compatibility assessment

Per [scope §2.26](../research/scope-pi4-uncovered.md), Pi 5 moves all
GPIO/UART/I²C/SPI/USB/Ethernet/SDIO into the RP1 chiplet behind a
PCIe 2.0 ×4 link. RP1 owns these; the BCM2712 SoC keeps
ARMv8 cores, GIC, mailbox, GPU/HDMI, and the on-package interconnect.

| Subsystem | Reusable on Pi 5? | Notes |
|---|---|---|
| EL2→EL1 drop, MMU, cache, SMP | **Mostly reusable** | BCM2712 is also Cortex-A76; sysreg semantics same; cache-coherency may differ; expect a probe pass per cache-mmu §10 |
| GIC-400 → GIC-600 | **Partial** | Distributor/redistributor differs; driver re-write small |
| Mailbox / firmware property tags | **Reusable** | Same VC tag protocol largely preserved |
| GPU/VC6 mailbox-KMS (Tier 1) | **Reusable** | Mailbox tags work; physical mode set may differ |
| GPU/VC6 direct HVS (Tier 2) | **Throwaway** | BCM2712 redesigned HDMI again (gpu-vc6 §10 Q6) |
| GENET Ethernet | **Reusable** | Pi 5 keeps GENET-shaped Ethernet but behind RP1 (scope §2.26) |
| VL805 xHCI | **Throwaway** | Pi 5 USB moves into RP1 |
| GPIO/pinctrl BCM2711 | **Throwaway** | Different IP in RP1 |
| I²C/SPI/PWM | **Throwaway** | Different IP in RP1 |
| BCM2711 PM/watchdog | **Likely throwaway** | New PM block on BCM2712 |
| BCM43455 WiFi/BT (SDIO+UART) | **Mostly reusable on Pi 4 / 5 / 0W2** | Same chip family; combo radio still SDIO+UART |
| SDHCI EMMC2 | **Throwaway** | SDIO moves into RP1 |
| FAT/ext2 fs, lwIP, libphoenix | **Reusable** | Hardware-agnostic |

Strategic implication: Pi-4-specific pinctrl/I²C/SPI/PWM/USB/SDHCI
work is **single-board-life-cycle** code. Plan for replacement when
Pi 5 work begins. Mailbox, mode-set Tier 1, GENET, libphoenix, and
the AArch64 plumbing carry forward.

---

## 11. Suggested sequencing of next 5 sessions

Concrete actions assuming current state (M1 done, M3 in flight at
xhci_init step 2.x). Each session has 1–2 deliverables and a clear
acceptance criterion.

**Session N+1 — Stage-1 Phase A: I-cache enable.**
- Deliverable: `_init.S` lines 410–415 patched to `(1<<0) | (1<<12)`
  with `ic iallu; dsb nsh; isb` prelude; SCTLR_EL1 baseline replaced
  with derived RES1 (cache-mmu §2 Phase A, §5 row "1A").
- Acceptance: full netboot cycle reaches `(psh)%`; existing markers
  unchanged; manifest snapshotted.

**Session N+2 — Stage-1 Phase B: D+I cache enable.**
- Deliverable: SCTLR set bits = M|C|I in one MSR; SMPEN re-enabled;
  TLBI moved adjacent to flip; `_inval_dcache_range` extended to
  cover all PMAP_COMMON_* + syspage source page (cache-mmu §2 Phase
  B, §5 rows "1B").
- Acceptance: `(psh)%` arrives noticeably faster; `dd if=/dev/zero
  bs=1M count=64` shows ≥ 1 GB/s; TD-04 NC-override fate decided
  (re-run E2 probe per cache-mmu §10 Q1); manifest snapshotted.
- **Fallback plan if hang**: drop to M-only, then C-only, then add
  errata 859971, then mailbox-quiesce VPU; iterate per cache-mmu §8.

**Session N+3 — USB keystrokes end-to-end + cleanup.**
- Deliverable: xhci_init steps 2.4–2.8 verified (per-step markers
  for the silent ones); `usbkbd_handleInsertion` fires; type `a` →
  echo on UART and HDMI; usb-xhci §5 cleanup pass committed
  (debug() cascade removed, fprintf companions kept).
- Acceptance: full keystroke loop visible on captured UART log; clean
  diff in pcie/xhci/hcd.c; manifest snapshotted.

**Session N+4 — Mailbox refactor + thermal Phase 1+3.**
- Deliverable: `hal/aarch64/generic/rpi-mailbox.{c,h}` refactored
  from plo (rtc §2); `_hal_init` prints firmware revision tag
  `0x00000001`; `phoenix-rtos-devices/sensors/rpi4-thermal/` created
  with `/dev/thermal` (rtc §4, §6 Phases 1+3).
- Acceptance: UART log shows plausible firmware-rev value;
  `cat /dev/thermal` returns 35000–60000; manifest snapshotted.

**Session N+5 — Watchdog driver + poweroff hooks.**
- Deliverable: `hal/aarch64/generic/rpi-pm.{c,h}` with
  `_hal_systemReset` and `_hal_systemHalt`; `reboot` syscall path
  wired (rtc §3, §5, §6 Phase 2+4).
- Acceptance: `reboot` reboots without external power-cycle; UART
  shows `_hal_init` twice across one run; partition-63 path parks
  Pi at red-LED-only state; manifest snapshotted; M9 partial done.

After session N+5, branching is justified: Storage stream A can
spawn under a forward-research agent (`docs/research/sdhci-emmc2.md`
per scope §6 task 1), in parallel with continuing GPIO and GENET
work in the main thread.

---

## 12. Multi-agent operating model

[`AGENTS.md`](../../AGENTS.md) does not currently carry a named
"Multi-Agent Working Architecture" section, but the project's
operating model is implicit in the rules already in place
(reading-list discipline, single-active-step, sibling-repo separation,
manifest-driven rollback,
[`docs/unattended-agent-mode.md`](../unattended-agent-mode.md)).

Operating principles for executing this master plan:

1. **One main thread per active critical-path milestone.** M2 (Phase
   B caches) and M3 (USB keystrokes) cannot be parallelised across
   two agents in the same checkout — they touch the same files
   (`_init.S` for caches; xhci.c for USB). Single agent on critical
   path until that milestone closes.

2. **Worktree-per-stream for non-critical-path work.** Phoenix's
   coordination repo is git-worktree friendly (current session
   itself runs in `.claude/worktrees/dazzling-joliot-cd9889`).
   Streams D (GENET), E (GPIO), F (rtc-thermal) touch disjoint
   sibling repos and can run in parallel worktrees once mailbox +
   GIC chokepoints clear.

3. **Forward-research agents** for scope-listed items not yet
   planned. Per [scope §6](../research/scope-pi4-uncovered.md), spawn
   in priority order: SDHCI research → SDHCI plan → DTB consumption
   → fs integration → DMA framework → userspace audit → I²C plan →
   CI design. Each is a 1–3 session research task that can run
   asynchronously while the main thread executes plans already
   landed.

4. **Hardware-test gating.** All milestone "Acceptance" criteria
   are observable on UART + HDMI through the existing
   `scripts/rebuild-rpi4b-fast.sh` →
   `scripts/capture-rpi4b-uart.sh` →
   `scripts/summarize-rpi4b-uart-log.py` loop. CI bot or human runs
   the loop; agent does not block on it. Failure → snapshot the
   manifest, restore previous green via
   `scripts/restore-integration-state.sh`, and diagnose offline.

5. **Manifest discipline at every milestone boundary.** Per
   [`AGENTS.md`](../../AGENTS.md) and `CLAUDE.md`'s rollback section,
   every milestone produces a `manifests/<date>-<milestone>.md`
   recording all sibling SHAs. Cross-milestone parallelism requires
   that two streams not modify the *same* sibling repo's branch
   simultaneously; parallel work uses per-stream branches in each
   sibling.

6. **License-sensitive code stays in named branches.** BTstack and
   any GPL-derived GPU Tier 2 code must live in a branch flagged
   `LIC-` per [bluetooth §9](bluetooth-bcm43455-impl.md), with
   counsel sign-off recorded in
   [`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`](../TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
   before any merge to a public branch.

7. **Single source of truth for "what's the active step"**:
   [`tracking/current-step.md`](../../tracking/current-step.md). All
   parallel streams update it on entry/exit so the orchestrator can
   see at a glance which milestones are in flight.

---

## Cross-reference index

Source plans cited:

- [`docs/plans/cache-mmu-smp-impl.md`](cache-mmu-smp-impl.md)
- [`docs/plans/usb-xhci-impl.md`](usb-xhci-impl.md)
- [`docs/plans/gpu-vc6-impl.md`](gpu-vc6-impl.md)
- [`docs/plans/ethernet-genet-impl.md`](ethernet-genet-impl.md)
- [`docs/plans/wifi-bcm43455-impl.md`](wifi-bcm43455-impl.md)
- [`docs/plans/bluetooth-bcm43455-impl.md`](bluetooth-bcm43455-impl.md)
- [`docs/plans/gpio-pinctrl-impl.md`](gpio-pinctrl-impl.md)
- [`docs/plans/rtc-thermal-power-impl.md`](rtc-thermal-power-impl.md)
- [`docs/plans/userspace-demo-apps.md`](userspace-demo-apps.md)
- [`docs/plans/tinyx-x11-demo.md`](tinyx-x11-demo.md)
- [`docs/research/scope-pi4-uncovered.md`](../research/scope-pi4-uncovered.md)

Boot rules and operating policy:

- [`AGENTS.md`](../../AGENTS.md)
- [`CLAUDE.md`](../../CLAUDE.md)
- [`docs/status.md`](../status.md)
- [`tracking/current-step.md`](../../tracking/current-step.md)
- [`docs/TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md`](../TEMPORARY-FIXES-AND-FUTURE-CLEANUP.md)
- [`docs/unattended-agent-mode.md`](../unattended-agent-mode.md)
- [`docs/code-quality-and-upstreaming.md`](../code-quality-and-upstreaming.md)
