# Userspace demo-apps plan — make `(psh)%` actually do something

> **STATUS 2026-06-18 — LARGELY DELIVERED (this plan is mostly superseded by progress).**
> The hard prereq (USB HID keystrokes) is done; psh is interactive on HW.
> - **Tier A (Alive):** ✅ psh applets work on HW; `mv` was the one missing MUST and is now
>   added (psh `mv` applet, rename + dir-target + EXDEV copy-fallback, committed 112c56b);
>   the `rpi4-sysinfo` boot banner (build stamp/uptime/HW-entropy/device inventory) covers the
>   motd/hello item.
> - **Tier B (Interactive):** ✅ `lua` REPL + `micropython` both HW-validated exec-from-NFS
>   (lua 5.3 stdlib correct; micropython "micropython-exec-ok 42"). `busybox` ash also runs.
> - **Tier C (Networked):** ✅ the crypto/network class now RUNS on HW (2026-06-18): `curl 7.64.1`
>   with mbedTLS (HTTPS/SSL), `Dropbear SSH client v2018.76`, `openssl` (version + dgst + rand) —
>   all were previously blocked on an unseeded `/dev/urandom`, now hwrng-backed. (sshd daemon
>   end-to-end login still needs a host-side client session = attended.)
> - **Tier D (Visual flourish):** `coremark` validated (perf number, see below); an ANSI game is
>   the remaining optional polish.
> - **RTC-via-NTP:** the `ntpclient` psh applet already queries SNTP and calls `settimeofday`
>   (kernel `settime` syscall + libphoenix `settimeofday`/`clock_settime` all present), so the
>   capability EXISTS. It defaults to `pool.ntp.org` → needs a reachable NTP server (internet
>   route or a host-side ntpd on the netboot link) to actually sync; not yet wired into boot.
>
> Net: the "first-keyboard demo experience" this plan scoped is achieved. Remaining items are
> optional polish (an ANSI game) or attended (sshd login). Original plan retained below for record.


The Pi 4 port reaches `(psh)%` today and USB phase 2
([usb-xhci-impl.md §6 Phase 3](../done/usb-xhci-impl.md)) is wiring real HID
keystrokes into libtty within the next few sessions. The moment a
keypress on a USB keyboard appears at the prompt, the system needs to
have *something interesting to type*. This plan inventories what is
already buildable, ranks ports we can pull from
[`phoenix-rtos-ports/`](../../sources/phoenix-rtos-ports/), and lays
out a tiered "demo experience" that scales from "system feels alive"
(works at HID-landing) to "type code, get output" (small interpreter)
to "visual flourish" (mc, a game) without blocking the v1.0 critical
path.

---

## 1. Goal and phased experience tiers

The deliverable is **not** a polished distro. It is the
shortest possible path from "kernel reaches psh" to a demo a person
can sit down at and play with. Four tiers, each gateable on its own:

- **Tier A — Alive (must-have at first-keyboard milestone).** The
  prompt responds. Built-in psh applets only. Zero new build work.
  Acceptance: type `ls /`, `ps`, `cat /proc/version`, `echo hi`,
  see correct output on UART and HDMI.
- **Tier B — Interactive (early showcase).** A scriptable interpreter
  is on the rootfs. The user can type code at a REPL and see
  computed output. Acceptance: type `lua` (or `micropython`), get
  REPL prompt, run `print(2^32 + 1)`, see `4294967297`.
- **Tier C — Networked (post-GENET / M5).** `dropbear sshd` brings up
  remote login; `wget`/`curl` pull a file. Multi-user feel. Gated on
  Ethernet (M5).
- **Tier D — Visual flourish (post-v1.0 polish).** `mc` (Midnight
  Commander) on the framebuffer, a small ANSI game (sokoban / 2048 /
  nibbles), `coremark` to demonstrate the cache-enable speedup.

Tier A is achievable today (USB HID is the only blocker); Tier B is
~1–2 dev-weeks of port enable; Tier C piggy-backs on M5; Tier D is
optional polish near v1.0.

---

## 2. Inventory: `phoenix-rtos-ports/` (sources/phoenix-rtos-ports/)

