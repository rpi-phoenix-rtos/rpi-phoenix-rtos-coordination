# Pi4 (BCM2711) AXI performance-monitor driver for Phoenix — owner LKML task

**Owner ask (autonomous-plan.md line 19):** "Look at https://lore.kernel.org/lkml/20260811083828.2057695-1-irogers@google.com/
and see if we could implement something similar in Phoenix-RTOS." The thread (fetched via **NNTP** —
nntp.lore.kernel.org, since the HTTP views are Anubis/JS-gated) is **"[PATCH v1 0/2] perf: Add Raspberry Pi AXI PMU
driver"** (Ian Rogers, Google): a Linux `drivers/perf/` uncore PMU exposing the Broadcom **AXI system bus** +
VideoCore VPU performance counters (bytes/bandwidth) via standard `perf`. Validated on Pi 400 (BCM2711 = Pi 4).

**"Similar for Phoenix" = a userspace driver that reads the BCM2711 System AXI bandwidth monitors** → real hardware
bus/memory-bandwidth measurement. Directly serves the project's perf work (NFS ~8 MB/s, genet RX, V3D fill-bound — all
currently inferred, never measured at the bus). Fits the established Pi4 userspace-peripheral-driver pattern
(rpi4-thermal/hwrng/gpio: mmap the peripheral uncached, read/write registers).

## Register map (System AXI monitor) — from Linux DT + vendor driver raspberrypi_axi_monitor.c

- **Base: `0xfe009800`** (BCM2711 phys; DT `bcm270x.dtsi` axiperf reg[0] = legacy `0x7e009800`, size 0x100; the
  2nd reg `0x7ee08000`→`0xfee08000` is the VPU-side block, driven via VideoCore mailbox — defer). SYSTEM_MONITOR=0.
- `GEN_CTRL` = 0x00; `GEN_CTL_ENABLE_BIT` = BIT(0).
- 3 **bandwidth watchers**: `BW0_CTRL`=0x40, `BW1_CTRL`=0x80, `BW2_CTRL`=0xc0 (`BW_PITCH`=0x40).
- Per-watcher result offsets (relative to `BWn_CTRL`): ATRANS +0x04, ATWAIT +0x08, AMAX +0x0c, WTRANS +0x10,
  WTWAIT +0x14, WMAX +0x18, RTRANS +0x1c, RTWAIT +0x20, RMAX +0x24. (A=address, W=write, R=read; TRANS=transaction
  count, TWAIT=wait cycles, MAX=max outstanding.)
- `BW_CTRL` bits: RESET=BIT(31), ENABLE=BIT(30), ENABLE_ID_FILTER=BIT(29), LIMIT_HALT=BIT(28); SOURCE field <<8
  (GENMASK(12,8), 5 bits); BUS_WATCH field = GENMASK(5,0) (6 bits, selects which of up to 16 buses); BUS_FILTER<<8.
- Counters are **31-bit**: `readl(addr) & 0x7FFFFFFF`.

## Programming sequence (from the vendor driver)
1. Configure watcher n: `writel(BW_CTRL_RESET | BW_CTRL_ENABLE | (bus_watch & 0x3f) [| source<<8], base + BWn_CTRL)`.
2. Enable the monitor: `writel(GEN_CTL_ENABLE_BIT, base + GEN_CTRL)`.
3. Run workload; read `readl(base + BWn_CTRL + {W,R,A}TRANS_OFFSET) & 0x7fffffff` = transaction counts.
   Bytes ≈ transactions × burst-size (AXI burst width; the Linux driver applies a `Bytes` unit scaling — extract the
   exact factor when implementing; for a first cut, report raw R/W/A transaction counts).

## Plan (NEXT — the build)
1. Userspace tool `tools/axi-pmu/` (or a driver): mmap `0xfe009800` uncached (like rpi4-thermal's 0xfe00b880 pattern),
   program BW0 to watch the DRAM/L2 bus (need the BUS_WATCH enum — extract from raspberrypi_axi_monitor.c's bus list),
   enable, sleep a known interval, read R/W/A transaction counters, print bytes/s.
2. Verify autonomously: run during a known workload (e.g., a big memcpy / an NFS read / a V3D render) and confirm the
   counters increment sensibly + track the workload; compare read vs write bias. Owner "compare with Linux": the
   numbers should be in the ballpark of the known figures (NFS ~8 MB/s etc.).
3. Later (deferred): the VPU monitor via VideoCore mailbox (2nd block); a /dev/axiperf device; wire to the F2 perf work.

Saved: the Linux patch driver body at $CLAUDE_JOB_DIR/tmp/rpi_axi_pmu_patch1.txt; vendor driver at
external/linux/drivers/perf/raspberrypi_axi_monitor.c.
