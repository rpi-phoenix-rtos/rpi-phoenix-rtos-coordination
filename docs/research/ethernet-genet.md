# Forward research: BCM2711 GENET v5 + BCM54213PE for Phoenix-RTOS

Scope: scout the work to bring up the Pi 4 built-in gigabit Ethernet under Phoenix-RTOS.
Primary references: upstream Linux driver, BCM2711 DT, YAML bindings, U-Boot port,
Circle bare-metal port. URLs cited inline.

## 1. Hardware overview

BCM2711 integrates a Broadcom GENET v5 MAC driving a BCM54213PE external gigabit PHY
over RGMII; PHY access is via a UniMAC-style MDIO bus mapped inside the GENET
register window. Peripheral base in low-peripheral mode is `0xFE000000`; the GENET
block lives at `0x7d580000` in the SoC's 35-bit view.

`bcm2711.dtsi` declares it as `compatible = "brcm,bcm2711-genet-v5", "brcm,genet-v5"`,
`reg = <0x7d580000 0x10000>`, with two GIC SPI lines (general/WOL plus ring TX/RX).
The MDIO child at offset `0xe14` (`reg = <0xe14 0x8>`) is
`compatible = "brcm,genet-mdio-v5"`; the PHY child has `phy-mode = "rgmii"` and
`reg = <0x1>` (MDIO addr 1). See `arch/arm/boot/dts/broadcom/bcm2711.dtsi` and
`bcm2711-rpi-4-b.dts` in raspberrypi/linux.

The BCM54213PE is a triple-speed (10/100/1000BASE-T), IEEE 802.3az copper transceiver
with RGMII. The full datasheet is NDA-only; practical register knowledge comes from
Linux PHY code (`drivers/net/phy/`) and the BCM54213 generic-PHY path in `bcmmii.c`.

