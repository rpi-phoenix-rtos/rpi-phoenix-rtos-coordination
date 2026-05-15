# Pi 4 RTC, Thermal, Watchdog & Power Management — Implementation Plan

Companion to the research briefs:
- `/Users/witoldbolt/phoenix-rpi/.claude/worktrees/dazzling-joliot-cd9889/docs/research/rtc-thermal-power.md`
- `/Users/witoldbolt/phoenix-rpi/.claude/worktrees/dazzling-joliot-cd9889/docs/research/rtc-thermal-power-non-linux.md`

Project conventions in `AGENTS.md` and `docs/code-quality-and-upstreaming.md` apply: small reviewable commits in upstream Phoenix repos, integration manifest in this repo, public-readability bar.

---

## 1. Scope decision per subsystem

| Subsystem | Decision | Rationale |
|---|---|---|
| Watchdog (reboot) | **MUST** | Production blocker. Test harness currently power-cycles via relay; that races the build script and is slow. Same MMIO unlocks poweroff. |
| Thermal — read-only | **MUST** | Cheapest diagnostic; firmware-level hard trip already protects the silicon, but software needs *some* observable temp for telemetry and bring-up regression tests. |
| Power-off via partition-63 | **SHOULD** | Falls out of the watchdog driver almost free (one extra register write). Useful for clean shutdown and for the future "automated long-running soak test" workflow. |
| Thermal trip / throttling policy | **DEFER** | Firmware default trip (~115 °C) protects hardware; ARM/GPU clock ramping requires `SET_CLOCK_RATE` mailbox plumbing and a thermal governor — out of scope for first boot. |
| RTC | **DEFER** | Pi 4 has no on-SoC RTC. Add PCF8523/DS3231 driver only after I2C lands and a HAT is integration-tested. NTP via Ethernet is the standard zero-hardware path. |

Order of value delivery: **mailbox → watchdog → thermal → poweroff**. Watchdog could land before mailbox (it has no mailbox dependency), but mailbox is the bigger architectural decision and unblocks WiFi / BT / GPU work in parallel — so we sequence mailbox first.

---

## 2. Firmware mailbox driver (Phase 1, prerequisite)

### Recommendation: kernel-internal helper, with optional userspace pass-through later

The mailbox is touched by:
- thermal read (`GET_TEMPERATURE`, 1 Hz)
- under-voltage telemetry (`GET_THROTTLED`)
- WiFi / BT bring-up (`SET_GPIO_STATE` for `WL_ON`, `BT_ON`, `BT_REG_ON`)
- GPU / framebuffer (already done in plo via direct MMIO; kernel-side will need it for any post-init reconfig)
- future CPU-frequency control (`SET_CLOCK_RATE`)

Most of those callers are kernel/driver-internal and need the mailbox before any userspace process (or any filesystem) is up. A pure-userspace server cannot own the device cleanly. **Recommendation: kernel-side primitive, mirrored optionally as a thin libphoenix passthrough later.**

### Module layout

```
sources/phoenix-rtos-kernel/hal/aarch64/generic/rpi-mailbox.{c,h}
sources/phoenix-rtos-devices/firmware/rpi-mailbox/   # userspace shim, Phase 1.5
    Makefile
    mailbox.c        # /dev/rpi-mailbox server (msg_t round-trip → kernel call)
    mailbox.h
    properties.h     # tag IDs, request/response struct templates
```

