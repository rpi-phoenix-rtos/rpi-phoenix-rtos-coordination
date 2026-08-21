# E7 WiFi data-plane — Linux-reference SDPCM comparison (2026-08-21)

**Goal (owner E7, greenlit):** Phoenix WiFi control-plane works (associates + 4-way
keyed) but the data-plane is stuck — TX packets reach the firmware but never leave the
antenna. Owner premise: hardware is NOT broken; find the Phoenix software divergence by
comparing against a working Linux (brcmfmac) reference on the *same* Pi + *same* AP.

## Result: divergence localized, with evidence

### 1. Proof the stall is software, not hardware
Booted the Linux-Pi4 reference (RPi-OS, kernel 6.18.34, brcmfmac) over netboot NFS on the
**same physical Pi**, joined the **same host AP** (`PhoenixNet`, WPA2, 10.43.0.1) that
Phoenix TX-stalls on, and ran `ping 10.43.0.1`:

```
64 bytes from 10.43.0.1: icmp_seq=1..6 ttl=64 time=4-10 ms
6 packets transmitted, 6 received, 0% packet loss     (reproduced across 2 runs)
```

Linux's data-plane is flawless on identical hardware + AP ⇒ **Phoenix's TX-to-air failure
is a Phoenix software bug**, confirming the owner's premise with direct evidence.
Baseline UART captures: `artifacts/wifi-linux-ref/linux-brcmfmac-sdpcm-baseline.log`,
`.../linux-brcmfmac-credit-baseline.log` (brcmfmac debug mask 0x13065A:
SDIO+GLOM+INTR+HDRS+DATA+CTL+BCDC+FWCON+EVENT+TRACE).

### 2. Linux TX path (working model)
`brcmf_netdev_start_xmit → brcmf_fws_process_skb (fwsignal) → brcmf_sdiod_send_pkt`
(SDIO block write with an SDPCM software header carrying a per-packet sequence `tx_seq`
bounded by a firmware-granted window `tx_max`). Two host-side gates move a frame from the
WLAN FIFO onto the air:
1. **SDPCM bus-level credit window** — `brcmf_sdio_hdparse` harvests the SDPCM *window*
   byte off **every** received frame to update `bus->tx_max`; `brcmf_sdio_txpkt` refuses
   to send once `tx_seq == tx_max`.
2. **fwsignal (`brcmf_fws`) per-fifo credits + MAC-descriptor OPEN gating** via BDC/TLV.

### 3. The divergence (Phoenix `tools/wifi-probe/wifi-probe.c`)
Phoenix builds a byte-correct SDPCM software header and its single data frame's seqnum is
in-order (so the frame *is* accepted into the FIFO — matches "reaches fw"), but it omits
the entire host-side flow-control contract:

- **No credit-window feedback loop.** `diag_f2RecvFrame` stops at `*outchan = buf[5]&0x0f`
  (`wifi-probe.c:1248`) — it never reads `buf[8]` (SDPCM flow-control mask) or `buf[9]`
  (the SDPCM *window* byte = the `tx_max` the fw advertises on every frame). So Phoenix
  holds **no `tx_max` state** and never gates the data write (`wifi-probe.c:1814`) on
  `tx_seq` being inside the window. `tx_seq` is a pass-by-value byte set once at
  `wifi-probe.c:1805` — no per-frame increment, no window check.
- **No fwsignal (`brcmf_fws`) layer** and the join sequence never sends the `tlv` iovar
  (so fwsignal is never negotiated). The data frame at `wifi-probe.c:1799-1810` is a bare
  SDPCM[12]+BDC[4]+802.3 frame with no fwsignal TLVs and no credit accounting.

### 4. Refinement from the trace: fwsignal is likely NOT the blocker
The Linux RX trace shows `brcmf_fws_hdrpull enter: ifidx 0, skblen 98, sig 0` on the
working link — **`sig 0` means fwsignal signaling data is absent** on this 43455 fw path.
Since Linux transmits successfully with `sig 0`, the missing-fwsignal-TLV gap (divergence
bullet 2) is unlikely to be what kills Phoenix's frame. That sharpens the primary suspect
onto **the SDPCM bus-level `tx_seq`/`tx_max` credit window** (bullet 1) — the mechanism the
SDIO bus layer uses regardless of fwsignal.

## Fix target (bounded, testable)
`tools/wifi-probe/wifi-probe.c`:
1. In `diag_f2RecvFrame` (~`:1248`): harvest `buf[8]` (fc mask) and `buf[9]` (SDPCM window)
   into persistent `g_txMax` / `g_fcMask` state on every received frame.
2. In `diag_wifiDataTx` (`:1805`, `:1814`): make `tx_seq` a persistent counter that
   increments per data frame, and gate the CMD53 write on `tx_seq != g_txMax` (block/spin
   until the fw advances the window), mirroring `brcmf_sdio_txpkt`.

## Decisive next experiment
Implement the harvest + seq/window gate, boot the Phoenix WiFi probe (`join`+`jointx`), and
watch the **host AP side** (tcpdump on `wlp3s0`/the AP bridge) for the probe's
DHCP-DISCOVER to actually appear on the air. Host AP `PhoenixNet` is left up for this; the
Linux reference rootfs has a reusable `wifi-sdpcm.service` capture oneshot
(`/usr/local/bin/wifi-sdpcm-capture.sh`) for re-running the Linux baseline.

**Caveat (source-reading limit):** whether this specific 43455 fw image runs fwsignal at
all is not resolvable by reading source; the `sig 0` trace is strong evidence it does not
for data frames, but pinning fw fwsignal state (send the `tlv` iovar either way) remains a
possible tie-breaker if the window-gate fix alone doesn't free TX-to-air.
