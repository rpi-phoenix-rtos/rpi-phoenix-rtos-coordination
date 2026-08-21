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

## Experiment EXECUTED (2026-08-21 cycle e7-wifi-credit) — credit/seq REFUTED

Per the advisor's "instrument read-only + tcpdump, don't blind-code" plan, I added read-only
instrumentation to `wifi-probe.c` (no gating): `diag_f2RecvFrame` now records the
fw-advertised SDPCM flow-mask (`buf[8]`) and credit window / max-seq (`buf[9]`) on every RX
frame, and `diag_wifiDataTx` records the `tx_seq` it writes. Ran `wifi-probe jointx
PhoenixNet phoenixpi2026` on HW (join → CONNECTED, WPA2 4-way keyed) with `tcpdump` on the
host AP `wlp3s0` in parallel. Result:

```
wifi: SDPCM-CREDIT tx_seq=21 rx_win_last=62 rx_win_min=21 rx_win_max=62 fc=0x00 rx_seq_last=26 rx_frames=27
tcpdump wlp3s0 (udp 67/68 + arp), whole cycle: 0 packets
```

**Interpretation (advisor's third branch):**
- `tx_seq=21` is far **inside** the fw-granted window (max-seq advertised up to **62**), and
  `fc=0x00` ⇒ the fw is **not** asserting flow-control. ⇒ **The SDPCM seq/credit window is
  NOT the blocker — hypothesis REFUTED.** The harvest+gate fix would have been wasted; the
  read-only cycle correctly pre-empted it.
- tcpdump saw **0 frames on air** ⇒ this is genuinely a **TX-to-air** failure (the frame dies
  inside the fw), **not** an RX-of-OFFER problem. The frame is handed to the fw over SDIO
  (F2-write rc=0, seq in-window, flow clear) yet never reaches the PHY.

**⇒ New prime suspect: fwsignal (proptxstatus) TX header.** With credit refuted and the BDC
data header (flags=0x20 ver2, priority 0, flags2 0, doff 0) matching brcmfmac's *fwsignal-off*
form, the remaining divergence is that this 43455 fw likely runs fwsignal (proptxstatus) and
silently discards a data frame lacking a valid fwsignal descriptor. **Next experiment (pin
first, per don't-blind-code):** re-capture the Linux baseline WITHOUT the early `dmesg -c` so
the brcmfmac init logs the `tlv`/proptxstatus mode it negotiates for this fw; if fwsignal is
on, add the `tlv` iovar + a minimal fwsignal TX header to the probe and re-test tcpdump. If
fwsignal is off there too, pivot to BDC priority/AC mapping or an interface-not-tx-ready iovar.

## Cycle linux-fwsignal (2026-08-21) — fwsignal signaling is NOT the differentiator

Re-captured the Linux baseline with the fwsignal-negotiation dump (no early `dmesg -c`).
brcmfmac loaded `brcmfmac43455-sdio` fw **BCM4345/6 wl0 version 7.45.265 (28bca26 CY)**, ping
0% loss again. Evidence on fwsignal: **no `fws_stats` debugfs** captured (before or after
traffic; on its own inconclusive — debugfs may be unmounted), **but** combined with the prior
capture's `brcmf_fws_hdrpull … sig 0` on *every* RX frame (siglen=0 ⇒ no fwsignal TLVs in the
BDC), the fw is **not signaling fwsignal on this link**. So Linux is effectively sending bare
BDC+eth data frames — structurally the same as Phoenix. ⇒ **fwsignal TLVs are unlikely to be
the missing ingredient.**

**Sharper problem statement:** Phoenix's control plane fully works (ioctls, scan, join,
WPA2 4-way keyed) — and the FULLMAC firmware-supplicant generates + transmits the EAPOL M2/M4
frames itself, which the AP must receive to complete the 4-way. **So the fw's *internal* TX
path to air demonstrably works.** What fails is specifically the **host-injected SDPCM
channel-2 DATA frame → 802.11 TX** handoff: the frame is accepted over SDIO (F2 write rc=0,
seq in-window, flow clear, no fwsignal needed) yet never reaches air. Remaining candidates
(post-credit, post-fwsignal): (a) BDC interface-index / bsscfg mapping (flags2 ifidx) or
priority/AC; (b) a post-association iovar / data-path-enable the fw needs before it will
egress host data (802.1X controlled-port authorize, or an interface "up for data" step
brcmfmac does that the probe omits); (c) frame framing the fw's data classifier rejects.
