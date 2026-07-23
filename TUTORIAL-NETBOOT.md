# Phoenix-RTOS on Raspberry Pi 4 — Netboot Setup Tutorial (advanced)

Build Phoenix-RTOS **from source** on a Linux host and boot your Pi 4 entirely
over the network — **DHCP + TFTP** for the boot files and **NFS** for the root
filesystem — with **no SD card in the Pi** after a one-time EEPROM update.

This is the developer workflow: every rebuild is live the moment it finishes,
you deploy new binaries by dropping them into the NFS export, and you never
shuffle an SD card again.

It assumes you start with **nothing** — everything is pulled from the public
`rpi-phoenix-rtos` GitHub org and built locally. It's more involved than the
[SD-card quick start](TUTORIAL.md); you should be comfortable with Linux
networking, `sudo`, and editing config files.

> **Hardware tested:** validated only on a **Raspberry Pi 4 Model B with 4 GB
> RAM**. Other Pi 4 variants / boards are untested.

---

## How netboot works here (the big picture)

```
   Raspberry Pi 4                          Linux host
  ┌──────────────┐                       ┌───────────────────────────────────┐
  │ EEPROM boot- │   1. DHCP DISCOVER    │  dnsmasq  (netboot-server-up.sh)   │
  │ loader (net  │──────────────────────▶│   • hands out 10.42.0.x lease      │
  │ boot enabled)│◀──────────────────────│   • option 66 = 10.42.0.1 (TFTP)   │
  │              │   2. TFTP: start4.elf,│   • TFTP root = .buildroot/…/       │
  │              │      config.txt, arm- │        rpi4b-bootfs  (served live)  │
  │  plo ─▶ Phoenix     stub, kernel8.img,│                                     │
  │              │      loader.disk, dtb │  nfs-kernel-server                  │
  │  nfsroot: /  │   3. NFS mount        │   • exports /srv/phoenix-rpi4-nfs   │
  │  over NFS ───┼──────────────────────▶│        (fsid=0) → 10.42.0.0/24      │
  └──────────────┘                       └───────────────────────────────────┘
        RJ45 ────────── dedicated NIC (or small switch) ──────────
```

1. The Pi 4's **EEPROM** is set to network-boot (one-time). On power-up it does
   DHCP, learns the TFTP server, and pulls the firmware + `plo` + Phoenix boot
   image over **TFTP**.
2. `plo` starts Phoenix. In the **`nfsroot`** variant, a small RAM root comes up
   first, networking (lwip/genet) gets a DHCP lease, then the Pi **mounts
   `10.42.0.1:/` over NFS and takes it over as `/`**.
3. You edit/rebuild on the host; the NFS export and the TFTP tree update in
   place — just power-cycle the Pi.

**Why a native build (not Docker):** the servers read the build products
*directly* from the buildroot on the host — TFTP from `.buildroot/…/rpi4b-bootfs/`
and NFS from the staged rootfs. A Docker build would trap those inside a
container image, so for netboot you build natively on the host that also runs
the servers.

---

## 1. Prerequisites

**Hardware**
- Raspberry Pi 4 Model B (4 GB), USB-C power (a **smart plug** is handy for
  hands-free power-cycling but not required).
