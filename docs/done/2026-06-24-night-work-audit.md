# Night-work audit — unattended-doable open items (2026-06-24)

Audit of the Pi4 port docs for **documented-but-not-done** work that is doable
**unattended** (build + netboot + UART + HDMI-snapshot + scripted-psh + diag-udp +
multi-boot bench + scriptable Meross power-cycle). Physical-human-action items
(SD swaps, JTAG, device plugs, scope/LED/speaker bench, eyeball/keypress/mouse-move
sign-offs) are **excluded** and listed separately at the bottom.

This report drives the night's work waves. Classification here is by the **task's
physical-action criterion**, NOT the docs' own `[U]/[A]`/⏸ tags (the roadmap tags
are 2026-06-09; the matrix ⏸ are stricter "single-boot-confidence" tags). Several
matrix-⏸ rows ARE unattended by the physical criterion and are included with a loud
risk note; they sort to the bottom on value-per-risk.

**Drift note:** the roadmap plan (`~/.claude/plans/calm-wobbling-quill.md`, progress
log dated 2026-06-09) is far behind reality. Already DONE since then (do NOT re-list):
Phase-0 watchpoint, USB #121 stackfix, all V3D tiers (power-on → triangle → GLQuake
~40fps), Vulkan Tier-4b user-shader triangle, audio streaming DMA + Quakespasm SNDDMA,
NFS poll/perf fixes, X server (Xphoenix) rendering xeyes on HDMI + input driver wired,
openssl/urandom (hwrng-backed). The latest real state is `status.md` 2026-06-18 +
`2026-06-23-overnight-progress.md` + the 2026-06-24 vkQuake scaffold.

---

## Hot-file legend (orchestrator: avoid concurrent subagents touching the same)

- `bcm-genet.c` = `sources/phoenix-rtos-lwip/drivers/bcm-genet.c`
- `devices Makefile` = `sources/phoenix-rtos-devices/_targets/Makefile.aarch64a72-generic`
- `user.plo.yaml` = `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml`
- **GPU-binary swap rule:** only ONE big V3D binary fits per boot. Any swap among
  `rpi4-quake` / `rpi4-v3dv-tier0` / vkQuake touches **BOTH** the devices Makefile AND
  user.plo.yaml, and these targets are **mutually exclusive** — never run two
  GPU-binary subagents concurrently, and never co-schedule with anything else editing
  those two files.
- **Single UART:** only one netboot/Pi cycle at a time. Host-only items (compile/link,
  write shims, code-only cleanups) need NO Pi cycle → run those in parallel; serialize
  only the validation boots.

---

## Unattended-doable open items — ordered by value-per-risk (high-value/low-risk first)

