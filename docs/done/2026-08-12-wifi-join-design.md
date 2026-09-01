# BCM43455 WPA2-PSK JOIN design (radio-as-transport #4, Phase 2)

Ground-truth firmware sequence extracted from the Linux brcmfmac driver
(`external/linux/.../brcm80211/`, primary-source cited). The BCM43455 is
**fullmac with an in-dongle supplicant (FWSUP)** — the firmware runs the WPA2
4-way handshake itself. Host job: set security params, enable `sup_wpa`, hand
the firmware the passphrase, issue the join, watch events. **No host EAPOL /
PBKDF2 needed** (use the passphrase path; firmware derives the PMK).

Test AP (host, `scripts/radio-ap-up.sh`): SSID `PhoenixNet`, WPA2-PSK
`phoenixpi2026`, 2.4GHz ch6, gw 10.43.0.1/24 (DHCP 10.43.0.10-254).

## Transports (already in wifi-probe.c)
- `diag_iovar(...,is_set,name,data,dlen,...)` → `WLC_SET_VAR`/`GET_VAR` (`"name\0"+data`). On the primary STA iface a "bsscfg iovar" == plain iovar (no prefix).
- `diag_bcdcCmd(...,is_set,cmd,txdata,txlen,...)` → raw WLC ioctl by number.
- All wire fields little-endian.

## Recipe (WPA2-PSK / CCMP, broadcast join)
Prelude (mostly already done by diag_wifiScan): `event_msgs` (enable bits below) → clmLoad → `WLC_C_SET_INFRA`(20)=1 → `WLC_UP`(2)=1. Then:
1. `wsec` iovar (`__le32`) = **0x04** (AES_ENABLED, pairwise|group).
2. `wpa_auth` iovar (`__le32`) = **0x80** (WPA2_AUTH_PSK).
3. `sup_wpa` iovar (`__le32`) = **1** (enable firmware supplicant). MUST precede PMK.
4. WLC ioctl **`WLC_SET_WSEC_PMK`=268** with `struct brcmf_wsec_pmk_le` (132 bytes):
   `__le16 key_len; __le16 flags; u8 key[128];`
   Passphrase path: `flags = 0x0001` (BRCMF_WSEC_PASSPHRASE), `key_len = strlen(psk)` (8..63), `key[0..len-1] = ASCII passphrase`. (PMK path alt: key_len=32, flags=0, key=32B PMK.) MUST precede join.
5. WLC ioctl **`WLC_SET_SSID`=26** with 36-byte `struct brcmf_ssid_le { __le32 SSID_len; u8 SSID[32]; }` — a bare 36-byte SSID set is a valid broadcast join (the `join` iovar w/ ext_join_params is the alt for targeted/channel-locked join).

Ordering: security params (1-4) before join (5); sup_wpa (3) before PMK (4); PMK (4) before join (5).

## Events (success/failure) — read off SDPCM channel 1, same demux as escan
`event_msgs` bitmask (byte array, `mask[i/8] |= 1<<(i%8)`) must enable: 0,5,6,7,11,12,16,46 (+ keep 69 for scan). Event fields at the same offsets diag_wifiScan uses: h_proto==0x886C at ehdr+12, event_type = be32 @ ehdr+28, status = be32 @ ehdr+32; reason = be32 @ ehdr+36; flags = be16 @ ehdr+26 (brcmf_event_msg: version,flags,event_type,status,reason,... — verify flag offset on HW).
- Event numbers: `WLC_E_SET_SSID`=**0**, `WLC_E_ASSOC`=7, `WLC_E_LINK`=**16**, `WLC_E_PSK_SUP`=**46**, deauth/disassoc 5/6/11/12.
- Status: SUCCESS=0, FAIL=1, TIMEOUT=2, NO_NETWORKS=3, ABORT=4, **FWSUP_COMPLETED=6** (=classic WLC_SUP_KEYED).
- Flags: `BRCMF_EVENT_MSG_LINK`=0x01 (in WLC_E_LINK: set=up, clear=down).
- **CONNECTED** = `WLC_E_SET_SSID`(0)/status 0 AND `WLC_E_PSK_SUP`(46)/status 6.
- **FAIL** = SET_SSID status!=0, or LINK status 3 (no AP), or PSK_SUP status!=6 (wrong PSK; reason FWSUP_* 15/16/17 = handshake timeouts), or LINK flags&0x01 clear (down).

## Then (Phase 2b/3)
On CONNECTED: DHCP over the wifi netif (lwip) → IP on 10.43.0.x → ping 10.43.0.1. Then radio-as-transport: NFS/file over the wifi link (compare throughput vs 100Mbps ether).

## wifi-probe.c integration points
- Add `g_join_*` result globals near `g_scan_*` (~line 1403).
- Add `diag_wifiJoin(sdhci, sdio_core)` mirroring `diag_wifiScan` (~1502): broader event_msgs mask; iovars wsec/wpa_auth/sup_wpa; WSEC_PMK + SET_SSID via diag_bcdcCmd; event loop for type 0 & 46.
- Constants: `#define WLC_SET_SSID_CMD 26`, `#define WLC_SET_WSEC_PMK_CMD 268` (BRCMF_C_SET_INFRA + WLC_UP_CMD already exist).
- `diag_format_sdio_fwrelease`: call diag_wifiJoin when `g_join_mode` (after bring-up, like scan).
- `main`: parse `join` (+ optional argv SSID/PSK; default PhoenixNet/phoenixpi2026) → `g_join_mode=1`.
- Report line: join iovar rcs + SET_SSID rc + SET_SSID-event status + PSK_SUP status + CONNECTED verdict.
