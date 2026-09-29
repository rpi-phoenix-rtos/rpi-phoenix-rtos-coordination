# WiFi in the default image (F1 "finalize wifi", 2026-09-30)

Scope: [KNOWN-ISSUES F1](../KNOWN-ISSUES.md) / [details](../known-issues-details.md#f1). F1 said WiFi
works from the shell (runtime join, DHCP over the lwip netif, link-loss rejoin, firmware-download
retry, TX 3.6 / RX 3.3 MB/s) but **ships in no image**, because `rpi4-wifi` compiled the
Cypress-licensed firmware in and so could only be built by a standalone script. This change ships it.
**No Pi cycle and no image build were run**; everything below that concerns hardware is a
prediction, checked by the pre-registered cycle at the end.

## Branches (all `wifi/in-image`, pushed to `publish`, not merged)

| repo | commit | what |
|---|---|---|
| phoenix-rtos-devices | `1113cd6` | `rpi4-wifi` reads its firmware from `/lib/firmware/brcm/`; built as a normal component with the `wifi` client; `-f` boot mode; single-instance guard; WL_REG_ON through `rpi4-vcmbox`; quiet bring-up; `wifi` input checks |
| phoenix-rtos-lwip | `c7e9d14` | `wifi43455`: no `/dev/wifidata` polling while not associated |
| phoenix-rtos-project | `0552f86` | `user.plo.yaml` starts `rpi4-wifi -f` (nfsroot + sd); `/etc/wifi.conf.example`; `.gitignore` for the staged firmware |
| coordination | this commit | `scripts/fetch-wifi-firmware.sh` + hook in `rebuild-rpi4b-fast.sh`; `stage-bcm43455-firmware.sh` uses the same pin; `check-rootfs-complete.sh`, `publication-audit.sh`, `sync-netboot-tree.sh`; this doc |

All four are based on today's masters (devices `e364c97`, lwip `356ae98`, project `901a9d4`,
coord `5fd9d59ed`). Merge all four together: the project line launches a binary the devices branch
builds, and the devices binary needs the firmware the coord script stages.

## Design

### 1. The driver reads its firmware at run time

Before: three generated C arrays (`wifi-fw-43455.c` and `wifi-nvram-43455.c` in the lwip port dir,
`clm-43455.h` in `tools/wifi-probe/`) were `#include`d, so the binary *was* the firmware (1.6 MB)
and nothing in the framework build could produce it. After: `wifi_fwLoad()` reads, once,

| file (in the image) | size | used as |
|---|---|---|
| `/lib/firmware/brcm/brcmfmac43455-sdio.bin` | 643 651 B | CR4 image, written verbatim to SOCRAM |
| `/lib/firmware/brcm/brcmfmac43455-sdio.clm_blob` | 4 733 B | `clmload` iovar |
| `/lib/firmware/brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt` (fallback `brcmfmac43455-sdio.txt`) | 1 883 B text → 1 728 B chip image | NVRAM at RAM top |

These are the names and layout of Linux `brcmfmac` / linux-firmware. The NVRAM text is converted to
the chip format at run time by `wifi_nvramPack()`, a port of `scripts/gen-wifi-nvram-py.py`. On the
host it produces output **byte-identical** to the Python generator and to the array the driver
shipped with (1 728 B, trailer `0xfe5001af`), plus an edge-case file (CRLF, indented comments, blank
lines, no final newline) identical to the Python output.

Why run time rather than fetching blobs into the devices build: the devices repo then builds from git
alone; the firmware sits beside its licence in the rootfs, as the licence expects; a firmware bump
needs no rebuild; `loader.disk` carries no Cypress bits (the daemon is 186 KB of text now); and the
generated-file spread over three repos is gone from the image build (`gen-wifi-fw-c.sh` is no longer
called by `rebuild-rpi4b-fast.sh`; nothing else in the image used the arrays; `tools/wifi-probe`
keeps them).

A missing or unreadable file disables WiFi with one line naming it, and the image builds and boots
either way.

### 2. It starts at boot

