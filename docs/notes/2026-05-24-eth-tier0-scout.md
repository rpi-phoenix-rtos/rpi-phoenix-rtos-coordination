# Ethernet Tier 0 scout — Phoenix lwIP integration + Pi 4 GENET

Pre-implementation reconnaissance for the BCM2711 GENET v5 driver port.
Companion to:
- `docs/research/ethernet-genet.md` (Linux-side reference)
- `docs/research/ethernet-genet-non-linux.md` (BSD / U-Boot / Circle / EDK2)

## 1. Phoenix lwIP integration contract — confirmed

Source: `sources/phoenix-rtos-lwip/`

### 1.1 Registration

Drivers register via a constructor with priority `1000`:

```c
__constructor__(1000) void register_driver_genet(void)
{
    register_netif_driver(&genet_drv);
}
```

`netif_driver_t` (from `include/netif-driver.h`):

```c
static netif_driver_t genet_drv = {
    .init = genet_netifInit,    /* called by lwIP per cfg arg */
    .state_sz = sizeof(genet_state_t),
    .state_align = _Alignof(genet_state_t),
    .name = "genet",            /* matched against cfg-string head */
    .media = genet_media,       /* returns "1Gbps/full-duplex" etc. */
};
```

### 1.2 cfg-string parsing

`port/main.c:127-134` iterates argv from the lwip daemon's syspage line.
Each arg is `"<drvname>:<cfg...>"`. lwIP looks up the driver by `<drvname>`
then calls `drv->init(netif, cfg)` with the suffix.

iMX template (`imx-enet.c:1574-1622`): `"enet:<phys_addr>:<irq>[:no-mdio][:PHY:<phyopts>]"`.

**Proposed GENET cfg**: `"genet:0xFD580000:189:190:PHY:bcm54213pe"`.
Two IRQs (general + ring) because the DT exposes both.

### 1.3 RX / TX path

- **TX**: lwIP calls `netif->linkoutput(netif, pbuf)`. Driver copies pbuf
  payload into a TX buffer, fills the TX BD (PA, length, SOP/EOP, OWN),
  bumps `TDMA_PROD_INDEX`. lwIP guarantees only one in-flight pbuf per call.
- **RX**: driver thread calls `netif_input(pbuf, netif)`, which routes via
  `tcpip_input` (set in `netif_dev_init`, `netif-driver.c:157`).

### 1.4 PHY abstraction — reuse `ephy.c` (1006 LOC)

`ephy_init(&state->phy, cfg_opts, board_rev, link_cb, netif)` —
takes care of standard MII register dance. BCM54213PE responds to
standard PHY ID/STATUS/AUTONEG registers, so `ephy.c` is reusable as-is.

### 1.5 MDIO bus abstraction

```c
static const mdio_bus_ops_t genet_mdio_ops = {
    .read  = genet_mdioRead,
    .write = genet_mdioWrite,
    .setup = genet_mdioSetup,
};
register_mdio_bus(&genet_mdio_ops, state);
```

The bus index returned is consumed by `ephy_init` via the cfg string.

### 1.6 `bdring.c` reusability — **NOT for GENET**

`bdring.c` (321 LOC) is a generic BD ring helper, but it assumes
descriptors live in DRAM (driver allocates an aligned pool, hands the
PA to the controller).

**GENET puts BD rings INSIDE the MMIO register window** (per Circle
research §5; confirmed by Linux `bcmgenet.h` `RDMA_DESC_*` /
`TDMA_DESC_*` register banks). We write descriptors via plain
`writel` to the device, no cache management needed for the BD memory
itself — only for the **payload buffers**, which we allocate
ourselves.

So `bdring.c` is **not** in the dependency list. We do need a small
in-driver "where am I in the ring" helper (cons/prod index + buffer
table) — ~50 LOC, written from scratch.

### 1.7 Packet buffer pool — `pktmem.c` (300 LOC) is reusable

Generic page-aligned packet buffer allocator. We'll use it for
TX and RX payload buffers.

## 2. IRQ plumbing — userspace API confirmed

`imx-enet.c:1326`: `interrupt(irq, enet_irqHandler, state, state->irq_cond, &state->irq_handle);`

Phoenix's `interrupt()` is the standard userspace IRQ-binding syscall.
The handler runs in IRQ context and signals the cond; an IRQ thread
(`enet_irqThread`, line 533) does the actual work.