| # | Item | Source pointer | Effort | Risk (boot-critical? cross-repo?) | File-overlap hazard | Exclusive netboot cycle to validate? |
|---|---|---|---|---|---|---|
| 1 | **Build remaining `phoenix-rtos-ports` for aarch64 + stage to NFS export + exec each via scripted-psh** (curl, openssl, dropbear, lua, mbedtls, …; some staged-but-unverified: curl/openssl/dropbear/luac) | roadmap 1.5; `calm-wobbling-quill.md` progress log "still-staged-but-unverified ports"; status.md 2026-06-18 | M | Low. Additive, no source-tree edits to core; cross-repo only into `phoenix-rtos-ports` + NFS export | None in core. Stages to `/srv/phoenix-rpi4-nfs` (host export) | Yes, but shares the nfsroot variant; one scripted-psh cycle per batch |
| 2 | **TD-05 remainder: gate boot markers behind `RPI4_BOOT_MARKER` + `_init.S` 8-space→tab reindent** (upstream-readiness cleanup) | TEMPORARY-FIXES TD-05 (status MOSTLY RESOLVED, line ~477); roadmap 1.6 | S | Low-med. Touches kernel `_init.S` (boot-critical asm) but pure text/gating; netboot-recoverable; rollback tag exists | kernel `hal/aarch64/_init.S` — solo within kernel; no other hot file | Yes, one boot-to-psh smoke (`--scope core`) |
| 3 | **TD-14 / #127 two-owner UART polish** (pl011-tty ↔ kernel klog interleave; throwaway bring-up diagnostics) | TODO(#127) `pl011-tty.c:1132`; TODO(TD-14-*) pl011-tty.c:650/1249/1268, pshapp.c:48/1600, libphoenix file.c:361, kernel name.c:34/258; matrix row "PL011 UART"; roadmap 1.6 | S | Low. Cosmetic/observability; cross-repo (devices+utils+libphoenix+kernel) but each edit small | devices pl011-tty.c, utils pshapp.c, libphoenix file.c, kernel name.c — none are the 3 hot files | Yes, one boot smoke (verify UART un-garbled, psh OK) |
| 4 | **Remove disproved/throwaway diagnostics flagged in source** — TODO(#126) throwaway USB-mouse reader in pl011-tty + board_config.h gate; TODO(quake-render-stall) dump in v3d winsys; bound/rate-limit TODO(#129) log spam in xhci.c/hub.c/mbox.c | TODO(#126) pl011-tty.c:1170 + board_config.h:37; TODO(quake-render-stall) `tools/v3d-driver-port/v3d_phoenix_winsys.c:904`; TODO(#129) xhci.c:1817/1846, hub.c:308/325/344, mbox.c:18/19/129/136/154 | S–M | Low-med. Deletions of dead/diag code; per code-quality policy. xhci.c/hub.c are USB daemon (statistically-sensitive — keep edits to pure log-bounding, don't touch enum logic) | devices xhci.c+hub.c+dev.h (USB), lwip mbox.c, v3d winsys (tools). Not the 3 hot files. **mbox.c also touched by item 11** | Yes, boot smoke + 1 multi-boot bench to confirm USB enum unregressed |
| 5 | **fb0 #149: `FBIOGET_*` fbdev veneer + true `mmap(fd,0)` kernel backing** (prereq for cleaner GPU/X present path) | matrix `/dev/fb0` row (🟡 partial); `2026-06-05-fb0-attended-decisions.md`; roadmap 2.2 | M–L | Med. Adds a devctl ABI + kernel-backed mmap; display-ownership is the subtle part (X DDX already disables fbcon). Validate via HDMI snapshot | devices `video/rpi4-fb/` — solo; not a hot file | Yes, HDMI-snapshot cycle |
| 6 | **RTC via SNTP auto-run at boot** (ntpclient applet exists; wire it into rc / boot so clock sets when a server is reachable) | matrix RTC row (🟡 capability present, "not yet auto-run at boot"); roadmap 2.8 | S–M | Low. Additive; touches boot init / rc only. Needs a reachable NTP server on the netboot link (host-side ntpd) | rc.psh / init config — no hot-file overlap | Yes, one boot to confirm settimeofday landed |
| 7 | **NFS #156 MT server (per-RPC worker threads) for exec-from-NFS throughput** + first-read ENOENT proper fix (libnfs cache config, not blind retry) | roadmap 1.3; `2026-06-07-nfs-night-progress.md`; status.md 2026-06-09 (#156 residuals); ENOENT root-caused parked (filesystems `4486720`) | M–L | Med. Concurrency in the NFS fs server; ENOENT fix risks ERANGE regression (documented). nfsroot-validatable | `sources/phoenix-rtos-filesystems/nfs/` (srv.c) — solo | Yes, nfsroot scripted-psh re-read repro |
| 8 | **Vulkan V3DV noop-job winsys BO/CL fix** (6th device-create blocker: `v3dv_device_create_noop_job` → `v3d42_job_emit_binning_prolog` NULL CL) — the gate to vkQuake on HW | matrix Vulkan row (🔬); `2026-06-18-vulkan-v3dv-noop-job-rootcause.md`; status 2026-06-17 | M–L | Med. Deep GPU winsys interop. HW-validatable via the `rpi4-v3dv-tier0` harness (UART scanout readback). **GPU-binary swap** | `misc/rpi4-v3dv-tier0/` + V3DV port (tools/v3d) + **devices Makefile + user.plo.yaml** (GPU swap) | Yes — exclusive GPU-binary cycle; mutually exclusive with rpi4-quake/vkQuake |
| 9 | **vkQuake host-side build completion** — write `pl_phoenix_sdlcompat.c` (18 SDL2 threading bodies + `pthread_mutex_timedlock`), port platform shims from quakespasm-port, add `copysign` to libphoenix, write the Vulkan vid shim `pl_phoenix_vk_vid.c` | `2026-06-24` update in `2026-06-23-vkquake-port-scaffold-status.md` §5 (already links 0-undefined with placeholders) | L | Med. Host-only (no Pi) for the link work → fully parallelizable. **BLOCKED for real pixels:** SPIR-V shaders need glslang + spirv-opt (NOT on host, no network) — placeholders link but render nothing. So this is link/shim progress only tonight | `tools/vkquake-port/`, libphoenix math (1 sym). On-HW run later = GPU-binary swap (Makefile+user.plo.yaml) | No for the build/link work (host-only). HW Tier-1 later = exclusive GPU cycle |
| 10 | **SMP: scheduler beyond cpu0** (resolve CNTV-on-secondary regression, dispatch to cpu1-3) | matrix SMP row (🟡); `project_smp_d7_d8_findings`; roadmap 1.7 | L | **High.** Kernel scheduler + per-CPU timer; boot-critical. Netboot-recoverable with strict rollback-first. Big throughput win | kernel hal/aarch64 (spinlock/exceptions/_init.S/timer) — solo, but boot-critical | Yes — exclusive; validate per-CPU tick/load via diag-udp `t` + multi-boot bench |
| 11 | **#11 cacheable GENET RX DMA pool** (streaming DMA: cacheable RX pool + per-frame `dc ivac`) — NFS bandwidth lever (Policy B) | matrix CPU/MMU row "residual perf lever"; `2026-06-15-td16-cache-enable-plan.md`; MEMORY TD-16 note; task #11 | M–L | **High.** Touches `bcm-genet.c` (HOT) + cache coherency; aliasing/brick risk. Cable-gated for full gigabit but the cacheable-pool change itself is netboot-validatable (throughput via host `ss -tin`) | **`bcm-genet.c` (HOT)** + mbox.c (item 4 also). Run solo | Yes — exclusive; throughput bench |
| 12 | **USB daemon hardening #142/#143/#145** (IRQ-driven URB-completion path + list-corruption guards) | matrix USB row (#142/#143 ⏸); roadmap 1.2; `2026-06-02-usb-workstream-plan.md` | M–L | **High for confidence.** Statistically-invisible to a single boot (team deferred as "attended" for that reason). Validate ONLY via `test-cycle-bench.sh` multi-boot (≥10). Daemon-internal regression risk | devices xhci.c + usb hub.c/dev.c/drv.c — overlaps item 4's USB edits; serialize | Yes — exclusive + multi-boot bench (≥10) |
| 13 | **#43 reboot/poweroff: productionize `_hal_systemReset`** | matrix Watchdog row (⏸); roadmap 1.8 | M | **High.** Kernel HAL; a wrong park needs a power-cycle, but netboot+Meross recovers it. diag-udp `r`/`h` already cover the operational need | kernel hal/aarch64 — solo, boot-critical | Yes — exclusive; observe Pi resets & re-netboots |
| 14 | **TD-02/03/06/20 kernel shortcuts** (pre-MMU inval, syspage/BSS map, DTB-driven memory layout [enables 8GB], DC ZVA gate) | TEMPORARY-FIXES TD-02 (l.254), TD-03 (l.278), TD-06 (l.506), TD-20 (l.213); roadmap 1.8 | M–L each | **High.** Kernel early-boot/cache; each low individual risk but boot-critical, netboot-recoverable. TD-20 (DC ZVA) does NOT repro in QEMU | kernel hal/aarch64 (_init.S, _memset.S, dtb.c, pmap.c) — solo | Yes — exclusive each |
| 15 | **TD-10 SError unmask + #144 PCIe inbound-abort root-cause** | TEMPORARY-FIXES TD-10 (l.600, PENDING, blocked by live PCIe/USB external abort); matrix PCIe row; roadmap 1.8 | L | **Highest boot-risk.** Unmasking regresses boot today (handler halts on the live abort). Research-only unattended; the fix itself needs the abort root-caused (ties to USB wall). Include but expect investigation, not a clean win | kernel _exceptions.S/cpu.c + devices usb/xhci/bcm2711-pcie.c | Yes — exclusive; high revert frequency |

---

## Done-but-residual (documented "done" with leftover TODOs / half-finished sub-steps)

Anchor each to its in-source marker. Where a residual is itself a clean unattended
cleanup, it also appears in the table above (item 4).

- **ext2 rootfs #120 — DONE but residuals are HW-gated.** Single-block-only CMD24/CMD18
  (perf) + noisy-recovering 50 MHz Data-CRC. Markers: `TODO(#120)` in
  `sdstorage_dev.c:48/49/140`, `sdcard.c:250`; `TODO(#154 diag)` self-test in
  `sdcard.c:39`. **Validation is ATTENDED (SD card swap)** — do NOT schedule the
  multi-block fix as an unattended win; code can be written but not validated.
- **USB enum/HID "done" — TODO(#129) bounding still open.** `xhci.c:1817/1846`
  (rate-limit timeout report; "promote to bounded retry"), `hub.c:308/325/340/344`
  (bound log volume / re-enum loop / device-tracking sync), `dev.h:76` (per-port
  enum-fail count), `mem.c:169` (`TODO(USB-MEM)` free-list-head-corrupt guard, root
  cause UNCONFIRMED per MEMORY). lwip `mbox.c:18/19/129/136/154` (#129/#121 corruption
  diagnostics — disproved-hypothesis dumps, candidate for deletion). The pure
  log-bounding + dead-diag deletions are clean unattended (item 4); the retry-promotion
  needs the USB bench (item 12).
- **#126 USB mouse "done" — throwaway bring-up reader still in tree.** `TODO(#126)`
  in `pl011-tty.c:1170` + `board_config.h:37` (a diagnostic mouse reader, "nothing
  in-tree consumes it yet"). Clean deletion (item 4).
- **GLQuake "shippable" — render-stall diagnostic dump left in.** `TODO(quake-render-stall)`
  in `tools/v3d-driver-port/v3d_phoenix_winsys.c:904` ("UNRESOLVED residual render stall"
  diagnostic). Stall mitigated via `r_quadparticles 0`; the dump is removable (item 4).
- **TD-14 IPC-slowness markers** across `pl011-tty.c:650/1249/1268`, `pshapp.c:48/1600`,
  `libphoenix file.c:361`, `kernel name.c:34/258` — retry-budget + devfs fast-path
  shortcuts kept; the two-owner UART polish (#127) is the live cleanup (item 3).
- **TD-04-hack-2/-hack-3 — ACTIVE HACKs in `hal.c`.** Localization probes + fake
  `dtbEnd = dtbStart + 0x10000`. Both kernel boot-critical; resolving needs the Heisenbug
  root-caused — careful, not a clean night win.
- **TD-19 — VALIDATED, needs upstream review** (kernel TLBI `dsb;isb` hardening). No code
  TODO; pure upstreaming.

---

## Excluded as ATTENDED (physical human action required) — for completeness

- **SD #120 exec-from-card + #154 write-completion** — code writable unattended, but
  validation = **SD card swap** (`SDMA_BOUNDARY` change, CMD13 poll). roadmap 1.4.
- **WiFi #91 (BCM43455 fw-exec gate)** — ⛔ blocked; software experiments possible but
  worst-case needs **JTAG**; live downloader must be reintroduced first. roadmap 2.1.
- **USB mass storage (umass)** — implementable unattended, validation needs **plugging a
  USB stick**. roadmap 2.4.
- **GPIO outputs / I²C / SPI / PWM** — need a **bench rig** (LED/scope/LA/sensor). 2.6.
- **Audio audible sign-off** — driver + SNDDMA done; tone audibility needs **headphones/
  speaker**. matrix Audio row; roadmap 2.7.
- **Bluetooth Tier-4 scan** — needs a **BLE device advertising in range**. 2.5.
- **#24 mouse interrupt-endpoint events** — root-caused; needs a **physical mouse move**
  to confirm event delivery. `2026-06-23-overnight-progress.md`.
- **#28 torch-flame black triangles** — deep V3D GLSL-alias shader bug; needs eyeball +
  deep investigation. `2026-06-23-overnight-progress.md`.
- **#26 Quake LAN multiplayer** — needs FIONREAD+NFS-safe rework THEN a **2-machine
  connect** (attended). Reverted; `2026-06-23-overnight-progress.md`.
- **X11 interactive finish** — server renders xeyes + input driver wired, but
  pupil-track / keyboard-in-X / twm need **mouse-move + keypress** sign-off (+ a poll()-
  readiness risk). `2026-06-23-overnight-progress.md`.
- **#12 directory reorganization** — touches the **whole tree** → conflicts with every
  other subagent; must run solo and is a structural change better done attended.
- **TD-16 global-cache spike** — already RESOLVED (caches ON since 2026-05-17); the only
  residual lever is item 11 (uncached GENET RX pool).

---

## Open tasks not located (orchestrator: unmapped)

`gh` is unauthenticated on this host, so the canonical descriptions of open tasks
**#29, #30, #31, #32, #33** could not be retrieved and are not present in the docs tree.
**#11** (cacheable-RX DMA) and **#12** (dir-reorg) are mapped above from MEMORY /
overnight-progress; **#26** (multiplayer) and **#28** (torch) are mapped from
`2026-06-23-overnight-progress.md`; **#34** (quakespasm-as-patch) is DONE (commit
c48fdf7). The orchestrator should `gh auth login` then `gh issue list --state open`
to recover #29–#33 before relying on this audit being complete.

---

## Suggested first wave (parallel-safe)

- **Host-only, no Pi (run concurrently):** item 1 (port builds — host compile + stage),
  item 9 (vkQuake shims/link — host only), item 2 / item 4-deletions (code edits; no
  validation boot yet).
- **Then serialize ONE Pi validation cycle at a time**, in value-per-risk order, never
  co-scheduling two GPU-binary swaps (items 8/9-HW) or two USB-internal edits (items 4/12)
  or two `bcm-genet.c` touchers (item 11).
