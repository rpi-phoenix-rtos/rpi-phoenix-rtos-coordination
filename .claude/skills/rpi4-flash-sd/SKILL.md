---
name: rpi4-flash-sd
description: Flash a new Phoenix-RTOS SD image to the Pi's card WITHOUT touching the hardware — netboot Phoenix, dd the image from the NFS root onto /dev/mmcblk0, then switch lanes and boot it. Use whenever the card needs updating, including unattended overnight.
---

# Updating the Pi's SD card without a human

The card **stays in the Pi**. Phoenix flashes its own card: netboot Phoenix (the card is
irrelevant to lane choice — dnsmasq picks it), `dd` the image from the NFS root onto
`/dev/mmcblk0`, drop dnsmasq, power-cycle, and the Pi boots what you just wrote.

No Linux netboot image is needed. Phoenix has the `bcm2711-emmc` driver (`/dev/mmcblk0`) and
coreutils `dd` on the export — that is the whole toolchain.

First proven end-to-end 2026-09-19: `b95e983a` → `6012dd0d` with no human intervention.

## Use `/usr/bin/dd` — but for its *rate line*, not for speed

↩ **RETRACTED 2026-09-20. This section used to say "`/bin/dd` is busybox and runs at ~0.65 MB/s
against coreutils' ~12.3 — a ~19× difference". That is false, and it was never measured.**
A direct A/B on the Pi, same source, same sink, same bytes, both binaries in one boot
(`ddbench`, 64 MiB to `/dev/mmcblk0`):

| bs | `/bin/dd` (busybox) | `/usr/bin/dd` (coreutils) |
|---|---|---|
| 1M | 5 s | 5 s (self-reported 12.9 MB/s) |
| 128k | 6 s | 6 s (self-reported 11.8 MB/s) |

**The two tools are the same speed.** The "28-minute flash" was **~25 minutes of
`--idle-secs 1500` with the Pi sitting idle after `dd` had already finished** — the same
harness artefact this file warns about two paragraphs down, which I then mis-attributed to
busybox. The real flash took ~90 s in both eras once the ADMA2 write path landed.

✅ **What survives, and is still the reason to prefer `/usr/bin/dd`:** coreutils `dd` prints its
own `bytes copied, N s, X MB/s` line, and this busybox is built with
`CONFIG_FEATURE_DD_THIRD_STATUS_LINE` **off**, so it prints only record counts. On a bench where
wall-clock has now produced two wrong conclusions, a tool that times itself is worth using —
but choose it for the measurement, not for the transfer.

**Budget ~90 seconds for a 1.1 GB image** on the ADMA2 driver (measured
2026-09-20: 1 139 949 568 bytes in 88.6 s = 12.9 MB/s, card flashed by the ADMA2 write path and then
booted from).

⚠ **Each binary can do only one direction.** coreutils `dd` **cannot read** `/dev/mmcblk0` —
`dd: cannot fstat '/dev/mmcblk0': Function not implemented` — and it then produces a **0-byte file**,
which looks exactly like a broken driver rather than a broken tool. So:

* write **to** the device → `/usr/bin/dd` (coreutils — reports its rate)
* read **from** the device → `/bin/dd` (busybox — the only one that can)

⚠ **Do not time transfers with the harness.** `test-cycle-psh-interact.sh --idle-secs N` waits N
seconds of UART idle **after each command**, so bracketing a command with `date` measures the
harness's cadence, not the device: three unrelated operations once all "took" exactly 312 s that
way. Read the rate coreutils `dd` reports instead.

## The procedure

### 1. Build and stage the image

```
./scripts/rebuild-rpi4b-fast.sh --variant sd --with-tests --with-ports --with-showcase
cp artifacts/rpi4b/rpi4b-sd-2part.img /srv/phoenix-rpi4-nfs-gcc16/sdimage.img
```

⚠ `--variant sd` **overwrites the TFTP `loader.disk`** with an SD blob that has no `nfs;/`, so
every netboot cycle then refuses. Rebuild the netboot loader **before** flashing — you need
netboot working to do the flash at all:

