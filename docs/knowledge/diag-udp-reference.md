# diag-udp — Pi 4 network observability responder reference

**Updated:** 2026-06-05.

The Pi 4 lwIP port embeds a small UDP responder (`sources/phoenix-rtos-lwip/port/diag-udp.c`)
that listens on **UDP port 9999** and replies to a single command character with a
text report framed as `PHX-DIAG/1 ...\n...\n.\n`. It is the primary way to inspect
the running Pi 4 over the network during netboot bring-up (no shell required).

## Sending a command

```
./scripts/diag-udp-probe.sh <cmd-char> <label> [ready_wait_s] [send_timeout_s] [ip] [port]
```
The Pi must already be booted with lwIP up (`lwip: genet ... link up`). The IP is
auto-discovered from the dnsmasq lease if omitted. Replies are saved under
`artifacts/diag-udp/`. `test-cycle-netboot.sh --probe <cmd>` boots-then-probes in
one shot, and every cycle auto-reads `c` (thermal/clocks) at the end.

## Commands

### System / kernel
| Cmd | Report |
|---|---|
| `t` | per-thread list + cpuTime (scheduler/load) |
| `b` | CPU burn / timing probe |
| `m` | kernel meminfo |
| `D` | devfs node listing |
| *(other)* | default: netif status (IP/gw/flags/link, lwIP stats) |

### Hardware (BCM2711 peripherals)
| Cmd | Report |
|---|---|
| `c` | VideoCore mailbox: EMMC/EMMC2 clock rates **+ thermal** (`temp_mC`, `max_mC`, `throttle`) |
| `g` | GPIO snapshot (fsel / pull / level) |
| `r` | **reboot** (PM_RSTC countdown, partition 0) — Pi restarts ~100 ms later |
| `h` | **halt/poweroff** (partition 63) |
| `P` | PCIe root-complex error-latch registers (TD-10 / USB abort triage) |
| `V` | **framebuffer probe** — graphmode query + mmap + non-destructive rw verify (fb0 groundwork) |
| `R` | **device-read smoke test** — open+read `/dev/thermal`,`/dev/throttled`,`/dev/hwrng` cross-process |

### WiFi / BCM43455 SDIO bring-up (debug-heavy; see WiFi notes)
| Cmd | Report |
|---|---|
| `s` | SDIO controller scout (mailbox clock + SDHCI version/caps) |
| `w` | WiFi power-on (WL_REG_ON via mailbox) + state |
| `i` | SDIO summary |
| `e` | SDIO chip enumeration (CMD5/CMD3/CMD7/CCCR) |
| `f` | F1 backplane probe |
| `F` | SDIO cores walk |
| `E` | EROM walk |
| `A` | ARM-CR4 ResetCtrl / IoCtrl |
| `S` | SOCRAM / BootROM window read |
| `B` | CMD53 block read |
| `W` | CMD53 block write |
| `H` | SDIO HS-mode (25/50 MHz, 4-bit) |
| `L` | firmware load test |
| `I` | full firmware load |
| `G` | firmware release (CR4 activation) + fw-alive check (#91 gate) |
| `M` | firmware walk |

(The WiFi `s..G` set is bring-up instrumentation for the firmware-execution gate
#91; expect to prune it once WiFi lands. See `docs/inprogress/2026-06-04-wifi-fw-exec-gate.md`.)

## Device nodes registered on the Pi 4 (netboot variant)

| Node | Provider | Read returns |
|---|---|---|
| `/dev/console`, `/dev/pts*` | pl011-tty + posixsrv | tty |
| `/dev/kbd0`, `/dev/mouse0` | `usb` daemon (USB HID) | input reports |
| `/dev/mmcblk0[pN]` | `bcm2711-emmc` (SD-boot) | block device |
| `/dev/thermal` | `rpi4-thermal` | SoC temp, milli-C (`"<mC>\n"`); `-EIO` on mailbox failure |
| `/dev/throttled` | `rpi4-thermal` | throttle/under-volt bitfield (`"0x%08x\n"`) |
| `/dev/hwrng` | `rpi4-hwrng` | hardware entropy stream (raw bytes, RNG200) |
| `/dev/{null,zero,full,urandom}` | posixsrv | standard pseudo-devices |

## Protocol notes

- Reply framing: first line `PHX-DIAG/1 <name>`, body lines, terminated by a lone `.`.
- One UDP datagram in, one (possibly multi-line) datagram out; no retransmit.
- `r`/`h` reply first, then fire the PM countdown from a detached thread (~100 ms),
  so the reply reaches the host before the Pi resets.
- The mailbox commands (`c`,`w`,`V`) and the `rpi4-thermal` driver share the single
  VideoCore mailbox with **no cross-process lock** — avoid hammering them
  concurrently (see the mailbox-concurrency caveat in `project_pi4_thermal_driver`).
