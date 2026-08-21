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

## Cycle e7-air-detect (2026-08-21) — CONFIRMED non-egress with an L3-independent detector

The advisor flagged that the prior "0 on air" rested on a BPF DHCP filter that must parse L3,
while the DISCOVER has a *hand-built, hardcoded* IP checksum + length fields — a slightly-off
L3 frame would show 0 packets even if it egressed. Re-ran `jointx` with two host-side detectors
that do NOT depend on L3 validity: `iw dev wlp3s0 station dump` in a 2 s loop, and a broad L2
`tcpdump` filtered on the RPi OUI (`ether[6:4] & 0xffffff00 = 0xdca63200`). Result (108 samples,
~216 s associated window):

```
station dc:a6:32:3c:dd:f3  authorized: yes  connected-time 10..133s climbing
rx bytes: 774   (CONSTANT across all 108 samples — never +289 for the DISCOVER)
tx bytes: 583   (constant — AP sent no data back either)
L2 tcpdump (Pi-OUI source): 0 frames
```

**Conclusions (robust, not filter-dependent):**
- **Association is live + `authorized` at the AP** — verified directly for the first time (had
  only been derived from "4-way keyed"). Premise holds.
- `rx_bytes` is a **802.11-MAC-level** counter (L3-independent); it plateaus at 774 (assoc +
  EAPOL) and **never rises by the ~289-byte DISCOVER** ⇒ **the data frame genuinely never
  reaches the AP MAC — confirmed non-egress, not a capture artifact.**
- The fw's OWN internal TX works (EAPOL M2/M4 reached the AP) ⇒ **only the host-injected
  SDPCM-ch2 DATA → 802.11-TX handoff is dead.**

⇒ advisor's "world #2" (data doesn't egress, association live). Earned next step (dispatched to
a subagent): brcmfmac source comparison of (1) the exact data-frame byte construction and (2)
the post-association **data-path-enable** ioctl/iovar sequence brcmfmac issues between "keyed"
and "first data TX" that the probe's `diag_wifiJoin` omits.

## 🎯 ROOT CAUSE CONFIRMED (cycle linux-txbytes, 2026-08-21) — TXGLOM header format

Per the advisor's step-2 (capture the real on-SDIO data-frame bytes, don't infer), enabled
brcmfmac BYTES+DATA+SDIO debug (0x20088) so `brcmf_sdio_txpkt_prep` hex-dumps each TX **data**
frame. Linux's actual TX data frame (a ping, 0x7c=124 bytes):

```
[0-3]   7c 00 83 ff          HW hdr: len=124, ~len=0xff83
[4-11]  78 00 00 01 00 00 00 00   <- 8-byte HWEXT GLOM descriptor: (len-4=0x78)|(lastfrm=1<<24); tail_pad=0
[12-15] 1c 02 00 16          SW hdr: seq=0x1c, channel=0x02 (DATA), nextlen=0, doff=0x16=22
[16-19] 00 00 00 00          SW hdr word2 = 0
[20-21] 00 00                head_pad (2 bytes, addr-alignment)
[22+]   20 00 00 00          BDC (flags 0x20 ver2, prio 0, flags2 0, doff 0), then eth dst/src/0x0800
```

**This is the txglom on-wire format** (brcmf_sdio_hdpack sdio.c:1512-1518 + txpkt_prep :2246):
`HW(4) + HWEXT-glom-desc(8) + SW-hdr(8) + head_pad + BDC(4) + eth`, with `doff = tx_hdrlen +
head_pad = 20 + 2`. brcmfmac enables it when `sg_support` + `bus:rxglom` succeed (sdio.c:3772-3782
-> `bus->txglom = true; bus->tx_hdrlen += SDPCM_HWEXT_LEN`). **On the Pi4's SDIO host txglom IS
negotiated** (the capture proves it -- the HWEXT descriptor is present on every data frame).

**Phoenix's `diag_wifiDataTx` builds a bare NON-glom frame:** `HW(4) + SW-hdr@byte4(8) +
BDC@byte12(4) + eth`, doff=12, **no HWEXT descriptor**. Once the fw is in txglom mode it expects
the HWEXT + SW-hdr@12 layout, so it **misparses Phoenix's frame and drops it before 802.11** --
CMD53 F2 write succeeds ("reaches fw") but nothing egresses. Control frames survive because they
go through a separate path (`brcmf_sdio_tx_ctrlframe`, sdio.c:2412) that isn't glom-framed.
This is the confirmed root cause of the whole "TX reaches fw not air" wall.

### The fix (precise, for the next cycle)
Edit `tools/wifi-probe/wifi-probe.c`:
1. **Enable glom during bring-up** -- send `bus:rxglom` = (le32)1 as an iovar (mirror brcmfmac
   sdio.c:3775) so the fw is in the same mode Linux uses (verify rc=0).
2. **Reframe `diag_wifiDataTx`** to the txglom layout: HW hdr `[0-3]`; HWEXT `[4-7]=le32((total-4)|(1<<24))`,
   `[8-11]=le32(tail_pad<<16)` (tail_pad=0); SW hdr at `[12]`=seq, `[13]`=0x02, `[14]`=0 (nextlen),
   `[15]`=doff; `[16-19]`=0; then BDC at `doff` and eth after. Phoenix controls its own buffer so
   head_pad can be 0 => doff=20, BDC@20, eth@24. `total_len` (HW-hdr len) counts byte 0 .. end of eth.
3. Re-run `wifi-probe jointx`; confirm the AP `rx_bytes` jumps by the DISCOVER size (robust egress
   test) + tcpdump sees the DHCP DISCOVER on air. Baseline: artifacts/rpi4b-uart/*linux-txbytes.log.