`user.plo.yaml` launches `rpi4-wifi -f` in the nfsroot post-takeover block and in the sd variant
(after `rpi4-audio`). **Why at boot, not on demand from the `wifi` tool:** the netif supervisor
design already assumes a resident daemon. `wifi43455` registers the `wl` netif at every boot, its
join thread waits for `/dev/wifidata` forever and then follows `/etc/wifi.conf`; `wifi connect` only
edits that file. With the daemon at boot, a saved network is rejoined after a reboot with no user
action, which is what `wifi connect`'s help already promised; on demand, nothing would start the
daemon after a reboot, and `wifi scan`/`status` would need their own start logic. Gating the launch
on `/etc/wifi.conf` would break `wifi connect` on a fresh image in the same way.

Details that make the boot start safe:

- **`-f`**: no fork (nothing waits for a detach at boot), and a wait of up to 60 s for the firmware
  files, because on nfsroot the daemon can start before the NFS root holding `/lib/firmware` has
  taken over `/`. Any error is retried during that window (a read can fail mid-takeover); only a
  failure that outlasts it disables WiFi. From the shell (`rpi4-wifi`), one attempt, fork as before.
- **Not on the netboot variant**: its `/` is a RAM dummyfs (the NFS mount is `/mnt`), so the daemon
  could only time out. `scripts/diff-boot-variants.py` shows `rpi4-wifi in: sd,nfsroot` and no other
  new asymmetry.
- **WL_REG_ON via `rpi4-vcmbox`.** The driver opened the raw mailbox FIFO for the chip's power line
  (and leaked the message page on a timeout, by design, because of C1). `libvcmbox.h` says every
  caller must go through the server because the FIFO has no arbitration. From the shell, after boot,
  the race was unlikely; at boot the daemon runs next to thermal, `rpi4-kms` and `rpi4-v3d-async`,
  all mailbox clients. `diag_expGpio()` is now a `vcmbox_call(SET/GET_GPIO_STATE, 8 B, {gpio, state})`,
  and the raw-FIFO code is deleted. This is also one fewer direct-mailbox writer on the Pi (C1's
  signature is a mailbox-shaped `0x8000000x` at PAGE+4), but nothing here claims a C1 effect.
- **One owner of the bus**: every mode first opens `/dev/wifi`; if a live daemon answers, it prints
  `rpi4-wifi: already running (/dev/wifi is served); nothing to do` and exits 0. The lab cycles that
  start with `"rpi4-wifi &"` therefore stay valid. Diagnostic modes (`selftest`, `fwloadbench`,
  `legacypio`) now need the boot daemon killed first.
- **Quiet**: the 16 KB `PHX-DIAG/1 sdio-fwrelease` report is printed only when the firmware fails
  to start (`rpi4-wifi verbose` restores it); the one-line `SDHCI-PIO …` summary, the WL_REG_ON
  readback and `firmware running after bring-up N` are always printed.
- **No idle polling (lwip)**: the netif's RX thread read `/dev/wifidata` in a 200 µs loop as soon as
  the device opened, joined or not: about 4 000 daemon round trips + SDIO probes a second. With the
  daemon at boot that would have been every boot of the default image. It now sleeps 100 ms per turn
  while the link is down (the link comes up only after a successful join).

### 3. Firmware: fetched at build time, pinned, never in git

`scripts/fetch-wifi-firmware.sh` (called by `rebuild-rpi4b-fast.sh` where `gen-wifi-fw-c.sh` was):

- **Source**: linux-firmware, tag **`20260810`**, commit **`2135b2f7714a3a514c989b9728f51f36144cab6f`**,
  from `https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/<path>?id=<commit>`,
  mirror `https://gitlab.com/kernel-firmware/linux-firmware/-/raw/<commit>/<path>`.

  | linux-firmware path | installed as `/lib/firmware/…` | sha256 |
  |---|---|---|
  | `cypress/cyfmac43455-sdio.bin` | `brcm/brcmfmac43455-sdio.bin` | `d408faa9d0d5b1a2f9912dcea53ab0be48217288e398406d117f0edafe7c3edd` |
  | `cypress/cyfmac43455-sdio.clm_blob` | `brcm/brcmfmac43455-sdio.clm_blob` | `15f50a27020b263d1bea215c8f68d0550d912932d1d9ef19ffd59f18d82dd460` |
  | `brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt` | same | `edb6f4e4fb19e18940004124feb4ffe160d72fc607243a07a4480338a28b2748` |
  | `LICENSES/LICENCE.cypress` | `LICENSES/LICENCE.cypress` | `ae0db6cc4db33941148df0f67de53e76a77b1b5a46b3165edb7040aa2750015f` |
  | `LICENSES/GPL-2.0` | `LICENSES/GPL-2.0` | `edaef632cbb643e4e7a221717a6c441a4c1a7c918e6e4d56debc3d8739b233f6` |

  plus a generated `/lib/firmware/WHENCE` giving the tag, commit, per-file sha256 and the WHENCE
  licence lines. Both mirrors served identical bytes (checked 2026-09-30). The `brcm/*.bin` and
  `brcm/*.clm_blob` names are linux-firmware's own `Link:` names for the cypress files (WHENCE
  lines 2938–2941), installed as plain files.