26 ports exist in the tree
([sources/phoenix-rtos-ports](../../sources/phoenix-rtos-ports/)).
Metadata read from each `port.def.sh`. "aarch64-ready" is best-effort:
no aarch64 ports flag exists today, so this column reflects whether
the port has architecture-specific assumptions (micropython, for
example, gates on `TARGET_FAMILY` and currently logs a warning for
non-armv7m/ia32 — see
[micropython/port.def.sh lines 60–73](../../sources/phoenix-rtos-ports/micropython/port.def.sh)).

| Port | License | Demo value | aarch64 readiness | Notes |
|---|---|---|---|---|
| `busybox` 1.27.2 | GPL-2.0-only | High — 50+ unix utils in one binary | Likely portable; per-target `busybox_config` already exists for ia32/zynq/zynqmp ([_projects/.../busybox_config](../../sources/phoenix-rtos-project/_projects/aarch64a53-zynqmp-zcu104/busybox_config)) | Heavyweight; competes with psh applets; **GPL — keep separate from public BSD core** |
| `lua` 5.3.6 | MIT | High — small REPL, ~250 KB stripped | Architecture-agnostic C; `port.def.sh` has no arch gate ([lua/port.def.sh](../../sources/phoenix-rtos-ports/lua/port.def.sh)) | **Best Tier-B candidate** |
| `micropython` 1.26.0 | MIT | High — Python REPL | Currently warns + sleeps for non-armv7m/ia32 targets ([micropython/port.def.sh lines 60–73](../../sources/phoenix-rtos-ports/micropython/port.def.sh)); needs aarch64 stanza | Larger than lua; strong batteries-included |
| `coremark` | Apache-2.0 | Demonstrates cache-enable Δ | Pure C, portable | Tier-D bench; great for showing M2 win |
| `coremark_pro` | Apache-2.0 | Multi-thread bench | Pure C, portable | Larger; gated by `LONG_TEST` in existing projects |
| `dropbear` 2018.76 | MIT | Tier-C remote login (sshd + ssh client) | Portable C; depends on lwIP | Needs M5 (GENET) before useful |
| `curl` 7.64.1 | curl | Pull files / show TLS | Portable C; mbedtls optional | Tier-C; optional Tier-B if loopback only |
| `wpa_supplicant` | BSD-3-Clause-derived | WiFi association | Needs M10 | Out of v1.0 scope |
| `lighttpd` | BSD-3-Clause | Tiny HTTP server | Needs M5 | Optional Tier-C |
| `mbedtls` | Apache-2.0 / GPL-2.0 | TLS for curl/lighttpd | Pure C portable | Library, not a demo by itself |
| `openssl111` | OpenSSL/SSLeay | TLS | Portable | Heavy; mbedtls preferred |
| `coreMQTT` | MIT | IoT messaging | Needs M5 | Library |
| `azure_sdk` | MIT | Cloud client | Needs M5 | Library |
| `picocom` | GPL-2.0+ | Serial terminal | Portable | We use the host build for capture; on-target value low |
| `jansson` | MIT | JSON parsing | Portable | Library |
| `libevent` | BSD-3-Clause | Event loop | Portable | Library |
| `pcre` | BSD | Regex | Portable | Library |
| `zlib` | zlib | Compression | Portable | Library |
| `lzo`, `heatshrink` | GPL-2.0-or-later / ISC | Compression | Portable | Libraries |
| `openvpn`, `openiked`, `sscep` | GPL-2.0-only / various | VPN/PKI tools | Heavy, network-dependent | Out of v1.0 demo scope |
| `smolrtsp` | MIT | RTSP client | Niche | Out of scope |
| `fs_mark`, `lsb_vsx` | GPL-2.0 / SPDX-mixed | Test harnesses | Portable | Test-only |

No ports.yaml exists for `aarch64a72-generic-rpi4b` today — every
peer project has one (e.g.
[aarch64a53-zynqmp-qemu/ports.yaml](../../sources/phoenix-rtos-project/_projects/aarch64a53-zynqmp-qemu/ports.yaml)
is a 4-line stub that gates `coremark_pro` on `LONG_TEST`). We will
need to create one as part of this milestone.

---

## 3. Existing in-tree utilities audit