Kernel header (plo's `video.c` already has the same constants — copy + parameterize):

```c
/* hal/aarch64/generic/rpi-mailbox.h */
int rpi_mboxSendProperty(volatile uint32_t *buf, size_t words);  /* channel 8 */
int rpi_mboxRevisionTag(uint32_t *out);                          /* convenience: tag 0x00000001 */
```

### MMIO + protocol

Base `0xFE00B880` (`PLO_RPI_MAILBOX_BASE_ADDRESS` in `board_config.h` already exposes this).
Registers (relative offsets, see `sources/plo/hal/aarch64/generic/video.c:36-46`):
- `MBOX0_READ`     `+0x00`
- `MBOX0_STATUS`   `+0x18`  (bit 30 EMPTY, bit 31 FULL)
- `MBOX1_WRITE`    `+0x20`
- `MBOX1_STATUS`   `+0x38`

Property-channel buffer: 16-byte aligned, `[size_bytes, code(0), tags..., 0]`. The address ORed with channel `8` is written to `MBOX1_WRITE`; spin until `MBOX0_STATUS & EMPTY == 0`, read until the channel byte matches, validate `buf[1] == 0x80000000`.

### Polling vs IRQ

**Poll for v1.** Mailbox round-trips are sub-millisecond and infrequent (1 Hz thermal at most). IRQ wiring (mailbox IRQ at GIC SPI 65) costs more code and is only worth it if WiFi or USB ever needs back-to-back property tags from a hot path. Defer.

### Cache discipline

The buffer must be flushed before write and invalidated before read; plo already does this in `video.c` via `hal_dcacheClean`. Re-use the kernel's `_hal_dcache*` after TD-04 ironing (already validated per `tracking/current-step.md`).

### Userspace pass-through (Phase 1.5, optional)

If/when a userspace driver legitimately needs property-tag access (e.g. a GUI temp widget), add `phoenix-rtos-devices/firmware/rpi-mailbox/` exporting `/dev/rpi-mailbox` with one ioctl: `RPI_MBOX_SEND_PROPERTY` taking a `uint32_t *buf, size_t len`. The implementation is a 30-line msg_t handler that copies `buf` into a kernel-pinned page and calls `rpi_mboxSendProperty`. Skip until a real user appears.

### Validation

Phase 1 ships with one regression check: print the firmware revision (tag `0x00000001`) to UART during early kernel init. The expected value depends on EEPROM revision but is constant across reboots; record it in `manifests/`.

---

## 3. Watchdog driver

### Module layout

```
sources/phoenix-rtos-devices/watchdog/rpi4-bcm2711-wdog/
    Makefile
    rpi4-wdog.c        # userspace server: /dev/watchdog0 + ioctls
    rpi4-wdog.h
sources/phoenix-rtos-kernel/hal/aarch64/generic/rpi-pm.{c,h}
                       # kernel-side helpers: _hal_systemReset, _hal_systemHalt
```

Two-layer design because `_hal_systemReset` must work from kernel panic context (no userspace, no msg_t). Userspace `/dev/watchdog0` covers liveness-pinging by app code.

### Register layout (per `docs/research/rtc-thermal-power-non-linux.md` §3, §9)

Base `0xFE100000`. Three offsets, password `0x5A000000`:

| Reg | Offset | Purpose |
|---|---|---|
| `PM_RSTC` | `0x1C` | Reset control. WRCFG: `0x20` = full reset, `0x00` = clear. |
| `PM_RSTS` | `0x20` | Reset status. Bits {0,2,4,6,8,10} encode 6-bit boot partition. |
| `PM_WDOG` | `0x24` | 20-bit countdown in ~16 µs ticks (32 kHz / 2). |

### API

```c
/* userspace driver public surface */
int  wdog_init(void);                       /* map MMIO, no-op start */
int  wdog_start(uint32_t timeout_us);       /* arm WDOG with given timeout */
int  wdog_feed(void);                       /* re-arm with previous timeout */
int  wdog_stop(void);                       /* RSTC = PASSWORD | RSTC_RESET (clears WRCFG) */
int  wdog_trigger_reboot(void);             /* partition 0, ~150 µs WDOG */
int  wdog_trigger_poweroff(void);           /* partition 63, same timing */

/* kernel hooks */
void _hal_systemReset(void);                /* from panic / shutdown(2) */
void _hal_systemHalt(void);                 /* poweroff path */
```

### Reboot / poweroff sequences (NetBSD `bcm2835_pmwdog.c` is the cleanest reference)

```c
static void pm_writePartition(unsigned partition) {
    uint32_t v = readl(PM_RSTS) & ~PM_RSTS_PARTITION_BITS;
    /* spread 6 bits of `partition` across {0,2,4,6,8,10} */
    for (int i = 0; i < 6; i++)
        if (partition & (1u << i)) v |= 1u << (i * 2);
    writel(PM_RSTS, PM_PASSWORD | v);
    writel(PM_WDOG, PM_PASSWORD | 10);                 /* ~150 µs */
    writel(PM_RSTC, PM_PASSWORD | (readl(PM_RSTC) & PM_RSTC_WRCFG_CLR) | PM_RSTC_WRCFG_FULL_RESET);
}
```

`pm_writePartition(0)` reboots; `pm_writePartition(63)` halts. The GPU stage-1 bootloader reads `PM_RSTS` after the reset, sees partition 63, and parks instead of reloading `kernel.img` — this is the only "poweroff" the Pi 4 hardware supports (no PMIC kill line; see `docs/research/rtc-thermal-power-non-linux.md` §10 and the NetBSD `bcm2835_power_poweroff` reference there).

### `/dev/watchdog0` semantics

Optional but cheap. Standard Linux-ish ioctls: `WDIOC_KEEPALIVE`, `WDIOC_SETTIMEOUT`, `WDIOC_GETTIMEOUT`, `WDIOC_GETSTATUS`. Skip the magic-write-on-close convention for now; not all Phoenix tooling honors it.

### Constraints

- Watchdog is intentionally **independent of mailbox** — it must work when mailbox is wedged. No firmware round-trip in the kick path.
- `PM_WDOG` is shared with the bootloader's `BOOT_WATCHDOG_TIMEOUT` mechanism; OS arming after boot is safe (the bootloader's timer is implicitly cancelled when the OS boots).