- **Why this commit and not HEAD** (a finding, not a preference): these three files are
  **byte-identical to the `.firmware/` set every Pi WiFi result so far was measured with**
  (firmware 7.45.234 `4ca95bb CY`, 2021-04-15). linux-firmware carried exactly those bytes from
  `f97e316` (2021-06-08) until `a137a09` (2026-08-31), which moved to **7.45.286**. That build's
  feature string drops `idsup-idauth` (the in-firmware supplicant) in favour of `extsae`. `rpi4-wifi`
  hands the firmware the passphrase (`sup_wpa` = 1, `WLC_SET_WSEC_PMK`) and lets it run the WPA2
  4-way handshake, so on 7.45.286 the join would most likely never key. Tag 20260810 is the last
  release before `a137a09`. Moving to newer firmware needs a host-side supplicant first.
- **Cache**: `.firmware/linux-firmware-20260810/` (already gitignored), so a build downloads once and
  is offline afterwards. **Staging**: `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/rootfs-overlay/lib/firmware/`
  (gitignored in the project repo; registered in `publication-audit.sh` as a by-design payload of
  this script), copied into the rootfs by the build's `fs` stage like the game data.
- **Failure behaviour** (all exercised in a scratch cache/overlay, see Validation):
  a download or cached file whose sha256 differs → `[wifi-fw] ERROR …`, exit 1, and the rebuild
  dies before building; a corrupt cached file is re-fetched; no network and nothing cached →
  `[wifi-fw] WARNING: … the image will be built WITHOUT WiFi firmware`, exit 0, image builds, daemon
  logs "WiFi disabled" at boot; no network but an already-staged verified copy → kept.
  `--remove` builds a deliberately firmware-less image; `--cache-only` fetches without staging.
- **Caveat, `auto` scope**: the overlay reaches `_fs` only in the `fs` stage, which the `auto` fast
  path (`project image`) skips. The rebuild script now warns when the overlay has the firmware and
  `_fs` does not; `--scope core` / `project` / `--variant sd` all include `fs`.
- `stage-bcm43455-firmware.sh` (for the standalone `tools/wifi-probe`) now copies from this same
  pinned cache instead of an unpinned `master` URL whose checksum mismatch only printed a NOTE.

**Licence.** WHENCE (commit above) gives both Cypress files `Licence: Redistributable. See
LICENCE.cypress for details.` and the Pi NVRAM text `Licence: GPLv2. See GPL-2.0 for details.`
The binary-distribution grant in `LICENCE.cypress`, verbatim:

> Software Provided in Binary Code Form. This paragraph applies to any Software provided in binary
> code form. Subject to the terms and conditions of this Agreement, Cypress Semiconductor Corporation
> ("Cypress") grants you a non-exclusive, non-transferable license under its copyright rights in the
> Software to reproduce and distribute the Software in object code form only, solely for use in
> connection with Cypress integrated circuit products ("Purpose").

The BCM43455 is a Cypress (now Infineon) part, so an image for the Pi 4 is within the Purpose. The
same licence forbids modification, which is one more reason to ship the files verbatim and do the
NVRAM conversion on the device. The NVRAM text is GPL-2.0 and is its own source; `GPL-2.0` ships
next to it. `check-rootfs-complete.sh` now fails a rootfs that has the firmware without
`LICENSES/LICENCE.cypress`, `LICENSES/GPL-2.0` and `WHENCE`.

### 4. Configuration: no credentials ship

- The image has **no `/etc/wifi.conf`**. `check-rootfs-complete.sh` now fails a built rootfs that
  contains one (it gates both the SD image and the pristine export).