Sources:
[bcm2711.dtsi (rpi-6.6.y)](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm/boot/dts/broadcom/bcm2711.dtsi),
[BCM2711 SoC overview](https://deepwiki.com/raspberrypi/linux/2.2-bcm2711-soc-(raspberry-pi-4)),
[Broadcom BCM54213PE](https://www.broadcom.com/products/ethernet-connectivity/phy-and-poe/copper/gigabit/bcm54213pe).

## 2. Linux driver structure

Path: `drivers/net/ethernet/broadcom/genet/`. Four files:

- `bcmgenet.h` - register map (`GENET_*`, `RDMA_*`, `TDMA_*`, `UMAC_*`, `EXT_*`),
  per-chip `bcmgenet_hw_params` table, ring/desc constants
  (`TOTAL_DESC = 256`, `WORDS_PER_BD`, `SKB_ALIGNMENT = 32`), the priv struct, and
  the inline accessors `bcmgenet_*_readl/writel`.
- `bcmgenet.c` - platform driver. Key entry points: `bcmgenet_probe` (DT parse,
  ioremap, IRQ request, MAC-address fetch, `register_netdev`),
  `bcmgenet_open`/`bcmgenet_close` (link bring-up, DMA enable),
  `bcmgenet_init_dma` -> `bcmgenet_init_{rx,tx}_queues` ->
  `bcmgenet_init_{rx,tx}_ring` -> `bcmgenet_alloc_rx_buffers`,
  the NAPI poll handler, `bcmgenet_xmit`, the `*_isr0/_isr1` ISRs, ethtool ops,
  and the suspend/resume path.
- `bcmmii.c` - everything related to the PHY: `bcmgenet_mii_init` (creates the
  mii_bus, scans, attaches PHY), `bcmgenet_mii_setup` (RGMII delay programming
  per `phy-mode`), `bcmgenet_mii_config`, link-state handler, and the v5
  MDIO read/write helpers around the `MDIO_CMD/MDIO_BUSY` registers.
- `bcmgenet_wol.c` - Wake-on-LAN/MagicPacket; not on the critical path for Phoenix.

The driver hooks into the kernel via `struct net_device_ops` (`ndo_open`,
`ndo_start_xmit`, `ndo_stop`, ...), `ethtool_ops`, and `phylib`. Sources:
[bcmgenet.c](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/genet/bcmgenet.c),
[bcmgenet.h](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/genet/bcmgenet.h),
[bcmmii.c](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/genet/bcmmii.c),
[bcmgenet_wol.c](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/genet/bcmgenet_wol.c).

## 3. DT bindings

`Documentation/devicetree/bindings/net/brcm,bcmgenet.yaml` - required `compatible`
takes one of `brcm,genet-v1..v5` plus the SoC-flavoured
`brcm,bcm2711-genet-v5`/`brcm,bcm7712-genet-v5`. Required: `reg` (one cell pair),
`interrupts`/`interrupts-extended` with two cells (general IRQ; ring TX/RX IRQ),
`#address-cells = <1>`, `#size-cells = <1>`, plus a single MDIO child described
by the unimac-mdio binding. Optional `phy-handle`, `phy-mode`, `mac-address` /
`local-mac-address`, and `clocks` (Pi 4 ties this to a fixed clock; the SoC has
no software-visible GENET clock-gate).

`Documentation/devicetree/bindings/net/brcm,unimac-mdio.yaml` - `compatible`
includes `brcm,genet-mdio-v5`; `reg` is one or two cells (the second is the
"indirect" window only used on platforms that need >16-bit MDIO XFERs - not
the Pi 4); `#address-cells = <1>`, `#size-cells = <0>`, plus child PHY nodes.

RPi-specific flags worth flagging: `phy-mode = "rgmii"` (no `-id`/`-rxid` variant
on stock Pi 4), the firmware-injected `local-mac-address`, and the absence of a
`clocks` reference in some Pi DT revisions.

Sources:
[brcm,bcmgenet.yaml](https://mjmwired.net/kernel/Documentation/devicetree/bindings/net/brcm,bcmgenet.yaml),
[brcm,unimac-mdio.yaml](https://www.kernel.org/doc/Documentation/devicetree/bindings/net/brcm,unimac-mdio.yaml).

## 4. Initialization sequence

Distilled from `bcmgenet_probe` and `bcmgenet_open` in upstream `bcmgenet.c`
and the simplified U-Boot port:

1. **Reset.** Assert `UMAC_CMD.SW_RESET` and `UMAC_CMD.LCL_LOOP_EN`, then clear
   - puts MAC and RX/TX FIFOs into known state (`reset_umac`).
2. **MAC address program.** Driver reads MAC from DT (`local-mac-address`); on
   Pi 4 the firmware writes that property into the DT before kernel start (see
   sec. 7). Driver writes it into `UMAC_MAC0/MAC1`.
3. **MDIO bring-up.** `bcmgenet_mii_init` registers the mii_bus backed by
   `bcmgenet_mii_read/write`, scans, locates the PHY at addr 1, attaches via
   `phy_connect_direct`, sets supported features.
4. **PHY config.** `bcmgenet_mii_config` programs RGMII delays via `EXT_RGMII_OOB_CTRL`
   (`RGMII_LINK`, `RGMII_MODE_EN`, optional `ID_MODE_DIS`); kicks PHY autoneg via
   `phy_start`.
5. **DMA ring alloc.** `bcmgenet_init_dma` zeroes the in-MMIO descriptor ring
   (`TOTAL_DESC = 256` BDs each direction), splits queues per
   `priv->hw_params->{rx,tx}_queues` and `{rx,tx}_bds_per_q`. For each ring
   `bcmgenet_init_rx_ring` programs `RDMA_*_{WRITE,READ,PROD,CONS}_INDEX`,
   `RDMA_RING_BUF_SIZE`, the start/end pointers, and calls
   `bcmgenet_alloc_rx_buffers` to map and fill skb buffers.
6. **DMA enable.** Set `RDMA_REG.CTRL.EN` and `TDMA_REG.CTRL.EN`; arm
   `INTRL2_CPU_*` mask registers.
7. **IRQ wire-up.** `request_irq` for the two GIC SPIs; ISR `bcmgenet_isr0`
   handles RX-default + TX-default + link/PHY, `bcmgenet_isr1` handles
   ring-priority queues.
8. **Link-up.** PHY autoneg completes, link handler updates `UMAC_CMD.SPEED`,
   `HD_EN`, and `RX_EN`/`TX_EN`; `netif_carrier_on` and the netdev becomes
   transmittable.

Sources:
[bcmgenet.c master](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/genet/bcmgenet.c),
[u-boot bcmgenet.c](https://github.com/u-boot/u-boot/blob/master/drivers/net/bcmgenet.c).

## 5. Memory model

GENET DMA is descriptor-based and operates on physical/bus addresses. Each BD is
`WORDS_PER_BD * sizeof(u32)` (typically 3 words on v5: address-lo, address-hi/length,
status). The ring is **inside the GENET MMIO window** (`TDMA_DESC_*` and
`RDMA_DESC_*` register banks), not in a CMA-allocated buffer - the driver writes
descriptors via MMIO `writel`. What you must allocate from system RAM is the
**packet payload buffers** themselves.

Constraints relevant to a Phoenix port:

- Buffer alignment 32 bytes (`SKB_ALIGNMENT`), driven by cache line + GENET burst.
- BCM2711 DMA on the GENET path uses 35-bit physical addresses (high word lives
  in the descriptor); needs to flush/invalidate D-cache around the buffer just
  like the existing TD-04 SDHCI work.
- `TOTAL_DESC = 256` total per direction is the hard ring cap; a Phoenix
  bring-up can collapse to a single default queue (16) and keep all 256 there,
  exactly like U-Boot does.
- No CMA on Phoenix is fine: a static page-aligned pool sized to
  `RX_RING_SIZE * MTU_ALIGNED + TX_RING_SIZE * MTU_ALIGNED` (e.g. 64 RX + 32 TX
  with 2KB slots ~ 192 KB) is enough for tier-2.

Source:
[bcmgenet.h master](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/genet/bcmgenet.h).

## 6. Phoenix-RTOS path to "working"

- **Tier 0 - link-up only.** ioremap GENET, run `reset_umac`, MDIO helpers
  around `MDIO_CMD/BUSY`, kick PHY autoneg, watch `UMAC_STATUS` /
  `EXT_RGMII_OOB_CTRL` for link. No DMA, no IRQ. Port of `bcmmii.c` MDIO
  helpers (~300 LoC). Deps: MMIO mapping + mailbox MAC fetch (sec. 7).
- **Tier 1 - TX one packet.** TX ring init, single default queue (#16),
  polled completion. Port `bcmgenet_init_tx_ring` and the `bcmgenet_xmit`
  body without locking/skb (~500 LoC). Validate via host `tcpdump` over a switch.
- **Tier 2 - RX one packet.** Mirror TX for RX, hand-rolled
  `bcmgenet_alloc_rx_buffers` over a static pool, polled drain on a timer.
  ~500 LoC. Adds the cache-flush dance.
- **Tier 3 - lwIP integration.** Move to GIC IRQ via Phoenix's IRQ plumbing,
  wire `pbuf` alloc/free to the descriptor recycler, register a `netif`.
  ARP -> DHCP -> ICMP -> TCP. ~800 LoC plus lwIP glue.

Circle `lib/bcm54213.cpp` is a useful second source: a single-author port at
~3 kLoC including PHY. Phoenix can be smaller by skipping WOL, RXCHK, ethtool,
and multi-queue priorities.

Sources:
[circle bcm54213.cpp](https://github.com/rsta2/circle/blob/master/lib/bcm54213.cpp),
[u-boot bcmgenet.c](https://github.com/u-boot/u-boot/blob/master/drivers/net/bcmgenet.c).

## 7. MAC address provisioning on Pi 4

The MAC is burned into SoC OTP (Pi 4/400 use OUI `dc:a6:32` or `e4:5f:01`, last three
bytes per-unit). The VideoCore firmware exposes it via the property mailbox tag
`0x00010003` (`GET_BOARD_MAC_ADDRESS`, request 0 bytes, response 6 bytes, network
byte order). On Linux boot the firmware patches `local-mac-address` in the DT
before kernel start; GENET reads it via `of_get_mac_address` in probe.

For Phoenix: reuse the existing mailbox property-interface driver (tag
`0x00010003`) rather than re-parsing the DT - avoids ordering between fdt and
netdev probe.

Sources:
[Changing MAC addresses (Raspberry Pi)](https://pip-assets.raspberrypi.com/categories/685-app-notes-guides-pcns-whitepapers/documents/RP-003474-WP/Changing-MAC-addresses),
[Mailbox property interface forum thread](https://forums.raspberrypi.com/viewtopic.php?t=218406).

## 8. Known quirks

- **v5 register deltas vs v4:** new MDIO command word layout (single
  `MDIO_CMD`), expanded `EXT_RGMII_OOB_CTRL` flags, INTRL2 split into
  `INTRL2_0`/`INTRL2_1`. `bcm2711-genet-v5` selects v5 in `bcmgenet_hw_params`.
- **No software clock gate on Pi 4.** GENET clock is always on; the Linux
  clock path is a no-op here, omit it in port.
- **PHY mode is plain "rgmii".** Pi 4 does NOT use `rgmii-id`/`-txid`/`-rxid`;
  delays are taken on the PCB. (raspberrypi/linux #3195.)
- **WOL.** Skip `bcmgenet_wol.c` entirely.
- **Reset is mandatory on warm boot.** Circle history shows skipping reset
  after firmware chain-load causes retransmission storms.
- **35-bit DMA.** The high address word in the descriptor must be programmed;
  mishandling silently aliases DMA into low RAM on 4 GB+ boards.

Sources:
[circle CHANGELOG](https://github.com/rsta2/circle/blob/master/CHANGELOG.md),
[raspberrypi/linux #3195](https://github.com/raspberrypi/linux/issues/3195).

## 9. Open questions for the orchestrator

1. Do we accept a polled tier-2 milestone before wiring the Pi 4 GIC? Phoenix
   IRQ plumbing on Pi 4 is unfinished and would otherwise gate Ethernet.
2. Tier-3 lwIP - bring in lwIP fresh, or piggy-back on the lwIP build that is
   already vendored for other Phoenix targets? Check `phoenix-rtos-lwip`.
3. Do we want a "U-Boot-style" minimal driver merged first (~1500 LoC, single
   queue, polled) and a richer port later, or skip the throwaway?
4. Cache management: reuse the TD-04 cache-coherency primitive (flush/invalidate
   by VA range) for descriptor and packet buffers, or add a GENET-specific
   variant? Consistency with the SDHCI work is preferred.
5. How do we want to expose the MAC to userland - via a Phoenix sysfs-equivalent
   or a `netif`-only API? Affects tier-3 design.
6. Test rig: do we have a managed switch on the bench so we can mirror the GENET
   port for `tcpdump` validation, or do we need a USB-Ethernet capture host?
