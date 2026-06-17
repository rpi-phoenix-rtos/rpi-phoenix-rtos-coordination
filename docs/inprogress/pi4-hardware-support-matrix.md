# Phoenix-RTOS Raspberry Pi 4 (BCM2711) — Hardware Support Matrix

**Updated:** 2026-06-17. Canonical "where are we" reference for the Pi 4 port.
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
| CPU bring-up, EL2→EL1, MMU | ✅ done | boots to userspace; **caches ON** (SCTLR.{M,C,I}, all Normal RAM WB-cacheable) since 2026-05-17 (TD-16 RESOLVED) | residual perf lever is the *uncached GENET RX DMA pool* (Policy B, attended), not a global cache switch |
| SMP (4 cores) | 🟡 partial | 4-core enum works | scheduler is **cpu0-only**; CNTV-on-secondary breaks primary (`project_smp_d7_d8_findings`) |
| Generic Timer | ✅ done | scheduler tick / delays | — |
| Interrupts (GIC-400) | ✅ done | GENET/USB/SD IRQs live | — |
| PL011 UART console | ✅ done | primary console + klog mirror | TD-14 two-owner UART polish (#127) |
| VideoCore property mailbox | ✅ done | userspace (thermal/clocks/power) | kernel-internal primitive ⏸ (for WiFi/BT/DVFS) |
| HDMI framebuffer **console** (fbcon) | ✅ done | klog+psh on HDMI (Tier 0) | slow fills (CPU writes to the uncached fb pages; caches are globally ON) |
| HDMI framebuffer **device** `/dev/fb0` | 🟡 partial | device LANDED + HW-validated netboot (#148): read/write + `RPI4FB_GETMODE` devctl, `video/rpi4-fb/` | attended (#149): fbdev `FBIOGET_*` veneer (Tiny-X), true `mmap(fd,0)` kernel backing, drawing/display-ownership |
| GENET Ethernet | ✅ done | Tier 5, IRQ-driven, ping ~0.9 ms | — |
| lwIP / DHCP / ICMP / UDP | ✅ done | autonomous DHCP, diag-udp :9999 | — |
| USB host (PCIe→VL805 xHCI) | ✅ done | enum 5/5 reliable (#139) | daemon hardening #142/#143, IRQ event path #145 — all ⏸ |
| USB HID (kbd + mouse) | ✅ done | `/dev/kbd0`+`/dev/mouse0`, live keys→psh (#122/#124/#126) | — |
| USB mass storage | ⬜ not started | — | umass driver |
| PCIe RC / VL805 inbound abort (TD-10) | ⏸ attended | SError handler in (#109); abort isolated to PCIe/USB bring-up | unmask SError = boot-risk; root-cause #144 |
| SD card (EMMC2 SDHCI) | ✅ done | `/dev/mmcblk0[pN]`, PIO reads, MBR (#119) | high-throughput needs DMA |
| ext2 persistent rootfs (#120) | ✅ done | mounts as `/`, binaries exec from it (`ifconfig`), boots to psh stably; HW-validated SD-boot 0/10 faults. Crash root cause was a **fs pool-thread stack overflow** (8 KB default too small) — fixed by `storage_run(2, 16*_PAGE_SIZE)`, full multithreading kept, ext2 unchanged | residuals: noisy-but-recovering 50 MHz Data-CRC (signal polish), single-block-only CMD24/CMD18 (perf) |
| SoC thermal + throttle | ✅ done | `/dev/thermal`,`/dev/throttled` (2026-06-05) | firmware owns the trip (telemetry only) |
| Hardware RNG (RNG200) | ✅ done | `/dev/hwrng` (2026-06-05) | not yet wired to a kernel `/dev/urandom` pool |
| Watchdog / reboot / poweroff | ⏸ attended | works via diag-udp `r`/`h` (PM block #43) | productionize `_hal_systemReset` (kernel, boot-risk) |
| WiFi (BCM43455 SDIO) | ⛔ blocked | fw+NVRAM load + CR4 release were built (in the now-**deleted** diag-udp.c) | **firmware not executing** (#91, image-scan proven). NVRAM-trailer lead DISPROVEN (2026-06-07); real suspects = download/clock ordering + SDIO-core intstatus-clear + rstvec semantics. Live downloader must be reintroduced first. Needs HW/JTAG |
| Bluetooth (BCM43455 UART HCI) | ⬜ not started | plan only | needs mailbox+GPIO alt-fn + `.hcd` blob |
| GPIO / pinctrl | 🟡 partial | `/dev/gpio` read-only observer device (#150): snapshot + per-pin `RPI4GPIO_GETPIN` devctl, `gpio/rpi4-gpio/` | **outputs** (GPSET/GPCLR/fsel set) need a bench rig to validate (⏸) |
| I²C / SPI / PWM | ⬜ not started | plans exist | need GPIO alt-fn + clock-manager |
| GPU (V3D 4.2) — OpenGL | ✅ done | ported Mesa gallium v3d driver + GL frontend (`tools/v3d-driver-port/`); **GLQuake (quakespasm) runs ~40-42fps@1080p** via render-to-scanout; R/B color + particle render-stall fixed (2026-06-16/17) | re-enable early-Z, double-buffer (tearing), gamma (cosmetic) |
| GPU (V3D 4.2) — Vulkan (V3DV) | 🔬 groundwork | V3DV compiles+links for aarch64-phoenix (Tier 0); on HW vkCreateInstance works + 5 device-create blockers cleared (2026-06-17); harness `misc/rpi4-v3dv-tier0/` | device-create instruction-abort (Tier 1 cont.); then Tier 2 clear+readback → vkQuake |
| Audio (PWM / I²S / HDMI) | 🟡 partial | PWM driver `/dev/audio0` (`audio/rpi4-audio/`): CPRMAN clock + PWM1 ch1/2 M/S+FIFO bring-up HW-verified (CM_PWMCTL=0x91), s16→FIFO write path verified (0 underruns); **DMA-paced FIFO feed proven** (legacy DMA, PWM1=DREQ 1, CS END/remain=0/no error, 2026-06-17) | continuous self-chained streaming + cross-process shared ring (Quake mmaps /dev/audio0) for the SNDDMA backend (`pl_phoenix_snd.c` stub); audible sign-off ⏸ (needs headphones) |
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
