# Demo runbook — Phoenix-RTOS on the Raspberry Pi 4 (live event, 2026-10-15)

The short version of [USER-GUIDE.md §9](USER-GUIDE.md#9-the-showcase--the-best-setup-in-one-sitting) for a
live audience, with only what was verified on the Pi on 2026-10-13 (showcase gate 7/7, 0 faults;
demo candidate 1, manifest [`2026-10-13-demo-candidate-1`](../manifests/2026-10-13-demo-candidate-1.md)).

## 1. Set up (at the venue, before the audience)

The lab setup ([LAB-SETUP.md](LAB-SETUP.md)): the laptop netboots the Pi (DHCP + TFTP + NFS root) over its **built-in Ethernet**
through a switch, gets internet from the **iPhone over USB-C**, and runs the WiFi soft-AP **PhoenixNet** that the Meross
plug (Pi power) joins to reach its cloud. Both NATs follow whatever uplink holds the default route.

```
./scripts/netboot-server-up.sh       # once after the host boots: sync the NFS root, NAT, DHCP/TFTP
./scripts/demo-preflight.sh          # every line must say OK (boot image, dnsmasq, NFS, uplink, NAT, DNS, HTTPS, UART)
./scripts/live-test-rpi4b.sh         # powers the Pi on, live UART here + the HDMI window; close the window = power off
```

- Plug the **HDMI grabber into a USB3 (blue) port**: the window is then uncompressed 1080p60. The script's
  first lines say `preview: yuyv422 1920x1080@60`; `mjpeg …@30` means a USB2 port (it works, softer picture).
- USB keyboard + mouse on the Pi **before** power-on. The Pi's HDMI port: the one nearest the USB-C jack.
- The Pi needs no WiFi for the demo (boot-time WiFi is off on purpose).
- Boot to `(psh)%` takes ~50 s.

**Warm-up (do this before the audience arrives):** `netboot-server-up.sh` clears the GPU shader cache, so
the first start of each game compiles its shaders. Start each one once:

```
export GAMEDRM_EXIT_SECS=20
vkquake +playdemo demo1        # ~75 s black on the first start (Vulkan pipelines); ~3 s afterwards
quake3 +map q3dm1
stk race
```

## 2. The show (each step verified on the Pi on 10-13)

| # | At the prompt / on the desktop | What the audience sees |
|---|---|---|
| 1 | Boot | the kernel log on HDMI, `(psh)%` |
| 2 | `uname -a`, `ps`, `top` (q to quit) | a real multi-process OS; 4 CPU cores |
| 3 | `/bin/bash /bin/xfce-session` | XFCE 4.20 on Wayland (labwc, GPU-composited) in ~20 s: panel, memory monitor, Thunar |
| 4 | panel: terminal button (or Super+Return) | foot terminal; `mc` runs in colour |
| 5 | panel: **browser button** → `en.wikipedia.org`, a second tab (Ctrl+T) | WebKitGTK 2.54 with tabs, HTTPS, GPU rendering |
| 6 | **Applications → Games → Quake III Arena** | Quake III in a window next to the others, 80–87 fps |
| 7 | **Applications → Multimedia → Video Demo** | the 720p video in a window (GTK player, 30 fps) — fixed 10-14 |
| 7a | **Applications → Multimedia → Video in the Browser** | HEVC video **in the WebKit browser**, decoded on the Pi's hardware block with zero copy, 30 fps, looping (verified 10-14). Close the window when done |
| 7b | **Applications → Office → Atril** | a PDF |
| 8 | panel: **Log Out** (the rightmost button; also Applications → System → Log Out, or **Ctrl+Alt+Delete**) | back to `(psh)%`, cleanly |
| 9 | `quake3 +map q3dm1` | full screen 1080p, 60 fps (vsync) |
| 10 | `vkquake +playdemo demo1` | Quake on **Vulkan** on the V3D, ~42 fps (after the warm-up) |
| 11 | `game-res stk 1280x720 race` | SuperTuxKart, an AI race, ends by itself |
| 12 | `/bin/bash /bin/video-play /usr/share/video-demo/hevc-720p30-aac.mp4` | **HEVC decoded on the Pi's hardware block**, full screen, real time |
| 13 | `/bin/bash /bin/startx` | X11 (Xorg + glamor), Window Maker, GL window, xclock, xbill; the root menu (right click) has the X11 programs; **Session → Exit Session** returns to psh in ~4 s. The mouse pointer flickers while it moves (known, cosmetic) |

Quit games with Esc → Quit (Quake III: Esc → Exit), or set `export GAMEDRM_EXIT_SECS=90` first so each
ends by itself 90 s after its first frame.

## 3. Avoid — and what to do instead

| Avoid | Why | Instead |
|---|---|---|
| `video-play` **in a terminal on the desktop**, video in the browser | ffplay in a window often closes at its first frame (C18); browser video is unreliable | the **Video Demo** / **Video Player** menu entries (GTK player, verified 10-14), or `video-play` **full screen from psh** (step 12) |
| **Vimeo**, YouTube, very heavy video sites in the browser | memory: the desktop used to freeze when it ran out (C17) | Wikipedia, GitHub, news sites. Watch the panel's `Mem` bar; if it turns red, close the browser. a page above ~770 MB is now terminated by the browser (its tab says so; the browser keeps running), and `lowmemd` ends the largest program below 200 MB free. On 10-13 a Vimeo video page took the whole desktop down before these limits existed |
| vkQuake cold in front of people | ~75 s of black on its first start | the warm-up above |
| SuperTuxKart in a **window** | slow to load (~1 min) and ~18 fps | `game-res stk 1280x720 race` full screen (~22 fps) |
| The WPE browser (Applications → Internet → Web Browser) | one page per window, no tabs | the panel's browser button (WebKitGTK) |
| WiFi on the Pi | not needed; 80 MHz channels do not carry data (F1) | the wired netboot link gives the Pi internet |

## 4. If something goes wrong

**Fallback without the host:** the Pi's SD card holds the same build (release #4, verified 10-14). With the
netboot server down (`./scripts/netboot-server-down.sh`, or no host at all) the Pi boots from the card in ~50 s;
everything in §2 works the same, except web pages need a network with DHCP on the Pi's Ethernet port.


**The UART (serial log)** is optional for the demo — HDMI shows everything. On 10-13 the USB-serial
adapter dropped off the laptop's USB mid-session; `live-test-rpi4b.sh` now reconnects by itself and
marks the gap in the log (`UART reader stopped … reconnecting`). Plug the adapter straight into the
laptop (no hub) and check its three wires on the Pi's GPIO header.


| Symptom | Do this |
|---|---|
| The Pi does not boot / no `(psh)%` in ~90 s | close the live-test window (powers off), run `./scripts/demo-preflight.sh`, fix the FAIL line, start `live-test-rpi4b.sh` again |
| `demo-preflight` says `nfs` FAIL, or the boot stops at `nfs` / `ERANGE` | `sudo systemctl restart nfs-server`, then power-cycle |
| Web pages fail, the rest works | `demo-preflight.sh`: the `uplink` / `nat` / `dns` lines. Re-plug the phone or Ethernet; `./scripts/pi-internet-nat.sh` |
| "Certificate refused" / clock not set in the browser | at psh: `ntpclient -w 30`, then reload |
| A game window or the browser stops responding | close it from the panel's window list (right click → Close); the desktop keeps running |
| The desktop itself stops responding | Log Out if it reacts; otherwise close the live-test window (power off) and boot again (~50 s) |
| A full-screen game does not return to psh | Esc → Quit; next time `export GAMEDRM_EXIT_SECS=90` first |