### 3.1 psh built-in applets

`psh` is a busybox-style multi-call binary
([psh/Makefile lines 25–28](../../sources/phoenix-rtos-utils/psh/Makefile)).
Single source of truth for the applet list:

```
bind cat cd chmod cp date dd df dmesg echo edit exec hd help hm
ifconfig kill ln ls mem mkdir mount nc nslookup ntpclient perf
ping pm printenv ps pwd reboot rm rmdir runfile route sync
sysexec top touch tty umount uptime wget
```

Plus internal: `pshapp` (the REPL with line editing + 512-entry
history, see
[pshapp.c lines 49–52](../../sources/phoenix-rtos-utils/psh/pshapp/pshapp.c)),
`help`, `pshlogin`. Confirmed by the rpi4b user.plo.yaml's
`fastlane_stagePshApplets` step that already creates `mkdir` and
`bind` hardlinks
([build.project lines 41–43](../../sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project)).

**Tier-A is essentially free** — every name above is a single
hardlink away from `/bin/`.

### 3.2 phoenix-rtos-utils peers

[`phoenix-rtos-utils/`](../../sources/phoenix-rtos-utils/) contains
`psh`, `psd`, `gsm`, `nandtool`, `nandpart`, `meterfs-migrate`,
`spitool`, `cm4-tool`, `metacheck`, `benchmarks`. The aarch64a72
target builds **only psh**:
```
DEFAULT_COMPONENTS := psh
```
([_targets/Makefile.aarch64a72-generic line 9](../../sources/phoenix-rtos-utils/_targets/Makefile.aarch64a72-generic)).
Same as zynqmp; armv7a9-zynq7000 adds `spitool`. No coreutils-style
sweep needed for v1.0 — psh's applets already cover what `scope §2.20`
calls out as MUSTs (`ls cat cp mv rm mkdir mount dd ifconfig` — note
`mv` is not currently in psh, and `cp`/`ifconfig` are present;
[scope §2.20 lines 504–523](../knowledge/scope-pi4-uncovered.md)).

### 3.3 phoenix-rtos-corelibs

[`phoenix-rtos-corelibs/`](../../sources/phoenix-rtos-corelibs/)
provides `libalgo`, `libcache`, `libgraph`, `libtinyaes`, `libtrace`,
`libuuid`, `libvga`, etc. None are end-user demo apps; they back the
device drivers and would back any custom demo we write.

### 3.4 Root-skel