- `/etc/wifi.conf.example` (project rootfs overlay) documents the file (`ssid=` / `psk=`, `#`
  comments), `wifi connect <ssid> <psk>` (writes the file, waits for the lease, joined again after
  every reboot), `wifi disconnect` (removes it), `wifi status`, `wifi scan`, and the routing rule
  (Ethernet keeps the default route while it has an address). The example is inert if copied
  verbatim: every value line is commented out.
- How the file is read is unchanged (`wifi43455` re-reads it every 3 s and compares content), so
  **the lab setup keeps working**: the export's `/etc/wifi.conf` is joined at boot with no command.
  `restore-export-data.sh` already restores it after a pristine export; `sync-netboot-tree.sh` now
  excludes it, so `SYNC_DELETE=1` mirror mode can no longer delete it.
- `wifi connect` creates the file mode 0600 and now rejects, up front, what the join command cannot
  carry: an ssid containing a space (the daemon splits `joinwpa <ssid> <psk>` at the first space)
  and a passphrase outside 8–63 characters (the daemon's buffer holds 63; the old check accepted a
  64-digit hex key that would have been truncated). A passphrase with spaces works in the file but
  not through psh, which passes quotes literally. These are documented limits, not fixed here.

## Validation done (static, no Pi, no image build)

- Both C files of the daemon and client compiled under the **real framework flags** of this target
  (recovered with `make -n` from `.buildroot`; `-std=gnu17 -Wall -Wstrict-prototypes -Wundef
  -Wimplicit-fallthrough -Werror -O2`), to `/dev/null`: clean. A negative control (an injected syntax
  error) fails. The daemon's code had only ever been built by the standalone script (`-Wall -Wextra`,
  no `-Werror`). `scripts/syntax-check.sh` itself could not be used: it compiles the copy in
  `sources/`, and these changes live in worktrees.
- Linked against the buildroot's `libphoenix.a` + `libvcmbox.a`: `nm -u` empty for both binaries.
- `lwip drivers/wifi43455.c` compiled with its real command line: clean.
- The new `wifi/rpi4-wifi/Makefile` dry-run through the framework (`make -n` in the devices
  worktree, `TARGET=aarch64a72-generic-rpi4b`): `all` compiles both sources, links `rpi4-wifi` with
  `libvcmbox.a`, strips both; `install` puts `/sbin/rpi4-wifi` and `/bin/wifi`.
- NVRAM conversion: byte-identical to the Python generator and to the shipped array (above).
- `diff-boot-variants.py` on the new `user.plo.yaml`: renders all three variants;
  `rpi4-wifi;-f` in sd and nfsroot; no duplicate alias.
- `bash -n` on every changed script. `fetch-wifi-firmware.sh` run for real into a scratch
  cache/overlay (`WIFI_FW_CACHE`, `WIFI_FW_DEST`): fresh fetch, re-run (no download), a corrupted
  cached byte (re-fetched), a wrong pinned sha (exit 1, file kept as `.bad`), unreachable mirrors with
  an empty cache (warning, exit 0, nothing staged), unreachable mirrors with a staged copy (kept),
  unreachable mirrors with a corrupt cache (exit 1), `--remove`, `--cache-only`.
  `stage-bcm43455-firmware.sh` run against a scratch root: the three flat files, correct sha256.
- Not validated: the build itself (the framework Makefile, `DEFAULT_COMPONENTS`, the `fs`-stage
  copy of `lib/firmware`), and everything at run time.

## Pre-registered Pi check (for the coordinator)

**Build** (after merging all four branches; devices and lwip are core repos):

```
./scripts/rebuild-rpi4b-fast.sh --scope core            # output must show "[wifi-fw] cache verified" and "staged"/"already staged"
strings -a .buildroot/_boot/aarch64a72-generic-rpi4b/loader.disk | grep -c 'already running (/dev/wifi is served)'   # 1
ls .buildroot/_fs/aarch64a72-generic-rpi4b/root/lib/firmware/brcm/ .buildroot/_fs/aarch64a72-generic-rpi4b/root/lib/firmware/LICENSES/
ls .buildroot/_fs/aarch64a72-generic-rpi4b/root/sbin/rpi4-wifi .buildroot/_fs/aarch64a72-generic-rpi4b/root/bin/wifi
./scripts/check-rootfs-complete.sh .buildroot/_fs/aarch64a72-generic-rpi4b/root    # COMPLETE, and the firmware rows "ok"
./scripts/sync-netboot-tree.sh                          # or make-pristine-nfs-export.sh (then restore-export-data.sh)
```