```
./scripts/rebuild-rpi4b-fast.sh --with-tests --with-ports --with-showcase
./scripts/check-netboot-blob.sh        # must print: rootfs: nfsroot
```

The staged image appears on the Pi as `/sdimage.img` (the export is the Pi's `/`). Delete it from the export once
the card boots — it sits in the root directory every screenshot of a file manager shows.

### 2. Verify the image before writing it

Never flash an unverified image — a bad one costs 30 min plus a recovery flash:

```
./scripts/qemu-boot-sdimage.sh          # must print: SD variant, as expected + PASS
```

### 3. Flash from Phoenix (netboot lane, dnsmasq UP)

```
./scripts/test-cycle-psh-interact.sh --label sdflash \
    --idle-secs 200 --max-cmd-secs 260 -- \
    "/usr/bin/dd if=/sdimage.img of=/dev/mmcblk0 bs=1M"
```

⚠ **Size `--idle-secs` to the transfer, not generously.** The cycle sits out the *whole* idle
window after `dd` returns, so `--idle-secs 1500` turns a 90-second flash into a 28-minute cycle —
which is exactly how this file came to blame busybox for a 19× slowdown that does not exist. ~200 s
covers a 1.1 GB image with margin.

Success looks like `1087+1 records in / 1087+1 records out` (records = image bytes / 1 MiB).
⚠ **Check the record count against the image size.** A short write is the one failure that
still leaves a plausible-looking log.

When dd finishes, the cycle sits out `--idle-secs` doing nothing. `kill -TERM` the
**`psh-interact.py`** child; the parent's EXIT trap then powers the Pi off.

### 4. Switch lanes and boot what you wrote

```
./scripts/netboot-server-down.sh
./scripts/test-cycle-netboot.sh --sd-boot --capture-secs 200 --label sdboot-new
```

**Grade by the ROOT ARG, never by `bcm2711-emmc`** — that driver starts on *both* lanes, so its
presence proves nothing:

* SD boot  ⇒ `bcm2711-emmc;-r;/dev/mmcblk0p2:ext2`, and **no** `nfs;/;10.42.0.1`
* netboot  ⇒ `nfs;/;10.42.0.1;/;v4;takeover`

Then confirm you are running the image you *meant* to write — `rpi4-sysinfo` prints
`/etc/build-versions` on every boot, so compare those SHAs against the tree you built from.
A card that boots the *old* image looks identical to success until you check this.

### 5. Restore the bench

```
./scripts/netboot-server-up.sh     # netboot is the default lane; leave it up
```

## Safety, and the one way this bricks the bench

**Keep dnsmasq UP for the whole of steps 1–3.** While the server is up the Pi netboots, so a
failed or interrupted flash is recoverable: netboot again and re-flash. Only drop it once dd
has reported the full record count.

⛔ **The unrecoverable case:** a **blank** card stops the Pi booting *at all* — 0 DHCP, 0 UART,
black HDMI, observed over four power-ons 2026-09-16, and it does **not** fall through to the
network despite `BOOT_ORDER=0xf12`. An interrupted `dd` leaves a *partially written* card,
which is untested territory. The dangerous window is small (dd writes the MBR in its first
moments, so the card has a valid partition table almost immediately) but it is real. If the Pi
ever stops responding on both lanes, that is the one situation needing the owner.

⚠ **Probe writes damage the card.** `seek=` into the ext2 area corrupts the running rootfs, so
any throughput probe commits you to completing a full flash. Do not "just measure" unless you
intend to flash.

## Related

* `rpi4-run` — running commands on the Pi generally.
* `feedback_selfflash_sd_via_netboot_linux` (memory) — lane selection, the blank-card finding,
  and the older Linux-netboot variant of this loop (no longer needed).
* `docs/misc/2026-09-19-ntp-clock-step-breaks-app-startup.md` — why a bench waits for the clock
  step before launching timing-sensitive commands.