- A **dedicated wired link** between host and Pi. Best: a **second/USB-Ethernet
  NIC** on the host, cabled to the Pi — optionally through a **small unmanaged
  switch** (recommended; a direct cable makes the host's link state follow the
  Pi's power state, which can wedge some setups). A regular straight cable is
  fine (the Pi's PHY is Auto-MDIX).
- A microSD card used **once** to update the Pi's EEPROM (then removed).
- *Recommended:* a USB-serial adapter on GPIO 8/10 (GND/Pi-TXD) at **115200 8N1**
  to watch `plo`/kernel output — invaluable when a netboot doesn't come up.

**Host** — a Linux machine (Ubuntu 24.04 validated). You'll install:
- The Phoenix build dependencies (handled by the bootstrap script below).
- **`dnsmasq`** (DHCP + TFTP), **`nfs-kernel-server`** (NFS), and
  **`sfdisk` + `mtools`** (for the one-time EEPROM SD image).
- ~15 GB free disk; the first build (toolchain included) takes **~1.5–2 h**.

> **Networking heads-up:** this guide uses the subnet **`10.42.0.0/24`** with the
> host at **`10.42.0.1`** — the address the Phoenix image mounts NFS from by
> default. If NetworkManager/systemd already manage your chosen NIC, set it to
> "unmanaged" (or use a NIC nothing else touches) so it doesn't fight the static
> IP. Don't run this DHCP server on a NIC that shares a LAN with another DHCP
> server.

---

## 2. Get the sources and build from source

Clone the coordination repo (it carries every build + netboot script) and let
the bootstrap pull all sibling repos from the org and build the cross toolchain:

```bash
git clone https://github.com/rpi-phoenix-rtos/rpi-phoenix-rtos-coordination.git ~/phoenix-rpi
cd ~/phoenix-rpi
./scripts/bootstrap-linux-host.sh        # installs deps, clones sources, builds the toolchain (~40 min)
```

Build the **`nfsroot`** variant with the full application showcase:

```bash
./scripts/rebuild-rpi4b-fast.sh --variant nfsroot --with-showcase --with-ports
```

This produces the two things the servers will hand to the Pi:

| Artifact (under `~/phoenix-rpi/.buildroot/`) | Role |
|---|---|
| `_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/` | **TFTP root** — `start4.elf`, `fixup4.dat`, `config.txt`, `phoenix-armstub8-rpi4.bin`, `kernel8.img` (`plo`), `loader.disk` (Phoenix), the DTB + overlays |
| `_fs/aarch64a72-generic-rpi4b/root/` | the fully **staged root filesystem** you'll serve over NFS |

For the toolchain/native-build details and options, see
[docs/BUILD.md](docs/BUILD.md).

---

## 3. Set up the host↔Pi network + DHCP/TFTP server

1. **Identify the NIC** cabled to the Pi:
   ```bash
   ip -br link          # e.g. enx00e04c68013a (USB) or eth1
   ```
2. **Point the netboot scripts at it** via a gitignored `.env.local` in the repo
   root (the only line you normally need):
   ```bash
   echo 'RPI4B_NETBOOT_IFACE=enx00e04c68013a' >> ~/phoenix-rpi/.env.local
   ```
   Optional overrides (defaults shown): `RPI4B_NETBOOT_HOST_IP=10.42.0.1`,
   `RPI4B_NETBOOT_DHCP_LO=10.42.0.10`, `RPI4B_NETBOOT_DHCP_HI=10.42.0.20`.
3. **Install dnsmasq** (if the bootstrap didn't) and **start the server**:
   ```bash
   sudo apt-get install -y dnsmasq
   ./scripts/netboot-server-up.sh
   ```

`netboot-server-up.sh` pins `10.42.0.1/24` on the NIC, writes a fresh dnsmasq
config, and launches it serving:
- **DHCP** on `10.42.0.10–20` (12 h leases), advertising **TFTP server
  `10.42.0.1`** (DHCP option 66 — what the Pi 4 firmware reads);
- **TFTP** rooted **directly at the buildroot `rpi4b-bootfs/`** — so every
  rebuild is served live, no copy step (`tftp-no-blocksize` is set for Pi 4
  firmware compatibility).

You should see `=== netboot server up ===` with the iface/dhcp/tftproot summary.
Manage it with `./scripts/netboot-server-{status,down,restart}.sh`.

> dnsmasq needs privileged ports, so these scripts use `sudo`. If NetworkManager
> keeps reclaiming the NIC or the address, mark the device unmanaged first.

---

## 4. Set up the NFS root

Install the server, create the export directory, and **populate it from the
build's staged rootfs**:

```bash
sudo apt-get install -y nfs-kernel-server rsync
sudo mkdir -p /srv/phoenix-rpi4-nfs
sudo rsync -aH --delete \
  ~/phoenix-rpi/.buildroot/_fs/aarch64a72-generic-rpi4b/root/ \
  /srv/phoenix-rpi4-nfs/
```

Declare the export (drop-in file so it's isolated from the rest of `/etc/exports`):

```bash
echo '/srv/phoenix-rpi4-nfs 10.42.0.0/24(rw,sync,no_subtree_check,no_root_squash,insecure,fsid=0)' \
  | sudo tee /etc/exports.d/phoenix-rpi4.exports
sudo exportfs -ra
sudo systemctl enable --now nfs-kernel-server
sudo exportfs -v          # confirm /srv/phoenix-rpi4-nfs is listed
```

Key points:
- **`fsid=0`** makes this the NFSv4 root, so the image's `mount 10.42.0.1:/`
  resolves to this directory.
- **`no_root_squash`** lets the Pi (running as root) own files normally.
- The Phoenix image mounts **`10.42.0.1:/`** — keep the host at `10.42.0.1`, or
  you'd also have to change the server address baked into the boot config.

---

## 5. Enable network boot on the Pi (one time)

The Pi 4 only netboots if its **EEPROM boot order** allows it. The repo builds a
tiny SD image that reconfigures the EEPROM:

```bash
sudo apt-get install -y mtools fdisk        # sfdisk + mformat/mcopy
./scripts/prepare-pi-eeprom-netboot.sh
# -> artifacts/eeprom-netboot/eeprom-prep-sd.img
```

It sets `BOOT_ORDER=0xf12` (network first, SD recovery fallback), `TFTP_PREFIX=2`
(files at the TFTP root, no per-serial subdirectory — matching our server), and
`BOOT_UART=0` (leaves the UART alone so `plo`/Phoenix output stays clean).

Apply it **once**:
1. Flash `eeprom-prep-sd.img` to any spare microSD (same `dd` procedure as the
   [SD tutorial](TUTORIAL.md#3-flash-the-sd-card)).
2. Insert it in the Pi and power on. The green LED **blinks rapidly** on success
   (~10 s).
3. Power off and **remove the card**. The EEPROM is now updated permanently.

(Alternatively, use **Raspberry Pi Imager → Bootloader / EEPROM configuration →
Network boot**.)

---

## 6. Boot the Pi over the network

1. Make sure the server is up (`./scripts/netboot-server-status.sh`) and NFS is
   exported (`sudo exportfs -v`).
2. Connect the Pi's **Ethernet** to your NIC (no SD card needed), attach the
   optional serial adapter, and power on.

The Pi does DHCP → TFTP-pulls the firmware + `plo` + `loader.disk` → `plo` boots
Phoenix → a RAM root comes up → lwip/genet gets a lease → **NFS takes over `/`**
→ you reach the `(psh)%` prompt.

**Watch it happen:**
- On the host: `tail -f artifacts/netboot/dnsmasq.leases` (a lease appears), and
  `sudo tail -f artifacts/netboot/dnsmasq.log` (you'll see `TFTP … sent …
  start4.elf`, `loader.disk`, etc.).
- On the Pi: the serial console (or HDMI) shows the boot log; expect
  `lwip: genet…: link up`, a DHCP `OFFER`, then `nfs-fs: mounted 10.42.0.1:/ via
  v4` and a psh prompt.

Once at psh, everything from the [SD tutorial's app section](TUTORIAL.md#6-the-showcase--what-to-try-and-how-to-start-it)
works the same way (`startx wmaker`, `mc`, `micropython`, …).

> There's also a fully automated one-shot cycle for iteration:
> `./scripts/test-cycle-netboot.sh --label mytest` (power-cycles the Pi, captures
> the UART, restarts the server on a DHCP timeout). It expects a smart-plug power
> helper; see the script header.

---

## 7. The development loop (the payoff)

No more flashing — rebuild and redeploy live:

```bash
# 1. Change code in sources/…, then rebuild:
./scripts/rebuild-rpi4b-fast.sh --variant nfsroot --with-showcase --with-ports

# 2. Re-sync the rootfs to the NFS export (TFTP already serves the new boot files live):
sudo rsync -aH --delete \
  ~/phoenix-rpi/.buildroot/_fs/aarch64a72-generic-rpi4b/root/ /srv/phoenix-rpi4-nfs/

# 3. Power-cycle the Pi — it netboots the new build.
```

You can even drop a single new binary straight into the export
(`/srv/phoenix-rpi4-nfs/bin/…`) and run it on the Pi without a full rebuild —
handy for iterating on one program. (First access to a just-added file can miss
the client-side dir cache and `ENOENT` once; retry and it's there.)

---

## 8. (Optional) Quake data over NFS

A native build doesn't fetch the Quake game data (the Docker image does). To play
GLQuake over netboot, drop the freely-redistributable **shareware** `pak0.pak`
into the export:

```bash
sudo mkdir -p /srv/phoenix-rpi4-nfs/usr/share/quake/id1
# fetch + extract the shareware pak0 (see TUTORIAL.md for the source), then:
sudo cp pak0.pak /srv/phoenix-rpi4-nfs/usr/share/quake/id1/
```
Then run `rpi4-quake` on the Pi. (Engine-only without it.)

---

## 9. Verify / troubleshoot

| Check | Command / expectation |
|---|---|
| Server running | `./scripts/netboot-server-status.sh` → `dnsmasq running` + iface has `10.42.0.1/24` |
| Pi got a lease | `cat artifacts/netboot/dnsmasq.leases` shows the Pi's MAC/IP |
| TFTP serving | `sudo tail artifacts/netboot/dnsmasq.log` → `sent … start4.elf` / `loader.disk` |
| NFS exported | `sudo exportfs -v` lists `/srv/phoenix-rpi4-nfs`; `showmount -e 10.42.0.1` shows it |
| Pi never DHCPs | EEPROM not network-boot? Re-do §5. NIC link down / wrong `RPI4B_NETBOOT_IFACE`? Check `ip -br link`. |
| TFTP times out | Firmware dislikes blocksize negotiation — already handled (`tftp-no-blocksize`); confirm the log shows the request arriving at all (cable/NIC). |
| Boots but no `/` | NFS: `exportfs -v`, firewall on the NIC, and the host really at `10.42.0.1`. Watch for `nfs-fs: mounted …` on serial. |
| Address conflict | Don't run this on a NIC sharing a LAN with another DHCP server; use the dedicated link. |

**Caveats specific to netboot:**
- **Exec-from-NFS is slower than SD** — first launch of a large binary (Quake,
  WindowMaker) pages in over the network; expect a startup delay.
- A **direct crossover cable** may negotiate **100 Mbit** (only two pairs wired),
  throttling NFS; a Cat5e/6 straight cable or a gigabit switch gives full speed.
  A switch also avoids the host-link-follows-Pi-power wedge.
- Everything else (app caveats, Wi-Fi unsupported, 4 GB-only) is as in
  [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md).

---

Prefer the simple path? The SD-card walkthrough is in [TUTORIAL.md](TUTORIAL.md).
Feedback and issues welcome.