For GENET this becomes two `interrupt()` calls for the two SPIs.
**Open question**: does Phoenix's aarch64 GIC code accept arbitrary SPI
numbers from userspace via this syscall? Confirmed yes for PPI 27
(architectural timer) — that path is exercised. SPI 189/190 should
follow the same path; Tier 1 will confirm in passing.

## 3. Pi 4 DT — GENET node parsed

```
ethernet@7d580000 {
    compatible = "brcm,bcm2711-genet-v5";
    reg = <0x00 0x7d580000 0x00 0x10000>;
    interrupts = <0x00 0x9d 0x04 0x00 0x9e 0x04>;
    phy-mode = "rgmii-rxid";      /* NB: not plain "rgmii" */
    status = "okay";
    phy-handle = <0x40>;
    mdio@e14 {
        compatible = "brcm,genet-mdio-v5";
        reg = <0xe14 0x08>;
        ethernet-phy@1 {
            reg = <0x01>;          /* MDIO addr 1 */
        };
    };
};
```

### 3.1 Address translation: SoC → ARM low-peri

Phoenix uses **low-peri mode** (confirmed against existing peripheral
bases in `board_config.h`):

| SoC view    | ARM-side    | What                |
|-------------|-------------|---------------------|
| 0x7E201000  | 0xFE201000  | PL011 UART          |
| 0x7E00B880  | 0xFE00B880  | Mailbox             |
| 0x7D500000  | 0xFD500000  | PCIe host bridge    |
| **0x7D580000** | **0xFD580000** | **GENET**         |

**`GENET_PHYS_BASE = 0xFD580000`**, size 64 KB.

### 3.2 IRQ numbers

GIC 3-cell encoding `<type, number, flags>`:
- `<0 0x9d 0x04>` → SPI 157, level-high → **absolute IRQ 189** (32 + 157)
- `<0 0x9e 0x04>` → SPI 158, level-high → **absolute IRQ 190** (32 + 158)

Linux assigns ISR0 to the "general/RX/TX-default" line and ISR1 to the
"ring priority" line — for our single-default-queue (ring 16) bring-up
only ISR0 (IRQ 189) is needed; ISR1 (190) goes unused until Tier 5.

### 3.3 PHY mode correction — `rgmii-rxid`, not `rgmii`

The research doc §3 says "no `-id`/`-rxid` variant on stock Pi 4" — that
is **wrong** for the actual Pi 4-B revision shipped DTB. We DO have
`phy-mode = "rgmii-rxid"`, which means:

- Internal RX delay enabled
- External TX delay (board PCB-trace based)

Implication for our `EXT_RGMII_OOB_CTRL` programming: bit
`ID_MODE_DIS` must be **cleared** so the GENET adds the RX-side
internal delay. Linux's `bcmgenet_mii_setup` in `bcmmii.c` already
handles this — we'll mirror it.

## 4. MAC-address source — `UMAC_MAC0/MAC1`

VideoCore firmware pre-programs the MAC into the GENET registers
before kernel handoff (confirmed by every non-Linux port surveyed:
U-Boot, Circle, EDK2, FreeBSD, NetBSD). Driver reads back:

```c
mac_lo = readl(GENET + UMAC_MAC0);  /* offset 0x80c */
mac_hi = readl(GENET + UMAC_MAC1);  /* offset 0x810 */
```

After `reset_umac()` these may need re-programming — driver should
read them BEFORE reset and write them back after. Same pattern as
Circle.

