# Lab setup (portable, since 2026-10-15)

The bench works the same in any location: the laptop brings its own networks and only needs the
iPhone for internet.

```
 iPhone ──USB-C (Personal Hotspot)──► laptop: enxc6c17d65f469, DHCP 172.20.10.x, the default route
                                       │
                                       ├─ enp1s0f0 (built-in Ethernet) 10.42.0.1/24 ──switch──► Pi 4 eth0
                                       │     netboot server: DHCP, TFTP, NFS root (scripts/netboot-server-up.sh)
                                       │     NAT for 10.42.0.0/24 out of any uplink (scripts/pi-internet-nat.sh)
                                       │
                                       └─ wlp3s0 (WiFi) soft-AP "PhoenixNet" 10.43.0.1/24, 2.4 GHz ch 6, WPA2
                                             NetworkManager profile phoenix-ap, ipv4.method=shared: DHCP + DNS
                                             + NAT out of any uplink. Clients: the Meross plug (→ its cloud),
                                             and the Pi's own WiFi when used.
```

- **Internet:** the iPhone over USB-C with Personal Hotspot on. Whatever holds the default route is
  the uplink; both NATs follow it, so venue Ethernet or another phone work too.
- **Pi link:** the laptop's **built-in** Ethernet, static `10.42.0.1/24` in
  `/etc/netplan/00-installer-config.yaml` (it used to be a DHCP client; the old file is
  `00-installer-config.yaml.bak-20261015`). `.env.local` has `RPI4B_NETBOOT_IFACE=enp1s0f0`. The old
  USB adapter profile `pi-netboot` (`enx00e04c680f3e`) is kept with autoconnect off — don't plug that
  adapter in as well, or two NICs claim 10.42.0.1.
- **Power:** the Meross MSS620 plug, switched through the Meross cloud by
  `/home/houp/meross-plug/plug.py` (`scripts/pi_power_on.sh` / `pi_power_off.sh`). It must be paired
  to the account in `/home/houp/ubuntu-setup/meross` and joined to **PhoenixNet**.

Check everything with one command — every line must say OK:

```
./scripts/netboot-server-up.sh     # once after the laptop boots
./scripts/demo-preflight.sh        # boot image, dnsmasq, NIC, NFS, uplink, NAT, internet, soft-AP, plug, UART
```

## Moving the Meross MSS620 plug to PhoenixNet

The plug keeps the Wi-Fi it was paired on, so in a new place it must be paired again (once; after that
it rejoins PhoenixNet by itself wherever the laptop is). The plug is 2.4 GHz only and does not do WPA
Enterprise — PhoenixNet is 2.4 GHz WPA2-PSK, so it fits.

1. **On the laptop:** make sure the soft-AP is up (`./scripts/demo-preflight.sh` → `soft-ap OK`) and
   get its password:
   ```
   ! nmcli -s -g 802-11-wireless-security.psk connection show phoenix-ap
   ```
2. **In the Meross app** (iPhone, the same Meross account `plug.py` uses): if an old MSS620 entry is
   still listed, remove it (on 2026-10-15 the account lists no device, so nothing to remove).
3. **On the plug:** plug it in near the laptop, then **press and hold its button for 5 seconds** — this
   factory-resets it and puts it into pairing mode (the status LED starts blinking).
4. **In the app:** `+` → add device → Smart Plug → **MSS620**, and follow the steps. The app asks you to
   join the plug's own Wi-Fi (`Meross_SW_…`) in iPhone Settings → Wi-Fi, then to go back to the app.
   When it asks for the Wi-Fi network for the plug, choose or type **PhoenixNet** and the password
   from step 1. Wait until the app says the device was added.
   - While the iPhone is on the plug's Wi-Fi, its USB tether to the laptop may pause; it comes back
     when the iPhone leaves that network.
5. **Check on the laptop:**
   ```
   /home/houp/meross-plug/plug.py status       # "<name> ch0: ON" or "OFF"
   ./scripts/demo-preflight.sh                 # plug OK
   ```
   The plug also appears in NetworkManager's lease file for the AP:
   `/var/lib/NetworkManager/dnsmasq-wlp3s0.leases`.

If pairing in the app fails: keep the plug within a few metres of the laptop for the first join, make
sure the app is not offered a 5 GHz network, and repeat from step 3. The plug's button always toggles
power by hand, which is the fallback for powering the Pi if the cloud is unreachable.
