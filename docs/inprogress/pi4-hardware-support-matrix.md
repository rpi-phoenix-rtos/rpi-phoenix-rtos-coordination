# Phoenix-RTOS Raspberry Pi 4 (BCM2711) — Hardware Support Matrix

**Updated:** 2026-06-05. Canonical "where are we" reference for the Pi 4 port.
One row per peripheral/subsystem. For narrative gap analysis see
`docs/knowledge/scope-pi4-uncovered.md`; for live progress see `docs/inprogress/status.md`.

**Status legend:**
- ✅ **done** — works on hardware, committed, validated.
- 🟡 **partial** — usable but incomplete / a known sub-feature missing.
- 🔬 **groundwork** — mechanism proven, but a deliberate decision/step remains.
- ⏸ **attended** — implementable but deferred to a human-attended session
  (boot-risk, statistical-regression, or needs a screen/scope/bench rig).
- ⛔ **blocked** — stuck on an external dependency (datasheet/JTAG/firmware/HW).
- ⬜ **not started**.

| Subsystem | Status | Evidence / entry point | Remaining |
|---|---|---|---|
| CPU bring-up, EL2→EL1, MMU | ✅ done | boots to userspace | caches still globally off (TD-16) |
| SMP (4 cores) | 🟡 partial | 4-core enum works | scheduler is **cpu0-only**; CNTV-on-secondary breaks primary (`project_smp_d7_d8_findings`) |
| Generic Timer | ✅ done | scheduler tick / delays | — |
| Interrupts (GIC-400) | ✅ done | GENET/USB/SD IRQs live | — |
| PL011 UART console | ✅ done | primary console + klog mirror | TD-14 two-owner UART polish (#127) |
| VideoCore property mailbox | ✅ done | userspace (thermal/clocks/power) | kernel-internal primitive ⏸ (for WiFi/BT/DVFS) |
| HDMI framebuffer **console** (fbcon) | ✅ done | klog+psh on HDMI (Tier 0) | slow fills (caches off) |
| HDMI framebuffer **device** `/dev/fb0` | 🟡 partial | device LANDED + HW-validated netboot (#148): read/write + `RPI4FB_GETMODE` devctl, `video/rpi4-fb/` | attended (#149): fbdev `FBIOGET_*` veneer (Tiny-X), true `mmap(fd,0)` kernel backing, drawing/display-ownership |
| GENET Ethernet | ✅ done | Tier 5, IRQ-driven, ping ~0.9 ms | — |
| lwIP / DHCP / ICMP / UDP | ✅ done | autonomous DHCP, diag-udp :9999 | — |
| USB host (PCIe→VL805 xHCI) | ✅ done | enum 5/5 reliable (#139) | daemon hardening #142/#143, IRQ event path #145 — all ⏸ |
| USB HID (kbd + mouse) | ✅ done | `/dev/kbd0`+`/dev/mouse0`, live keys→psh (#122/#124/#126) | — |
| USB mass storage | ⬜ not started | — | umass driver |
| PCIe RC / VL805 inbound abort (TD-10) | ⏸ attended | SError handler in (#109); abort isolated to PCIe/USB bring-up | unmask SError = boot-risk; root-cause #144 |
| SD card (EMMC2 SDHCI) | ✅ done | `/dev/mmcblk0[pN]`, PIO reads, MBR (#119) | high-throughput needs DMA |
| ext2 persistent rootfs (#120) | 🟡 partial | block reads validated; 2-part SD image built | mount-as-`/` + rc.psh pivot (attended/morning) |
| SoC thermal + throttle | ✅ done | `/dev/thermal`,`/dev/throttled` (2026-06-05) | firmware owns the trip (telemetry only) |
| Hardware RNG (RNG200) | ✅ done | `/dev/hwrng` (2026-06-05) | not yet wired to a kernel `/dev/urandom` pool |
| Watchdog / reboot / poweroff | ⏸ attended | works via diag-udp `r`/`h` (PM block #43) | productionize `_hal_systemReset` (kernel, boot-risk) |
| WiFi (BCM43455 SDIO) | ⛔ blocked | fw+NVRAM load + CR4 release were built (in the now-**deleted** diag-udp.c) | **firmware not executing** (#91, image-scan proven). NVRAM-trailer lead DISPROVEN (2026-06-07); real suspects = download/clock ordering + SDIO-core intstatus-clear + rstvec semantics. Live downloader must be reintroduced first. Needs HW/JTAG |
| Bluetooth (BCM43455 UART HCI) | ⬜ not started | plan only | needs mailbox+GPIO alt-fn + `.hcd` blob |
| GPIO / pinctrl | 🟡 partial | `/dev/gpio` read-only observer device (#150): snapshot + per-pin `RPI4GPIO_GETPIN` devctl, `gpio/rpi4-gpio/` | **outputs** (GPSET/GPCLR/fsel set) need a bench rig to validate (⏸) |
| I²C / SPI / PWM | ⬜ not started | plans exist | need GPIO alt-fn + clock-manager |
| Audio (PWM / I²S / HDMI) | ⬜ not started | plan `docs/todo/pi4-audio-impl.md` | validation needs speaker/scope (⏸) |
| RTC | ⏸ deferred | Pi 4 has no on-SoC RTC | NTP over GENET (zero-HW); or I²C HAT later |
| DMA framework | ⬜ not started | PIO everywhere today | blocks line-rate SD + audio |
| Camera (CSI-2) / DSI display | ⬜ not started | — | — |
| posixsrv / psh userspace | ✅ done | pipes, ptys, `/dev/{null,zero,urandom,full}`, interactive psh | psh has no `|` pipe parsing |

## Build / test infrastructure (✅)

- Two build variants: `rebuild-rpi4b-fast.sh --variant netboot|sd` (2026-06-05).
- Netboot loop: `test-cycle-netboot.sh` (UART + HDMI snapshots + diag-udp `--probe`).
- Network observability: diag-udp responder on :9999 — full command + `/dev`-node
  reference in **[docs/knowledge/diag-udp-reference.md](../knowledge/diag-udp-reference.md)** (`c` clocks+thermal,
  `r`/`h` reboot/halt, `g` GPIO, `V` framebuffer probe, `R` device-read smoke test,
  `D` devnodes, plus the WiFi/SDIO bring-up set).
- Deterministic rollback: `snapshot-/restore-integration-state.sh` + `manifests/`.

## What "fully supported" still needs (priority order)

1. **USB** is functionally complete (enum + HID); the remaining items (#142/#143/#144/#145)
   are *hardening/perf/root-cause* and are **attended** (statistical regression or boot-risk).
2. **ext2 rootfs** (#120) — finish mount-as-root (attended/morning).
3. **fb0 driver** — decide ABI + display ownership, then implement (attended).
4. **WiFi #91** — the one true *blocker*; firmware-execution gate needs deeper HW visibility.
5. Greenfield: DMA framework → audio/I²C/SPI/PWM; Bluetooth; GPIO full driver.

## Unattended-vs-attended note

Overnight/autonomous netboot work is restricted to **additive + deterministic-self-log +
cannot-silently-regress** items (see `feedback_unattended_scoping` memory). The ⏸ rows above are
attended precisely because their failure is either physically unrecoverable over netboot
(kernel/reboot), statistically invisible to single-boot validation (USB daemon internals), or
needs human judgement (a screen/scope/bench rig).