---

## 4. Thermal driver

### Module layout

```
sources/phoenix-rtos-devices/sensors/rpi4-thermal/
    Makefile
    thermal.c    # /dev/thermal pseudo-file: cat returns "<millideg>\n"
```

### Implementation

```c
int rpi4_thermal_get_celsius(int *out_milliC) {
    uint32_t buf[8] __attribute__((aligned(16))) = {
        sizeof(buf), 0,           /* size, code */
        0x00030006, 8, 4,         /* GET_TEMPERATURE: tag, payload size, req size */
        0,                        /* sensor_id */
        0,                        /* response value */
        0                         /* end tag */
    };
    if (rpi_mboxSendProperty(buf, sizeof(buf)/4) != 0) return -EIO;
    if (buf[1] != 0x80000000) return -EIO;
    *out_milliC = buf[6];
    return 0;
}
```

`/dev/thermal` exposes a `read()` that formats the value to `"<millideg>\n"` so `cat /dev/thermal` works for spot-checks. No trip configuration, no governor.

### Optional `GET_THROTTLED` (tag `0x00030046`)

Worth surfacing because under-voltage is the most common silent Pi failure mode (per `docs/research/rtc-thermal-power.md` §4). Folded into the same driver as a second pseudo-file `/dev/throttled`, returning the bitfield in hex. Sticky bits (16-19) treated as "since boot" — log to UART once on first poll.

---

## 5. Power-off path

Implemented in the watchdog driver (§3) — `wdog_trigger_poweroff()`. Justification:

- Same MMIO block, same password, same timing.
- Partition-63 magic is the only way to get a clean Pi 4 power-off.
- Reference: NetBSD `bcm2835_power_poweroff` (`sources` cite in `docs/research/rtc-thermal-power-non-linux.md` §3).

Wire to the kernel reboot path:

```c
void _hal_systemHalt(void) { pm_writePartition(63); for (;;) wfe(); }
void _hal_systemReset(void) { pm_writePartition(0);  for (;;) wfe(); }
```

Both live in `sources/phoenix-rtos-kernel/hal/aarch64/generic/rpi-pm.c`.

---

## 6. Phased delivery

### Phase 1 — Mailbox primitive (kernel-internal)
- Add `hal/aarch64/generic/rpi-mailbox.{c,h}`, refactored from `sources/plo/hal/aarch64/generic/video.c:36-110`. Same MMIO, same poll loop, parameterized buffer.
- Hook one call into `_hal_init`: print firmware revision (tag `0x00000001`) to UART.
- **Test**: rebuild via `./scripts/rebuild-rpi4b-fast.sh`, capture UART, verify rev tag printed and value is plausible (`0x00xxxxxx`).
- **Manifest**: snapshot via `./scripts/snapshot-integration-state.sh`.

### Phase 2 — Watchdog driver (reboot only)
- Add `hal/aarch64/generic/rpi-pm.{c,h}` with `_hal_systemReset`. No userspace driver yet.
- Wire into existing kernel reboot path (replace whatever current shutdown does — likely `wfe` infinite loop).
- **Test**: trigger reboot from a userspace test (`reboot` syscall, or `phoenix-rtos-tests` runner). Pi reboots without external power-cycle. Compare against current relay-cycle baseline in `tracking/current-step.md`.

### Phase 3 — Thermal read
- Add `phoenix-rtos-devices/sensors/rpi4-thermal/`.
- Drop into `_targets/aarch64a72-generic-rpi4b/` build.
- **Test**: `cat /dev/thermal` returns 35000 – 60000 (35 – 60 °C indoors at idle). Validate against a probe touched to the SoC heatsink — within ±3 °C of an external thermometer.

### Phase 4 — Poweroff
- Add `_hal_systemHalt` and the `wdog_trigger_poweroff()` userspace wrapper.
- **Test**: invoke from userspace; verify Pi power LED goes red-only (firmware halt state) and pulling/replugging USB-C boots normally.

Each phase: small commit in the respective upstream repo + a coordination-repo commit + one manifest entry. No phase widens scope: e.g. Phase 2 does not pre-emptively add `/dev/watchdog0` userspace ioctls — those land in a follow-up after Phase 4.

---

## 7. Test strategy (observable behavior)

| Phase | Observable | How to check |
|---|---|---|
| 1 | Firmware rev printed in UART boot log | `python3 scripts/summarize-rpi4b-uart-log.py <log>` greps for `firmware-rev:` line |
| 2 | Pi reboots without relay action | UART shows `_hal_init` twice across one test run; relay state unchanged |
| 3 | Plausible temperature | `cat /dev/thermal` ∈ [35000, 75000] within 5 s of boot |
| 4 | Pi reaches halted state | UART silent; power LED red-only; USB-C re-plug reboots |

