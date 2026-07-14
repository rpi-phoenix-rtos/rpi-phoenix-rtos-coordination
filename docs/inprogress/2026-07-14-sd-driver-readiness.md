# SD card driver (bcm2711-emmc) — readiness verdict

**Date:** 2026-07-14
**Driver:** `sources/phoenix-rtos-devices/storage/bcm2711-emmc/` (HEAD `ef54b2d`, working tree clean)
**Scope of this note:** answer "is the SD card driver ready — full speed and correctness?" from the
committed code + HW logs, distinguishing what is validated from what is deferred. No new code logic;
only two stale-comment/doc fixes (see end).

## Verdict

- **Reads — done.** UHS-I DDR50 (1.8V) + SDMA + multi-block CMD18. HW-validated ~38 MB/s on netboot
  (~86% of Linux on the same card), 0 corruption. This is the headline win of #62.
- **Writes — correct, PIO-bound.** Multi-block CMD25 over PIO, completed via the #154 CMD13-poll-to-TRAN.
  Correct (0 silent corruption, see evidence) but ~17 MB/s (DDR50) / ~13 MB/s (HS50) — CPU-copy bound.
- **Not "full speed" overall:** reads hit the DDR50 target; writes are PIO-limited. Say exactly that.

So: **correctness is met; read speed is met; write speed is PIO-bound by a deliberate, documented
controller-quirk workaround.** The remaining speed levers are attended HW work (below), not unattended.

## Why writes stay on PIO (not a bug — a diagnosed quirk)

DMA (SDMA) *writes* on this BCM2711 EMMC2 controller intermittently corrupt the **first block**
(`firstBadBlk=0`), and the corruption survived both a poll-idle completion and a `dsb` drain barrier.
Reads via SDMA are clean. So the driver uses `useDma = host->useDma && (dir == sdio_read)`
(`sdcard.c:1978`) — DMA reads, PIO writes. Documented in commit `1b78519`.

## Evidence the *production* write path is correctness-clean

The production write path is **multi-block PIO** (sdstorage_dev.c `writeCb` → `sdcard_transferBlocks`
full length → CMD25, `useDma` false for writes). The `SDDIAG-MB` diag exercises exactly this path and
byte-compares against a single-block oracle. On the **final 128k config** (postdating the finalize
commits `95267ee`/`23bc607`), across three independent boots and all block sizes:

| Log (2026-07-01) | Mode | WRITE SILENT-CORRUPT (nb=8/32/128/256) | write nb=256 | read nb=256 |
|---|---|---|---|---|
| `…-220751-netboot-phx-128k` | DDR50 (uhs=4) | 0/10 all sizes | 17.4 MB/s | 38.3 MB/s |
| `…-221348-sdboot-phx-sdboot-128k` | HS50 (uhs=0) | 0/10 all sizes | 13.2 MB/s | 21.4 MB/s |
| `…-213342-sdboot-phx-sdboot-ddr50` | DDR50 (uhs=4) | 0/10 (to nb=128) | — | 35.7 MB/s |

The only `SILENT-CORRUPT=2/10 firstBadBlk=0` observations are in the `…-sdma2`, `…-dmawrite2`,
`…-dma-rw` logs — the **DMA-write experiment** runs (useDma temporarily true for writes). That is the
quirk that led to keeping PIO writes; it is not the production path.

Caveat: each cell above is one 10-trial run; three independent boots × 4 sizes is strong but not a soak.
A rigorous sign-off would run the soak named below.

## DDR50 switch: netboot reliable, SD-boot best-effort

`sdcard.c` performs the CMD11 1.8V signal-voltage switch when `SDCARD_ENABLE_DDR50` is set. On netboot
the card comes up `uhs=4` (DDR50) reliably; on SD-boot it is best-effort — one boot negotiated DDR50
(`uhs=4`), another fell back to HS50 (`uhs=0`). The HS50 fallback is safe and correct; only the read
throughput differs (~21 vs ~38 MB/s). A full `RESET_ALL` to force the SD-boot switch was tried and did
not help (comment at `sdcard.c:1433`).

## Deferred — attended HW work (card must be IN the Pi = SD-boot)

1. **DMA writes.** Root-cause the first-block SDMA-write corruption (ADMA2 descriptor path is the likely
   proper fix vs. the single-buffer SDMA). Test: `SDCARD_DIAG_CLOCKSWEEP` build, `SDDIAG-MB` WRITE with
   `useDma` forced true, **50+ trials** at nb=128 and nb=256 on the good card; require SILENT-CORRUPT=0.
2. **Reliable DDR50 on SD-boot.** Make the 1.8V switch deterministic under SD-boot (today best-effort).
   Test: 10 cold SD-boots; require `HC2 uhsMode=4` every time (currently intermittent).
3. **Write-path soak (correctness sign-off).** `SDDIAG-MB` WRITE, 50+ trials at nb=128/256 on the good
   card, both DDR50 and HS50; require SILENT-CORRUPT=0. Strengthens the 3×10 evidence above.

## Doc/comment fixes made with this note (HW-free, no logic change)

- `README.md`: SD row was "PIO reads/writes" → now states DDR50+SDMA multi-block reads (~38 MB/s),
  PIO multi-block writes (~17 MB/s), 0 corruption.
- `docs/KNOWN-ISSUES.md`: SD-throughput row gained the write speed, the 0-corruption evidence, and the
  netboot-reliable / SD-boot-best-effort DDR50 nuance.
- `sdcard.c:675`: stale "SDCARD_MAX_TRANSFER (64K)" comment → "(128K)".
