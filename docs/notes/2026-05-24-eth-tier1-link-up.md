# Eth Tier 1 complete — GENET v5 link-up validated on hardware

Hardware: Raspberry Pi 4-B (BCM2711, GENET v5 + BCM54213PE PHY).
Logs: `artifacts/rpi4b-uart/rpi4b-uart-20260524-2*-netboot-tier1-genet-*.log`.

## Tier 1 deliverable

UART line reached on the third iteration:

```
lwip: genet@fd580000: SYS_REV_CTRL=0x06000000 (major=6 minor=0)
lwip: genet@fd580000: no firmware MAC in UMAC_MAC{0,1}; using fallback
lwip: genet@fd580000: MAC 02:b8:27:eb:00:01
lwip: genet@fd580000: MDIO bus 0
lwip: ephy0.1: link is DOWN 0Mbps/Half (ctl 1140, status 7949, adv 01e1, lpa 0000)
[0J(psh)% lwip: genet@fd580000: link up: 100 Mbps full-duplex
```

That's: GENET v5 silicon ID recognized, MDIO bus exposed, BCM54213PE
auto-negotiation completed against the netboot bridge's USB-Ethernet
partner (100M-capped), netif transitioned to NETIF_FLAG_LINK_UP.

## Findings worth keeping

### 1. UMAC_MAC0/MAC1 are zero on Pi 4 at lwip start

The first run printed `MAC 00:00:00:00:00:00`. The Pi 4 VideoCore firmware
does **not** push the board's burned-in MAC into the GENET UMAC registers
— Linux fetches it from the device tree's `local-mac-address` property
(populated by VideoCore via the `bcm2835-firmware` mailbox channel) and
then writes it back to UMAC. Phoenix has no DT/mailbox plumbing in
userspace yet, so Tier 1 falls back to a locally-administered MAC.

This is filed as `TODO(TD-Eth-MAC)` in `bcm-genet.c`. Plumbing for the
real MAC is part of Tier 4 (TD entry to be added when implementing).

### 2. BCM54213PE link-state arrives ~3 s after AN restart

ephy's one-shot `ephy_setLinkState` call at init runs immediately after
the `ephy_restartAN` AN restart and sees `BMSR=0x7949` — AN-incomplete,
link-down. With `irq:MAC` ephy doesn't spawn an internal poller, so
the first run reported link DOWN and never updated.

Fix: GENET driver now owns a 1 Hz link-state poll thread that calls
back into `genet_setLinkState` on transitions. This is also what
production drivers (Linux GENET, FreeBSD `if_genet`) do — IRQ-driven
link state is a v5+ EXT_PHY_DET_LINK_STATUS feature on this same
register, but using the IRQ requires the interrupt path to be wired
(Tier 4+). Polling is the right Tier-1 answer.

### 3. PHY-mode is `rgmii-rxid`, not `rgmii`

Tier 0 research docs say `rgmii`. The actual Pi 4 device tree says
`rgmii-rxid` (internal RX delay added by the GENET block). The driver
sets `RGMII_MODE_EN=1, ID_MODE_DIS=0`. Without this, packet reception
in Tier 3 will see timing-skew errors.

### 4. ephy.c needed a one-line addition for BCM54213PE

Added `ephy_bcm54213pe` enum + a `linkSpeed` helper that reads the
Aux Status Summary register (reg 0x19, HCD field at bits 10:8). Five
lines of bcm54213pe-aware code; the rest fell out of the existing
BMCR/ANAR/AN-restart paths.

The lpa=0x0000 print at init time is harmless — that's the Link
Partner Ability register at moment-zero of AN; it fills in once the
partner's FLP bursts are decoded.

## What we don't have yet

- TX path (Tier 2): UMAC_CMD.TX_EN, TDMA ring + descriptor format,
  pbuf → BD copy, ring tail kick.
- RX path (Tier 3): UMAC_CMD.RX_EN, RDMA ring + pbuf allocation,
  pull-from-FIFO loop.
- Interrupt wiring (Tier 4+): INTRL2_0 mask programming, GIC SPI 189/190
  registration, DHCP + ICMP smoke test.
- TD-Eth-MAC: VideoCore mailbox lookup for the burned-in board MAC.

## Files touched in this tier

- `sources/phoenix-rtos-lwip/drivers/bcm-genet.c` — new driver
- `sources/phoenix-rtos-lwip/drivers/bcm-genet-regs.h` — new register map
- `sources/phoenix-rtos-lwip/drivers/ephy.c` + `ephy.h` — bcm54213pe model
- `sources/phoenix-rtos-lwip/drivers/netif-driver.c` — Ethernet name match
- `sources/phoenix-rtos-lwip/port/main.c` — register_driver_genet hook
- `sources/phoenix-rtos-lwip/_targets/Makefile.aarch64a72-generic` — driver list
- `sources/phoenix-rtos-build/build-core-aarch64a72-generic.sh` — build lwip
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/build.project` — LWIPOPTS_DIR + WiFi/IPsec off
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/lwip/lwipopts.h` — Pi 4 lwip config
- `sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/user.plo.yaml` — lwip syspage entry

Integration manifest: `manifests/2026-05-24-eth-tier1-link-up.md`.