No mailbox call needed for the common case. Fallback (rare netboot
path where firmware didn't pre-program) is mailbox tag `0x00010003`
(`GET_BOARD_MAC_ADDRESS`) — Phoenix already has mailbox-property
infra for the PCIe `NOTIFY_XHCI_RESET` path, so the fallback is a
~30 LOC addition if ever needed.

## 5. Cache management — TD-04-derived primitives expected

GENET DMA targets system memory for payload buffers. AArch64 + Phoenix
MAP_UNCACHED behavior (used by USB drivers via `usb_allocAligned`) is
the simplest model: allocate UNCACHED packet buffers, no cache flush
needed per-packet. Costs throughput but simplifies bring-up.

Tier 1-3 will use MAP_UNCACHED throughout. If Tier 4/5 benchmarks
warrant it, we can switch to cacheable buffers + manual flush/invalidate
(`dc cvac` clean for TX, `dc ivac` invalidate before RX read) using
the TD-04 primitives that already exist for the USB outbound work.

## 6. 35-bit DMA addressing

GENET descriptor has `addr_lo` (32 bits) + `addr_hi` (high bits of a
35-bit physical address). Pi 4 / BCM2711 DRAM tops out at 4 GiB, so
high bits of the PA fit in the descriptor's high word.

**Critical**: must write `addr_hi = (pa >> 32) & 0x7` even though
on Pi 4 (4 GiB max) that's always 0. Mishandling silently aliases
DMA into low RAM on >4 GiB boards. Always-zero on our hardware but
correct-by-construction.

## 7. lwIP target Makefile — needs creating

No `sources/phoenix-rtos-lwip/_targets/Makefile.aarch64a72-generic-rpi4b`
exists yet. Template after `Makefile.armv7a7-imx6ull`:

```make
NET_DRIVERS_SUPPORTED := genet tuntap
NET_DRIVERS ?= $(NET_DRIVERS_SUPPORTED)

DRIVERS_SRCS_genet = bcm-genet.c ephy.c pktmem.c $(DRIVERS_SRCS_UTIL) hw-debug.c
DRIVERS_OBJS_genet = $(addprefix $(PREFIX_O), $(DRIVERS_SRCS_genet:.c=.o))
```

Plus a tweak to `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/`
to launch `lwip` with the `genet:0xFD580000:189:190:PHY:bcm54213pe`
arg in syspage.

## 8. Tier-1 implementation plan (~300 LOC, link-up only)

Files to add:
- `sources/phoenix-rtos-lwip/drivers/bcm-genet.h` — register defs
  (UMAC_*, RDMA_*, TDMA_*, EXT_*, INTRL2_*, MDIO_CMD), structs
- `sources/phoenix-rtos-lwip/drivers/bcm-genet.c` — driver skeleton

Tier-1-scope content:
- `genet_netifInit(netif, cfg)` — parse cfg, ioremap, register MDIO bus
- `genet_reset_umac()` — assert UMAC_CMD.SW_RESET | LCL_LOOP_EN, clear
- `genet_readMac()` — UMAC_MAC0/MAC1 → state->mac, write back post-reset
- `genet_mdioRead/Write/Setup` — UMAC_MDIO_CMD poll loop
- `genet_phyReset()` — strobe EXT_GPHY_RESET via EXT_RGMII_OOB_CTRL
- `genet_configRgmii()` — set RGMII_LINK, RGMII_MODE_EN, ID_MODE_DIS=0
- `ephy_init` invocation
- `genet_media()` — return formatted speed/duplex string
- Stub linkoutput (returns ERR_IF) so lwIP can register the netif

Deliverable: kernel log after build+boot+lwip-daemon-launch:
```
genet: BCM54213PE @ MDIO addr 1, link up 1000 Mbps full-duplex
genet: MAC dc:a6:32:xx:xx:xx
```

Bringing up at-most-one packet (Tier 2) requires the TX descriptor
ring + cache management; deliberately deferred.

## 9. Open questions for Tier 1 (will resolve during impl)

1. Does Phoenix's `interrupt()` syscall accept SPI 189/190 without
   any prior GIC distributor enable from the kernel side? (PPI for
   timer is already enabled in `_hal_interruptsInitPerCPU`; we may
   need a kernel-side GIC enableSPI call too.)
2. Does the syspage spawn loop launch `lwip` with the right argv? Need
   to check `phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/`
   for the lwip program manifest.
3. Pi-4-specific UMAC offsets vs upstream Linux: Linux `bcmgenet.h`
   uses dynamic offsets via `priv->hw_params`; we hardcode v5 layout.

## 10. Risks and known sharp edges

- **Plain `EXT_RGMII_OOB_CTRL` programming**: the Linux `bcmmii.c`
  `bcmgenet_mii_setup` has SoC-specific delay tuning. We must mirror
  the v5/BCM2711 branch literally.
- **GENET clock**: not gated in software on Pi 4 (always on). No
  clock-control needed (would be a no-op).
- **Reset is mandatory on warm boot** (Circle history): firmware may
  leave GENET in a half-initialized state. Always do `reset_umac` first.
- **lwIP TX flow control**: Phoenix lwIP daemon expects backpressure
  via `linkoutput` return value. Standard for all drivers.

## Next: write Tier 1 skeleton

Start with `bcm-genet.h` register definitions (~150 lines), then
`bcm-genet.c` skeleton with `genet_netifInit` + reset + MDIO + PHY
config + link-status polling thread. Skip everything beyond that.
