# Implementation plan: BCM2711 GENET v5 Ethernet for Phoenix-RTOS

Companion to the research briefs in `docs/knowledge/ethernet-genet.md` and
`docs/knowledge/ethernet-genet-non-linux.md`. This plan turns the surveyed
material into concrete files, ABI, and phased acceptance criteria sized to
the existing Phoenix conventions.

## 1. Goal and tier ladder

Bring the Pi 4B's built-in gigabit Ethernet (Broadcom GENET v5 MAC at
`0xfd580000` low-peripheral, BCM54213PE PHY over RGMII at MDIO addr 1) up
to the point where the lwIP stack carried by `phoenix-rtos-lwip` can run
DHCP, ICMP, and TCP against the netboot host.

Tiers, mirroring the research brief:

- **Tier 0 - link.** GENET MMIO mapped, `UMAC_CMD.SW_RESET` works, MDIO
  read of PHY ID at addr 1 returns the BCM54213PE OUI, RGMII OOB control
  is programmed, autoneg completes, link reports 1000FDX. No DMA, no IRQ.
- **Tier 1 - first TX.** A hand-built ARP request leaves the PHY (visible
  on the host's `tcpdump`/`dnsmasq` logs). Single ring, polled completion.
- **Tier 2 - first RX.** The reply that the host generates lands in the
  RX ring and is hexdumped by Phoenix. Polled drain.
- **Tier 3 - lwIP integration.** A `netif` registered in `phoenix-rtos-
  lwip`, GIC IRQ wired, DHCP lease obtained, ICMP echo answered, a TCP
  connection back to the host succeeds.

## 2. Phoenix conventions audit

The reference design is `phoenix-rtos-lwip/drivers/imx-enet.c` (iMX6ULL/
iMXRT GMAC) with FreeBSD-licensed netif glue. Key points:

- **Drivers live in `phoenix-rtos-lwip/drivers/`, not `phoenix-rtos-
  devices/`**. The latter has no Ethernet today. Pattern: register a
  `netif_driver_t` via `register_netif_driver()` through a
  `__constructor__(1000)` hook (`imx-enet.c:1713`). The `init(netif,
  cfg)` callback parses `enet:base:irq[:no-mdio][:PHY:...]` and is
  driven by `create_netif()` in `include/netif-driver.h`.
- **MMIO** uses `physmmap()` (`drivers/physmmap.c`) -
  `mmap(... MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS,
  -1, addr)`.
- **Descriptor / packet memory.** `dmammap(sz)` returns physically
  contiguous, uncached system RAM (`MAP_PRIVATE | MAP_ANONYMOUS |
  MAP_UNCACHED | MAP_CONTIGUOUS`). Shared helpers: `bdring.[ch]` for
  rings, `pktmem.[ch]` for a page-pooled pbuf allocator with a 16-entry
  free cache. Both are reused as-is.
- **IRQ.** Userspace: `interrupt(irq, handler, arg, cond, &handle)`
  from `<sys/interrupt.h>`. Kernel side lives in
  `phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.[ch]`. Pi 4 GICD
  `0xff841000`, GICC `0xff842000` are already in `aarch64a72-generic-
  rpi4b/board_config.h`. GENET SPIs are 157 and 158 per the BCM2711
  DTSI - hard-coded in cfg.
- **MDIO.** `register_mdio_bus(ops, arg)` plus `mdio_read/write`
  callbacks (`include/netif-driver.h`). Generic-PHY code in `ephy.c`
  covers Clause 22; we add a `bcm54213.c` for OOB reset and LED defaults
  but reuse `ephy.c` for autoneg.
- **MAC provisioning** in imx-enet reads OCOTP fuses; on Pi 4 the
  equivalent is firmware-programmed `UMAC_MAC0/MAC1` plus the property
  mailbox tag `0x00010003` (Section 7).
- **No new devctl ABI for tier 3.** Standard lwIP surfaces from
  `port/devs.c` and `port/sockets.c` cover everything.
- **Build.** Per-target Makefiles in `_targets/Makefile.<target>`
  declare `NET_DRIVERS_SUPPORTED` and per-driver source lists
  (`Makefile.armv7a7-imx6ull` is the model).

## 3. File-level breakdown

All paths absolute. Phoenix convention is to put the actual bring-up
code in `phoenix-rtos-lwip/drivers/`, register-only headers next to it,
and per-target Makefiles in `_targets/`.

```
/Users/witoldbolt/phoenix-rpi/sources/phoenix-rtos-lwip/
  drivers/
    bcmgenet.c               -- main driver (init, DMA, ISR, xmit, drain)
    bcmgenet-regs.h          -- offsets + bitfields, no logic
    bcmgenet-mdio.c          -- UMAC_MDIO_CMD wrappers, mii_bus ops
    bcm54213.c               -- PHY-specific reset + LED + EEE off
    bcm54213.h               -- PHY ID constants
  _targets/
    Makefile.aarch64a72-generic-rpi4b  -- new target
```

`drivers/Makefile` already foreach-expands `DRIVERS_SRCS_<name>` so the
new target file just declares
`NET_DRIVERS_SUPPORTED := bcmgenet tuntap` and
`DRIVERS_SRCS_bcmgenet = bcmgenet.c bcmgenet-mdio.c bcm54213.c
$(DRIVERS_SRCS_UTIL)`.

DTB / device-config integration: the Pi 4 currently boots without a
fully parsed FDT (the kernel uses a board_config-driven model). The
GENET base, two IRQ numbers, and PHY address are passed as the netif
config string from the project's user.plo.yaml. **No DTB parsing in the
first cut.** Once `dtb.c` in the kernel is exercised, GENET parsing can
move there and the cfg string becomes optional.

Build/manifest integration: after the first tier-1 success, snapshot
all three repos with `scripts/snapshot-integration-state.sh` and commit
the manifest as `manifests/2026-MM-DD-genet-tier1.md` per the rollback
discipline in `CLAUDE.md`.

## 4. Public ABI

- **netif config string** (passed through `create_netif()`):
  `bcmgenet:0xfe580000:157:PHY:bcm54213:0.1`
  - base: GENET MMIO physical address (low-peripheral mapping)
  - irq: GIC SPI for `intr0` (the link/RX-default IRQ); SPI for `intr1`
    is `irq+1` by GIC convention here, hard-coded for now
  - PHY model token + bus.addr (bus 0, MDIO addr 1)
- **/dev paths**: standard lwIP set - `/dev/route`, `/dev/ifstatus`,
  `/dev/ipstats`. The interface name follows lwIP convention; `eth0` if
  it is the first netif registered (see `phoenix-rtos-lwip/port/main.c`).
- **No new devctl messages** are introduced. Link state is read via the
  `/dev/ifstatus` text dump and BSD `SIOCGIFFLAGS`; MAC address is set
  by the driver and queryable via `SIOCGIFHWADDR`.
- **Interrupt indices**: BCM2711 DTSI declares
  `interrupts = <GIC_SPI 157 IRQ_TYPE_LEVEL_HIGH>,
                 <GIC_SPI 158 IRQ_TYPE_LEVEL_HIGH>`
  - SPIs land at GIC numbers 157+32 = 189 and 190 in absolute
  numbering. Phoenix's GICv2 driver expects the absolute number; we'll
  resolve the off-by-32 once the first IRQ probe lands and document in
  `bcmgenet-regs.h`.

## 5. Key functions and data structures

Mirroring the imx-enet.c topology. New names below; entry points start
with `bcmgenet_`, helpers with the relevant subsystem prefix.

```c
typedef struct bcmgenet_state {
    volatile uint8_t  *mmio;           /* physmmap'd 64 KiB */
    addr_t             dev_phys_addr;
    struct netif      *netif;
    uint8_t            mac[6];

    /* ring management (uses bdring.[ch] like imx-enet) */
    union { struct { net_bufdesc_ring_t rx, tx; }; net_bufdesc_ring_t rings[2]; };

    /* IRQ plumbing */
    handle_t irq_lock, tx_lock;
    handle_t irq_cond, irq0_handle, irq1_handle;
    atomic_uint drv_exit;
    uint32_t irq_stack[1024] __attribute__((aligned(16)));

    /* PHY */
    eth_phy_state_t phy;       /* reused from ephy.c */
} bcmgenet_state_t;

/* Top-level entry points */
static int  bcmgenet_init(struct netif *, char *cfg);          /* netif_driver_t.init */
static err_t bcmgenet_xmit(struct netif *, struct pbuf *);     /* netif->linkoutput */
static int  bcmgenet_irq0_handler(unsigned, void *);           /* RX-default + link  */
static int  bcmgenet_irq1_handler(unsigned, void *);           /* TX-default         */
static void bcmgenet_irq_thread(void *);                       /* drain + refill     */

/* Sub-system functions */
static int  bcmgenet_resetUmac(bcmgenet_state_t *);
static int  bcmgenet_initDma(bcmgenet_state_t *);              /* TDMA + RDMA setup  */
static void bcmgenet_enableDma(bcmgenet_state_t *);
static int  bcmgenet_readMac(bcmgenet_state_t *);              /* UMAC_MAC0/MAC1     */
static int  bcmgenet_setMac(bcmgenet_state_t *);
static int  bcmgenet_setRgmii(bcmgenet_state_t *);             /* EXT_RGMII_OOB_CTRL */
static void bcmgenet_setSpeed(bcmgenet_state_t *, int speed);  /* UMAC_CMD.SPEED     */

/* MDIO */
static int  bcmgenet_mdioRead(void *, unsigned addr, uint16_t reg);
static void bcmgenet_mdioWrite(void *, unsigned addr, uint16_t reg, uint16_t val);

/* PHY */
int  bcm54213_init(eth_phy_state_t *, ...);   /* OOB reset + EEE off + autoneg start */
```

**Descriptor layout.** GENET v5 stores BDs at fixed register offsets
inside the GENET MMIO window: `RDMA_DESC_BASE + i * 12` and
`TDMA_DESC_BASE + i * 12`, three 32-bit words per BD (length+status,
addr-lo, addr-hi). We do **not** allocate descriptor memory from system
RAM (Circle/U-Boot model). The `bdring` helper is therefore wrapped to
write through MMIO; the per-ring `phys` it currently exposes is
unused. We add a thin `bcmgenet_writeBd(idx, lo, hi, status)` and
override the imx-enet style `fillTxDesc/fillRxDesc` callbacks.

**Ring sizing.** `RX_RING_SIZE = 64`, `TX_RING_SIZE = 32`, ring 16
default queue only (Linux `DESC_INDEX`). All 256 hardware BDs are
assigned to the default queue per U-Boot's pattern.

## 6. DMA strategy without CMA

BCM2711 has no IOMMU and Phoenix has no CMA, so we follow Circle's split
exactly:

- **Descriptors are device-memory.** They live inside the GENET register
  window we already `physmmap` with `MAP_DEVICE | MAP_UNCACHED`. No
  cache maintenance, no allocation cost. This drops the `bdring.c`
  `dmammap` for descriptor storage; the `ring->phys` is set to the
  GENET base + `T/RDMA_DESC_BASE` so writes look identical.
- **Packet payload buffers** come from `dmammap()` via `pktmem.c` -
  exactly what imx-enet does. `dmammap` already returns
  `MAP_UNCACHED | MAP_CONTIGUOUS` memory, so no manual flush is needed
  in the steady state. The 35-bit physical-address upper word required
  by GENET v5 is filled from the high 3 bits of `va2pa(buf)` (Pi 4 RAM
  bank starts at `0x08000000` and ends below 4 GiB on the 4 GiB SKU, so
  the upper word is normally zero; we still program it correctly).
- **Cache coherency for early Pi 4 stages.** Until Stage-1 caches are
  on (TD-04 cache-coherency class), `MAP_UNCACHED` is sufficient. Once
  caches enable, the `pktmem` allocations stay uncached; only if a
  future optimization moves pbufs to cacheable memory do we need to add
  the same `clean+invalidate` sequence the SDHCI driver uses (TD-04
  hack-1/2/3). This plan does **not** require a new cache primitive at
  this stage.
- **Static reservation in syspage** is rejected: `pktmem.c` already
  allocates lazily and pools, so dynamic mmap suffices and matches
  every other Phoenix Ethernet driver.

Reference: `docs/knowledge/ethernet-genet-non-linux.md` Section 5
(Circle), Section 9 ("DMA without CMA"), and `docs/research/ethernet-
genet.md` Section 5.

## 7. MAC address provisioning

Order of attempts (matches FreeBSD/Circle lineage):

1. **Read `UMAC_MAC0`/`UMAC_MAC1`** at GENET offsets `0x80c`/`0x810`
   immediately after MMIO map and before `reset_umac` (the reset
   clears these registers - we save first, restore after).
2. **If zero or 0xff-padded**, fall through to the property mailbox.
   Phoenix has no general-purpose mailbox driver yet; we add a
   bare-metal helper next to `bcmgenet.c` that reuses the mailbox
   address from `RPI_MAILBOX_BASE_ADDRESS = 0xfe00b880` (already in
   `board_config.h`) and sends the standard tag `0x00010003`
   (`GET_BOARD_MAC_ADDRESS`, response 6 bytes).
3. **If both fail**, synthesize a locally-administered address from the
   board serial (mailbox tag `0x00010004`) plus 0x02 high-bit flag, the
   same fallback imx-enet uses.

We do **not** parse `local-mac-address` from the FDT in the first cut.
DTB parsing already lives in `phoenix-rtos-kernel/hal/aarch64/dtb.c`
but isn't wired through to userspace; opening that channel is out of
scope here. When the FDT property pipe is available, prepend it as
attempt 0.

## 8. Phased delivery

Each phase produces a unique UART signature (one line per check) so the
existing `summarize-rpi4b-uart-log.py` can grep for completion.

| Phase | Goal | Acceptance | UART signature |
| --- | --- | --- | --- |
| 0 | Baseline | Boot reaches `_hal_init` post-TD-04 (already true). `phoenix-rtos-lwip` builds with the new target Makefile but `bcmgenet` not yet wired. | (existing boot trail) |
| 1 | MMIO + register dump | `physmmap` GENET base, dump `SYS_REV_CTRL` and verify v5 (`0x06000005`); print `UMAC_MAC0/MAC1` raw bytes. | `bcmgenet: SYS_REV=0x06000005 MAC=xx:xx:xx:xx:xx:xx` |
| 2 | PHY init + link up | OOB reset + RGMII config + MDIO read PHY ID (expect `0x600d84a` Broadcom OUI). Spin on autoneg complete. | `bcmgenet: phy id=0x600d84a link=1000FDX` |
| 3 | First TX | Hand-built broadcast ARP for the host's IP via the polled TX path. dnsmasq on host logs the request. | `bcmgenet: tx ok len=42 status=0x.. (host saw ARP)` |
| 4 | First RX | Drain RX ring after host's ARP reply; hexdump first 64 bytes; verify ethertype `0x0806`. | `bcmgenet: rx ok len=60 ethtype=0x0806` |
| 5 | lwIP integration | `register_netif_driver`, link state callback, GIC IRQ live, DHCP lease, ICMP echo to host succeeds, a TCP connect back. | `lwip: bcmgenet@... eth0 up`, `dhcp: lease 10.0.0.x`, `icmp: echo replied` |

Each phase is a separate kernel-repo branch on top of the active
`agent/rpi4-program-reloc`. Phase 1-3 are squashable; Phase 5 is the
release point.

## 9. Test strategy

The netboot rig already has dnsmasq on the host (see `docs/netboot-test-
cycle.md`). Reuse the same wiring:

- **Wiring:** Pi 4 RJ-45 to the host's USB-Ethernet adapter through a
  managed switch (port-mirror to a third NIC for capture). dnsmasq
  serves DHCP, advertises a TFTP boot, and logs every ARP/DHCP packet.
- **Phase-3 check:** before lwIP, run `tcpdump -ni eth1 'arp or icmp'`
  on the host. The hand-built ARP from Phase 3 must show up; the
  host's reply round-trips for Phase 4.
- **Phase-4 verification:** Phoenix prints the first 60 bytes of the
  RX'd ARP reply; the macros in `summarize-rpi4b-uart-log.py` are
  extended with `bcmgenet:` prefixes.
- **Phase-5 e2e:** once `eth0` is up,
  `ping -c3 <pi-ip>` from the host must succeed,
  `nc <pi-ip> <port>` opens a TCP socket against a debug echo program
  in `phoenix-rtos-lwip/tests/`.

Smoke test cycle uses the existing
`./scripts/rebuild-rpi4b-fast.sh && ./scripts/capture-rpi4b-uart.sh`
plus a host-side `tcpdump` shell run in parallel.

## 10. Inter-dependencies

- **Stage-1 cache enable.** Not a hard prerequisite for correctness
  (`MAP_UNCACHED` works), but throughput on a fully cached kernel is
  several times higher. Order: TD-04 closure -> Stage-1 caches on ->
  bcmgenet retest. lwIP DHCP timeouts give some slack regardless.
- **GIC plumbing.** `phoenix-rtos-kernel/hal/aarch64/interrupts_gicv2.c`
  is in tree but not yet exercised on Pi 4. Phase 5 is the first
  consumer at SPI 157+158. Likely SPI->GIC number off-by-one to debug;
  budget half a session.
- **GPIO driver - confirmed not needed.** GENET sits on the SoC and uses
  dedicated pins routed on the PCB; the BCM54213PE is wired to GENET's
  RGMII block and to MDIO addr 1. No header-pin GPIO routing, no
  `pinctrl` action. Only the PHY's external GPHY-RESET strobe is
  required, and that lives inside `EXT_RGMII_OOB_CTRL.EXT_GPHY_RESET`
  in the GENET register window itself, not via a system GPIO.
- **lwIP server stack from libphoenix.** Phase 5 brings in
  `phoenix-rtos-lwip/port/main.c` which spawns the lwIP TCPIP thread.
  This is already used by other targets, so the dependency is just
  enabling it in the project's user.plo.yaml.
- **Firmware-programmed MAC ordering.** GENET MMIO must be read **before**
  the kernel relocates if we want to avoid re-mapping; in practice the
  read happens in user-space after PMAP is up, well after relocation.
  No special timing.

## 11. Effort and risks

- **Effort.** ~1500-1800 LoC of new C across the four driver files,
  matching the Circle-derived envelope from the non-Linux survey.
  Phase 1-2: ~1 session. Phase 3-4: ~1-2 sessions, mostly debugging.
  Phase 5: ~1-2 sessions for IRQ wiring + lwIP smoke + DHCP timing
  edge cases. Total: 4-6 sessions, spread over a week of bench time.
- **Risks.**
  - **GIC SPI numbering off-by-one.** Phoenix's GICv2 layer has not been
    driven on Pi 4 yet; expect one debug cycle to confirm SPI 157
    maps to absolute IRQ 189.
  - **35-bit DMA descriptor.** Easy to forget the `addr_hi` word on
    Pi 4 4 GiB+ SKUs. Mitigation: assert `va2pa(buf) < (1ULL << 32)`
    in Phase 3 and only allow upper bits in Phase 5.
  - **PHY autoneg latency.** BCM54213PE can take 3-4 seconds; spin
    loops in Phase 2 must be timed long enough.
  - **Reset on warm boot.** Circle's CHANGELOG flagged retransmission
    storms when `reset_umac` is skipped after firmware chain-load.
    Always reset.
  - **Cache-coherency after Stage-1 cache enable.** `MAP_UNCACHED`
    masks this today. If we later move payload buffers to cacheable,
    a TD-04-class flush/invalidate must be added.
  - **License hygiene.** Circle is GPLv3+; we use it as a behavioural
    reference only and code from scratch with a BSD-3 header citing
    the Linux GENET driver and the FreeBSD `if_genet.c` for register
    sequencing. No copy-paste.

## 12. Open questions

1. Polled tier-2 before GIC bring-up (saves ~1 session) or wait?
   Recommendation: polled Phase 3-4 keeps IRQ work off the critical path.
2. Mailbox property-interface helper: keep inside `bcmgenet.c` for now,
   factor to `drivers/rpi-mailbox.c` once a second consumer appears?
3. PHY reset path: BCM54213PE on Pi 4 is reset by GENET's own
   `EXT_GPHY_RESET`, not a board GPIO. Verify on Phase 2.
4. netif name: `eth0` (lwIP default) or `en1` (imx-enet style)? Choosing
   `eth0` for portability.
5. Multi-queue: skip entirely for the first release.
6. When DTB userspace lands, drop the cfg string? Out of scope here;
   track as a follow-up `TD-` item.