[`phoenix-rtos-project/_fs/root-skel/`](../../sources/phoenix-rtos-project/_fs/root-skel/)
contains `etc/{passwd,group,profile,inittab,rc,shells,...}` and empty
`root/` and `tmp/`. Per
[etc/inittab line 17](../../sources/phoenix-rtos-project/_fs/root-skel/etc/inittab)
this is busybox-shaped (`/bin/ash`, `syslogd`); the rpi4b user.plo
path skips it and runs psh directly. We will likely produce a small
`rootfs-overlay/etc/rc.psh` analogous to
[zcu104's rc.psh](../../sources/phoenix-rtos-project/_projects/aarch64a53-zynqmp-zcu104/rootfs-overlay/etc/rc.psh)
to print a banner on first boot.

---

## 4. Top candidates ranked by value × effort

| # | Candidate | Tier | Effort | User-visible value |
|---|---|---|---|---|
| 1 | psh built-ins (already there) | A | 0 d | Type `ls`, `ps`, `cat`, `echo` — system feels alive |
| 2 | `motd` + ANSI banner via rc.psh | A | 0.5 d | Branded first-boot screen on UART+HDMI |
| 3 | A small "hello" demo app written in-tree | A | 1 d | Project-specific demo, prints rev/temp/uptime |
| 4 | `lua` REPL | B | 2–3 d | Type code, get output. Tiny binary. Established port |
| 5 | `micropython` REPL | B | 4–6 d (aarch64 stanza needed) | Same as lua; bigger; "Python on a fresh OS" wow |
| 6 | `coremark` | D | 0.5 d | Number that goes up after M2 cache enable |
| 7 | A 200-LoC ANSI game (2048 / sokoban) | D | 2–3 d | Visceral "this is a real machine" demo |
| 8 | `dropbear` (sshd/scp) | C | 2–3 d after M5 | Remote login; truly multi-user |
| 9 | `busybox` (full) | optional | 1–2 d but **GPL** | 50+ utilities at once; license boundary risk |
| 10 | `mc` (Midnight Commander) | D | not in ports today | Visual file manager — needs new port (~1 wk) |

Recommendation: **Tier A is free, Tier B starts with `lua`, Tier D
starts with `coremark`** — total ≤ 1 dev-week for a complete
"first-keyboard" demo experience, with `dropbear` deferred until M5
makes it interesting.

---

## 5. Per-candidate detailed plan

### 5.1 Tier A — psh applets only (no port work)

**Files to touch (coordination-side):** none.

**Files to touch (`phoenix-rtos-utils`):** none today. The applet
list already includes everything we need; the rpi4b
`fastlane_stagePshApplets` only creates `mkdir` and `bind` hardlinks
([build.project lines 41–43](../../sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project)).
psh's own install rule makes the rest at install time
([psh/Makefile lines 41–47](../../sources/phoenix-rtos-utils/psh/Makefile))
— but that runs only for components built into the image. Verify the
rpi4b path actually instantiates each applet hardlink; if not, extend
`fastlane_stagePshApplets` or move to the standard install rule.

**Image impact:** zero — applets are symlinks/hardlinks to one
binary. psh today is bundled into the same DDR-loaded ELF the boot
script `app -x psh` already pulls
([user.plo.yaml line 16](../../sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml)).

**Smoke script (rc.psh-style, optional):**
```
:{}:
echo "Phoenix-RTOS on Raspberry Pi 4 — demo build"
cat /etc/platform
uptime
```

### 5.2 Tier A — banner + small custom hello

Add a project-specific `rootfs-overlay/etc/rc.psh` (mirroring
[zcu104](../../sources/phoenix-rtos-project/_projects/aarch64a53-zynqmp-zcu104/rootfs-overlay/etc/rc.psh))
that prints a one-screen ANSI banner with kernel version, boot time,
and instructions. Optionally wire a `pi4-hello` app under
`phoenix-rtos-utils/` that polls `/dev/thermal` (when M9 lands) and
prints a status snapshot. Effort: 1 day. No new dependencies.

### 5.3 Tier B — lua REPL

This is the headline Tier-B target.

**Why lua over micropython for first land:** the existing
[lua port.def.sh](../../sources/phoenix-rtos-ports/lua/port.def.sh)
has no arch gate; micropython explicitly logs a warning and sleeps
for `${TARGET_FAMILY}` outside `armv7m7|armv7r5f|ia32`
([micropython/port.def.sh lines 60–73](../../sources/phoenix-rtos-ports/micropython/port.def.sh)).
Lua's port already builds with a `luaconf_local.h` placed by the
project — same model as the per-project busybox_config.

**Files to add:**

1. `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/ports.yaml`
   — new file:
   ```yaml
   ports:
     - name: lua
   ```
   Pattern matched on
   [aarch64a53-zynqmp-qemu/ports.yaml](../../sources/phoenix-rtos-project/_projects/aarch64a53-zynqmp-qemu/ports.yaml).

2. `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/luaconf_local.h`
   — copy from another port that uses lua; or rely on `PORTS_LUA_CONFIG_DIR`
   default (which points at `${PREFIX_PORT}` per
   [lua/port.def.sh line 33](../../sources/phoenix-rtos-ports/lua/port.def.sh))
   — verify via build.

3. `user.plo.yaml`: lua is loaded from rootfs (after a real fs lands
   — see deps), so no plo-script change at first. Until M4, the
   demo path is "drop the lua binary into the dummyfs root via the
   image build; psh `runfile /bin/lua`". Confirm `runfile` is in psh
   applets (it is —
   [psh/Makefile line 27](../../sources/phoenix-rtos-utils/psh/Makefile)).

4. `build-core-aarch64a72-generic.sh` — no change; ports build is a
   separate top-level step driven by `build-ports.sh`
   ([build-ports.sh line 28](../../sources/phoenix-rtos-build/build-ports.sh)).

**Syspage size impact:** lua stripped is ~250 KB on aarch64. The
`go!` flow loads userspace from DDR via `ddr ddr` text/data maps
([user.plo.yaml line 9](../../sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml)).
Until M4 (persistent fs), lua adds ~250 KB to whatever DDR-resident
rootfs we end up shipping in `loader.disk`. Well within the 128 MiB
plo-RAM window already enforced by
[build.project line 196](../../sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project).

**Test:** type `lua` at psh; expect the lua interactive banner;
type `print(math.pi)` → `3.1415926535898`. Type `os.exit(0)` → back
to psh.

### 5.4 Tier B alternative — micropython

If lua lands cleanly and bandwidth allows, mirror the same flow for
micropython. Extra work: add an `aarch64*)` stanza to
[micropython/port.def.sh lines 60–73](../../sources/phoenix-rtos-ports/micropython/port.def.sh)
(replace the sleep-and-warn with a strip flag), and stage
`mpconfigport.mk` like the existing
[micropython/files/001_mpconfigport.mk](../../sources/phoenix-rtos-ports/micropython/files/001_mpconfigport.mk).
Effort budget 4–6 dev-days; deferred to post-Tier-B-lua.

### 5.5 Tier D — coremark

Drop-in: add `- name: coremark` to
`aarch64a72-generic-rpi4b/ports.yaml`; coremark's port.def.sh has no
arch gate ([coremark/port.def.sh](../../sources/phoenix-rtos-ports/coremark/port.def.sh)).
Acceptance: run before and after M2 (Phase B caches), expect ≥ 5×
score improvement — directly demonstrates the value of the cache
work.

### 5.6 Tier D — small ANSI game

Pick one of: 2048 (~150 LoC), sokoban (~250 LoC), nibbles (~300 LoC),
all of which work over a VT100 terminal. Implement as a small
in-tree app under `phoenix-rtos-utils/games/2048/` with its own
Makefile. No port pull-in. Tests: arrow keys move tiles; ESC quits.

### 5.7 Tier C — dropbear

Defer to M5 (GENET online). Then add `- name: dropbear` to
ports.yaml, generate a host key on first boot, write an rc.d
fragment to start `dropbear -F -E -p 22`. Existing zynq7000-zedboard
project has busybox-driven equivalent
([etc/rc line 42](../../sources/phoenix-rtos-project/_fs/root-skel/etc/rc)).
Out of this milestone's scope — listed only so the master plan can
mark it as Tier C dependent.

### 5.8 busybox (optional, license-flagged)

busybox is GPL-2.0-only
([busybox/port.def.sh line 18](../../sources/phoenix-rtos-ports/busybox/port.def.sh)).
The rest of the public-facing core is BSD-3-Clause
([phoenix-rtos-utils/README.md line 12](../../sources/phoenix-rtos-utils/README.md)).
If we ship busybox in the public image, that triggers a per-binary
license boundary, not a project-wide one — acceptable but flagged.
**Recommendation:** do not ship busybox in the v1.0 demo image; psh
+ lua already cover the demo surface. Keep busybox available as an
opt-in for soak testing per existing peer projects.

---

## 6. Test strategy

The existing UART summariser
([scripts/summarize-rpi4b-uart-log.py](../../scripts/summarize-rpi4b-uart-log.py)
per [CLAUDE.md](../../CLAUDE.md) build loop) is the gating mechanism.

| Tier | Test | Pass criterion |
|---|---|---|
| A | Type `ls /` after USB-HID-keystroke milestone closes | UART log shows directory listing |
| A | Type `ps`, `cat /etc/platform`, `uptime` | All return without errno |
| A | Banner | First screen-full visible on HDMI+UART |
| B | Type `lua`; `print(2^32+1)` | `4294967297` echoed |
| B | `os.exit(0)` | Returns to `(psh)% ` cleanly |
| D | `coremark` pre-M2 vs post-M2 | ≥ 5× speedup |
| D | Game starts on `runfile /bin/2048`, arrows respond | Visible state change |
| C | After M5: `ssh -p22 root@<lease>` from build host | Login prompt |

Failures snapshot the manifest and roll back via
[`scripts/restore-integration-state.sh`](../../scripts/restore-integration-state.sh).

---

## 7. Inter-dependencies and critical-path placement

### Hard prereqs

- **USB HID keystrokes end-to-end** ([usb-xhci-impl.md §6 Phase 3–4](../done/usb-xhci-impl.md))
  — without this milestone no demo of any tier is interactive.
- **psh on real HW** — already done (`(psh)%` reaches UART/HDMI per
  [docs/inprogress/status.md](../inprogress/status.md)).

### Soft prereqs

- **M2 (Stage-1 cache enable)** — Tier-B lua works without caches but
  is uncomfortably slow (every malloc is DRAM); coremark's value
  story is post-M2.
- **M4 (persistent boot + fs)** — Until M4, the demo binaries are
  baked into DDR via `loader.disk`; that's fine for 250 KB of lua but
  becomes painful at multi-MB. The Tier-D and Tier-C apps push the
  image past comfort and want a real fs.
- **M5 (GENET)** — gates Tier C (dropbear, curl, lighttpd).
- **M9 (thermal/watchdog)** — unlocks the "show /dev/thermal in the
  banner" Tier-A polish.

### Critical-path effect

**None.** This milestone does not sit on the critical path defined
in [00-master-plan.md §4](../knowledge/00-master-plan.md). It is a pure
parallel-work-stream item, slottable any time after M3 (USB HID).
Tier A is achievable in the *same* session that closes M3.

---

## 8. Effort estimate per tier

| Tier | Best | Likely | Worst |
|---|---|---|---|
| A (psh applets + banner) | 0.5 d | 1 d | 2 d |
| B (lua only) | 2 d | 3 d | 1 wk |
| B+ (lua + micropython) | 4 d | 1 wk | 2 wk |
| D (coremark + 1 small game) | 2 d | 3 d | 1 wk |
| C (dropbear after M5) | 2 d | 3 d | 1 wk |
| **Tier A+B+D total** | **~5 d** | **~1.5 wk** | **~3 wk** |
| **Full A+B+C+D after M5** | ~7 d | ~2 wk | ~4 wk |

---

## 9. Open questions

1. **Should the v1.0 demo image ship lua, or keep ports out of the
   first public release entirely?** Answer affects ports.yaml and
   loader.disk size. Author recommends **ship lua**: it is BSD-/MIT-
   like, ~250 KB, and a Python/lua REPL is the difference between
   "it boots" and "it's alive" at a public demo.

2. **micropython aarch64 enablement: now or post-v1.0?** Author
   recommends **post-v1.0**: lua covers the same demo, and the
   aarch64 stanza in `port.def.sh` is enablement work better done
   once the rest of the port stabilises.

3. **Where does the demo image bake binaries pre-M4?** Today
   `loader.disk` carries DDR-resident text/data; we need to confirm
   that adding lua's binary keeps `loader_size <= ram_bank_size`
   ([build.project line 209](../../sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project)).
   Worst case: defer Tier B until M4 lands.

4. **GPL-bounded busybox: include in soak/test image?** Author
   recommends **yes for internal soak, no for public demo image** —
   keeps the public-image license boundary clean.

5. **psh's `mv` is not in the applet list
   ([psh/Makefile line 25–28](../../sources/phoenix-rtos-utils/psh/Makefile)).**
   scope §2.20 calls `mv` a MUST. Either upstream a `mv` applet to
   psh (small — `cp`+`rm`) or document the gap. Tier-A blocker? No,
   nice-to-have.

6. **How does ports build wire into the rpi4b
   `build-core-aarch64a72-generic.sh` flow today?** The script does
   not call `build-ports.sh`. Confirm whether the orchestrator
   `build.sh` invokes it for any non-empty ports.yaml (per
   [build-ports.sh line 28](../../sources/phoenix-rtos-build/build-ports.sh)
   it should), or whether the rpi4b core build script needs an
   explicit `ports` step appended.

7. **HID layout assumption.** The xhci HID-to-ASCII map per
   [usb-xhci-impl.md §4 lines 151–155](../done/usb-xhci-impl.md) is implied
   US QWERTY. Demos that include "type a special character" assume
   this; document it.