Before booting: the export must not hold a hand-staged `/bin/rpi4-wifi` from the standalone era
(an old binary without the single-instance guard, found first on `PATH`). The 2026-09-29 pristine
export should have dropped it; if it is there, remove it. Host AP up: `./scripts/radio-ap-up.sh`.
Throughput needs `wifi-perf.py` staged in the export's `/root` (as in cycle T) and, per boot,
`python3 scripts/wifi-perf-host.py 7777 4194304 3` running on the host.

**Cycle W1 — lab export (has `/etc/wifi.conf`), default nfsroot image, AP up:**

```
./scripts/test-cycle-psh-interact.sh --label wifiimg-1 --inter-cmd-secs 8 --idle-secs 60 --max-cmd-secs 200 -- \
    "wifi status" "wifi status" "ping -c 5 10.43.0.1" \
    "python3 /root/wifi-perf.py 10.43.0.1 7777 10.43.0.89 4194304 3" \
    "rpi4-wifi" "wifi connect a b" "wifi stats"
```

Predicted, in the boot log (no command typed yet):

| # | line | meaning if absent |
|---|---|---|
| W1.1 | `rpi4-wifi: loaded /lib/firmware/brcm/brcmfmac43455-sdio.bin (643651 B), clm_blob (4733 B), nvram (1728 B chip image)` | firmware not in the rootfs (fs stage, fetch) or the 60 s wait too short: look for `rpi4-wifi: WiFi disabled: cannot read …` |
| W1.2 | `rpi4-wifi: SDHCI base clock (EMMC) = 250000000 Hz` (as in every archived boot) and `rpi4-wifi: WL_REG_ON readback=1 (expect 1)` | the vcmbox path (new): `WL_REG_ON on failed (mailbox)` would be a regression of this change |
| W1.3 | `rpi4-wifi: SDHCI-PIO mode=level fw_bytes=643648 rc_w=0 rc_nvram=0 …` (the archived value for this firmware) then `rpi4-wifi: firmware running after bring-up 1` | download failure; the retry prints `firmware did not start (bring-up 1 of 3)` |
| W1.4 | `rpi4-wifi: registered /dev/wifi (write "scan", then read the AP list)` | — |
| W1.5 | **no** `PHX-DIAG/1 sdio-fwrelease` block | the quiet default failed (or the firmware failed and it printed on purpose) |
| W1.6 | `lwip: wifi43455: /dev/wifidata + /dev/wifi open`, `lwip: wifi43455: MAC …`, `lwip: wifi43455: joining "PhoenixNet" (WPA2 associate + 4-way key, 20-40s)`, `lwip: wifi43455: joined "PhoenixNet"; link up`, `lwip: wifi43455: dhcp_start: 0 (0=ok); waiting for a lease` | the auto-join from the export's file, with no command typed |
| W1.7 | the boot still reaches `(psh)%`; no new fault dumps | boot-time interaction with the other servers |

Predicted command output:

| command | predicted |
|---|---|
| `wifi status` (2nd) | `wanted:  "PhoenixNet" (/etc/wifi.conf)`, `daemon:  STATUS joined=1 ssid=PhoenixNet losses=0`, `address: 10.43.0.89 (wl2)` (the AP's lease for this MAC: all 24 archived `wifi status` address lines say 10.43.0.89) |
| `ping -c 5 10.43.0.1` | 5 of 5 replies |
| `python3 /root/wifi-perf.py …` | `WIFIPERF-MEDIAN runs=3 tx=3.6x MB/s … rx=3.3x MB/s …`, within the cycle-T master ranges (TX 3.45–3.62, RX 3.23–3.31) |
| `rpi4-wifi` | `rpi4-wifi: already running (/dev/wifi is served); nothing to do`, and WiFi keeps working (`wifi stats` after it) |
| `wifi connect a b` | `wifi: WPA2 passphrase must be 8-63 characters`; `/etc/wifi.conf` untouched (validation runs before any write) |

**Cycle W2 — follow the file without typing the PSK** (never `wifi disconnect` on the lab export: it
deletes the only copy of the lab file):

```
./scripts/test-cycle-psh-interact.sh --label wifiimg-2 --inter-cmd-secs 8 --idle-secs 60 --max-cmd-secs 120 -- \
    "wifi status" "mv /etc/wifi.conf /etc/wifi.conf.off" "wifi status" \
    "top -n 1" "mv /etc/wifi.conf.off /etc/wifi.conf" "wifi status" "wifi status"
```

Predicted: first status joined with an address; after the move, within ~6 s, `lwip: wifi43455:
leaving "PhoenixNet"` and `lwip: wifi43455: no credentials (boot cfg empty, no ssid=/psk= in
/etc/wifi.conf); waiting -- run \`wifi connect <ssid> <psk>\``, and `wifi status` shows `wanted:
none`, `daemon:  STATUS joined=0 …`, `address: none`. `top -n 1` while unassociated: lwip and rpi4-wifi
near 0 % CPU (the new link-down sleep; before it, ~4 000 wakeups/s). After moving the file back: a
new `joining` / `joined` / `dhcp_start` sequence and an address again within ~60 s. **Check the file
is back** (`wifi status` shows `wanted:  "PhoenixNet"`) before ending the session. If the cycle dies
between the two moves, the export is left with `/etc/wifi.conf.off`: rename it back on the host
(`sudo mv <export>/etc/wifi.conf.off <export>/etc/wifi.conf`) — nobody needs to know the PSK for that.

**Cycle W3 — fresh image, owner-attended (persistence), optional:** an SD image (it has no
`/etc/wifi.conf`) with the AP up. Boot log: W1.1–W1.5 and `lwip: wifi43455: no credentials …`.
At the console, typed by hand so the PSK is not in any scripted command list: `wifi connect
PhoenixNet <psk>` → `wifi: connecting to "PhoenixNet" (join 20-40 s, then DHCP)...` then `wifi:
connected, address 10.43.0.x (wl2)`. Reboot: joined with no command (W1.6). Then `wifi disconnect`
→ `wifi: disconnected`, and the next boot stays unassociated.

**Regression gate**: one standard showcase gate on the new image (the daemon now runs during every
game). Note for GPU benches: with the export's `/etc/wifi.conf` and the AP up, every lab boot now
joins WiFi, and a joined netif still polls (the 200 µs loop). To keep bench conditions as before,
run GPU benches with the AP down (`./scripts/radio-ap-down.sh`): the netif then retries the join at
most once a minute and does not poll.

| outcome | reading |
|---|---|
| W1.1–W1.7 and the command table as predicted | WiFi ships; F1's "ships in no image" is closed |
| `WiFi disabled: cannot read …` | the firmware did not reach the rootfs: check the `[wifi-fw]` line of the build and whether the stage list had `fs` |
| W1.3 fails but a shell restart works | a boot-time interaction (vcmbox load, timing): compare `SDHCI-PIO` timing fields with the shell runs of 09-28 |
| joined but no lease, or a lease only after a delay | the lwip link-down gating (c7e9d14): revert it alone and re-run W1 |
| throughput somewhat below the cycle-T range, everything else as predicted | first suspect the compiler flags, not the design: the daemon is now built by the framework (`-std=gnu17 -O2 -mstrict-align -fomit-frame-pointer -ffunction-sections`), cycle T's by `build-standalone.sh` (`-O2 -std=gnu11`). Neither boot start nor the lwip change touches the joined data path |
| a showcase-gate regression | kill `rpi4-wifi` in a re-run to attribute it |

## Follow-ups (not done here)

- An ssid with a space and a 64-digit hex PSK: the `/dev/wifi` join command needs a framing that can
  carry them (e.g. length-prefixed fields), in the daemon, lwip and `wifi` together.
- Interrupt-driven RX would remove the joined-state polling cost (the WIFI_RX_* notes in the daemon).
- Newer firmware (7.45.286+) needs a host-side supplicant, because it has no `idsup`.
- User docs (P4): the `wifi` commands and `/etc/wifi.conf.example`; `docs/done/wifi-firmware-blobs.md`
  describes the old compiled-in scheme and is superseded by this doc.