All four are gateable by `scripts/summarize-rpi4b-uart-log.py` extensions — add one regex per phase. No probe code committed beyond what `code-quality-and-upstreaming.md` allows.

---

## 8. Inter-dependencies

```
                    rpi-mailbox (Phase 1)
                    /     |        \
                   /      |         \
              thermal   wifi/bt    gpu reconfig (later)
              (P3)    (out of scope here)
                                       ↑
                                       │ — already uses mailbox in plo
                                       │   stage; kernel side will need it
                                       │   for any post-init mode change

   rpi-pm / watchdog (Phases 2, 4) — independent of mailbox
```

Watchdog has **no** mailbox dependency: this is deliberate — watchdog must arm even when mailbox is wedged (e.g. firmware crash). Thermal **does** depend on mailbox. Power-off depends on watchdog only (same MMIO).

External (out-of-scope here, but unblocked by Phase 1):
- WiFi `WL_ON` GPIO via `SET_GPIO_STATE` (mailbox tag `0x00038041`)
- BT `BT_REG_ON` GPIO same path
- Future CPU DVFS via `SET_CLOCK_RATE`

---

## 9. Effort estimate

| Phase | Eng days | Confidence |
|---|---|---|
| 1 — mailbox primitive (kernel-side, refactor from plo) | 3 – 5 | High; plo code already works |
| 2 — watchdog reboot | 2 – 3 | High; ~150 LoC, three regs |
| 3 — thermal read | 1 | High; ~50 LoC over Phase 1 |
| 4 — poweroff | 1 | High; one extra function over Phase 2 |
| **Total** | **7 – 10** | |

Buffer +2 days for first-boot integration weirdness (cache coherency on the property buffer is the most likely surprise; TD-04 history suggests so).

---

## 10. Open questions

1. **Kernel-internal vs userspace mailbox.** Recommendation above is kernel-internal with optional userspace shim later. Confirmable now: are there any *strict* userspace-only consumers (e.g. test infra) that would force a userspace-first design? Probable answer: no — but worth a quick scan of `phoenix-rtos-tests` before committing to the kernel-internal split.
2. **`/dev/watchdog0` exposure.** Linux-ish ioctls are easy but Phoenix doesn't have a documented watchdog dev-class. Decision deferred to after Phase 2; a zero-userspace driver covers the operational need (reboot from kernel panic).
3. **Thermal sysfs equivalent.** No sysfs in Phoenix. Pseudo-file at `/dev/thermal` returning `"<millideg>\n"` is the chosen surface — confirm in code-review whether `procfs`-style might be preferred. (`phoenix-rtos-devices/sensors/sensors.c` has prior art for sensor pseudo-files; copy that style.)
4. **Mailbox buffer placement.** plo uses either a fixed `PLO_RPI_MAILBOX_BUFFER_ADDRESS` (low memory) or a 16-byte-aligned static. Kernel-side should use a per-CPU static buffer with explicit cache ops; revisit if SMP arrives.
5. **PM clock validation.** `docs/research/rtc-thermal-power.md` §8 flags that `PM_WDOG` tick rate has not been verified for our specific firmware blob. First reboot test will inform this — if 10 ticks ≠ ~150 µs, adjust the constant. Not blocking.

---

## 11. References (paths, not snippets)

- Research: `docs/research/rtc-thermal-power.md`, `docs/research/rtc-thermal-power-non-linux.md`
- Existing mailbox code (plo): `sources/plo/hal/aarch64/generic/video.c:36-110`
- Existing watchdog template (i.MX 6ULL): `sources/phoenix-rtos-devices/watchdog/imx6ull/imx6ull-wdg.c`
- Existing temp-driver style: `sources/phoenix-rtos-devices/temp/nct75/`, `sources/phoenix-rtos-devices/temp/sht3x/`
- Board config: `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/board_config.h:25-27` (mailbox base/buffer already declared)
- Kernel HAL target dir: `sources/phoenix-rtos-kernel/hal/aarch64/generic/`
- Build/test loop: `scripts/rebuild-rpi4b-fast.sh`, `scripts/capture-rpi4b-uart.sh`, `scripts/summarize-rpi4b-uart-log.py`
- Rollback: `scripts/snapshot-integration-state.sh`, `scripts/restore-integration-state.sh`

---

**File path**: `/Users/witoldbolt/phoenix-rpi/.claude/worktrees/dazzling-joliot-cd9889/docs/plans/rtc-thermal-power-impl.md`
**Prose word count**: ~1900 words (excluding code blocks, tables and the references list); within the 1500 – 2500 target.
