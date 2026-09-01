# WiFi data-plane design (radio-as-transport #4, Phase 2b/3)

Make the associated BCM43455 WiFi carry IP traffic (DHCP → ping → transport).
Control plane DONE (scan + WPA2 join CONNECTED, [[project_wifi_fw_exec_gate_91]]).
This is the DATA plane. Primary-source-cited from Linux brcmfmac + Phoenix lwip.
**The data plane rides the SAME SDIO F2 transport as control** — SDPCM channel 2.
wifi-probe.c:1362 ALREADY receives channel-2 frames and discards them ("data
channel -- ignore") = the F2 path + demux are proven; missing = header wrap/unwrap
+ block-mode + credits + RX thread + lwip binding.

## Byte layouts (all little-endian; offsets from F2 payload byte 0)
SDPCM header (12B, same for control+data): [0-1]=len, [2-3]=~len, [4]=seq,
**[5]=channel (0x02=DATA)**, [6]=nextlen(0 tx), [7]=data_offset(**12**),
[8]=flowctl(rx), **[9]=tx_max/window (READ on rx for credits)**, [10-11]=0.
After SDPCM, channel differs:
- Control (existing): 16B BCDC dcmd {cmd4,len4,flags4,status4}.
- **Data (new): 4B BDC header** {[0]flags=**0x20** (ver2<<4), [1]priority=0,
  [2]flags2=0 (if idx0), [3]data_offset=**0** (fws words; 0 when off)}, then the
  802.3 frame.

**TX wrap** (802.3 frame → F2 write): [0..11] SDPCM ch=2 seq=tx_seq doff=12 |
[12..15] BDC 0x20 00 00 00 | [16..] eth frame. total=12+4+eth_len. Block-mode
CMD53 write to F2 (addr 0x8000, window 0x18000000 — same as control). Pad to
block boundary.

**RX unwrap** (F2 read → 802.3): len=le16(buf[0]),chk=le16(buf[2]); ~(len^chk)==0.
channel=buf[5]&0x0f; if !=2 → existing control(0)/event(1) handlers. sdpcm_doff=
buf[7] (>=12,<=len). bdc=buf+doff; eth=bdc+4+(bdc[3]<<2); eth_len=len-doff-4-
(bdc[3]<<2). Hand eth[0..eth_len) to the stack.

## Transport gaps (the real work — beyond channel demux)
1. **Block-mode, not byte-mode.** Existing F2_FRAME_MAX=512 (byte-mode helpers)
   truncates a 1500B MTU frame → corruption. Use the EXISTING block-mode helpers
   diag_sdioCmd53Read(wifi-probe.c:432)/Write(:700), F2 blocksize 64, ~2048B buf.
   (Byte-mode stays fine for small control ioctls.)
2. **Credit flow control.** tx_max delivered in RX SDPCM word2 byte[9]; tx_seq++
   per data frame; do NOT TX when tx_seq==tx_max (fw grants credits via RX). Ignoring
   → wedge/drop under load.
3. **RX drain thread.** SDIO has no completion IRQ callback (unlike usbwlan's URB);
   poll F2 (gate on intstatus I_HMB_FRAME_IND, already read wifi-probe.c:1329).
4. **Bus mutex + per-context buffers.** g_txf/g_rxf are process-global + single-
   threaded; TX (lwip) + RX-drain + control ioctls all share one SDIO bus → need a
   bus mutex or one serialized bus-service thread with queues. Largest structural change.
5. **Two devices:** keep /dev/wifi (text scan/join); add /dev/wlan0 (binary frames).

## Architecture: **B — standalone driver + device-backed lwip netif** (netboot-safe)
Keep rpi4-wifi a SEPARATE process (SDIO/join/SDPCM-data); expose /dev/wlan0
(usbwlan-style frame device); add a THIN lwip client netif that is INERT unless a
boot arg (`bcmwifi:/dev/wlan0`) is passed (port/main.c:140-147 argv). Rationale:
the risky 643KB-fw-download+join+data code stays OUT of the netboot-critical lwip
server; a wifi crash kills only rpi4-wifi, genet netboot untouched. Matches the
owner's net/usbwlan reference. (Arch A = in-process bcm-wifi.c netif in lwip = puts
risky code in the sole recovery channel → rejected.)
- Finding: NO existing lwip client consumes a driver-served frame device. tuntap.c
  is the INVERSE (lwip = server). usbwlan = server, no lwip consumer yet.
- **B1 (fast interim, 0 new lwip code):** reuse tuntap `tap` (lwip serves /dev/ta0);
  rpi4-wifi opens /dev/ta0 as CLIENT: read=frames-to-TX, write=inject-RX. Caveat:
  tap_init doesn't set netif->hwaddr → must stamp the chip MAC (dc:a6:32:3c:dd:f5).
- **B2 (clean end, recommended):** rpi4-wifi serves /dev/wlan0 (mtWrite=TX,
  mtRead=RX-held-pending, mtGetAttr=poll, mtDevCtl=GET_MAC/GET_LINK); thin
  drivers/bcm-wifi.c client netif in lwip (~150 lines mirroring bcm-genet.c:
  RX-thread read→pbuf_alloc→netif->input; linkoutput=write; MAC via devctl; register
  via __constructor__+register_netif_driver). Bootstrap: /dev/wlan0 appears only after
  ~2min bring-up → netif open must retry/defer.

## Ordered plan (each step independently observable; DHCP alone does NOT prove the
## transport — a single 342B frame passes even with the buggy 512B/seq=0 path)
1. TX one hardcoded DHCP-discover (byte-mode ok) after join; **verify: tcpdump on host
   10.43.0.1 sees the discover.** (Proves wrap + ch2 TX addr.)
2. RX poll channel-2; log the DHCP offer src/dst/len. (Proves demux + unwrap.)
3. **Upgrade to block-mode + credits;** verify with `ping -s 1400` (>512B) + ping flood
   (credit flow) — the REAL gate, before any lwip.
4. RX drain thread + bus mutex + per-context buffers; 60s concurrent TX+RX+scan, 0 corrupt.
5. B1: expose via tap /dev/ta0 + stamp MAC → dhcp + ping the gateway.
6. B2: /dev/wlan0 + thin bcm-wifi.c (inert unless boot arg, deferred open) → full DHCP→
   ping→TCP; genet netboot still healthy; forced rpi4-wifi crash leaves netboot up.

## Key file:line
brcmfmac sdio.c:1345-1528 (SDPCM), bcdc.c:47-76,256-331 (BDC). wifi-probe.c:432/700
(block helpers), :548/624 (byte), :1362 (ch2 ignored today). rpi4-wifi.c:1214 (F2 addr),
:2654-2689 (join), :2781 (msg loop), :2924 (create_dev), :2905 (sleep120 bootstrap).
lwip: netif-driver.h:31-42, netif-driver.c:45-49, main.c:140-147, bcm-genet.c:861-909/1110/
1447-1460, tuntap.c:84-102/223/248-257. usbwlan.c:344-366/474-503/506-529/579-615.
