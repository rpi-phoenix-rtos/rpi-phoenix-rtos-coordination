# kmsprobe — firmware planes, SMI vblank and scan-out range (new GPU lane E3 / E4 / E6)

Standalone Phoenix probe that drives the VideoCore firmware's plane API (`SET_PLANE`, the
interface Linux's `vc4-fkms` driver uses) with buffers we allocate, counts the firmware's SMI
vblank interrupt, and tries scan-out from different physical ranges. The HVS registers and
display-list memory are mapped **read-only** as a witness: after each `SET_PLANE` the probe looks
for the buffer address in the list the hardware is actually scanning, and uses the HVS frame
counters as a vblank reference that does not depend on the SMI interrupt.

Pre-registration, run plan and interpretation:
[`docs/gpu-new-lane/E3-firmware-planes-vblank.md`](../../../docs/gpu-new-lane/E3-firmware-planes-vblank.md).

All mailbox traffic goes through `/dev/vcmbox`. Every result line starts with `KMSPROBE `.

## Build

```
S=.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
V=sources/phoenix-rtos-devices/misc/rpi4-vcmbox
.toolchain/aarch64-phoenix/bin/aarch64-phoenix-gcc -O2 -Wall -Wextra -std=gnu11 \
    --sysroot=$S/ -B$S/lib/ -I$V \
    -o tools/gpu-lane/kmsprobe/kmsprobe tools/gpu-lane/kmsprobe/kmsprobe.c $V/libvcmbox.c
```

`libvcmbox.c` is compiled from the source tree (not the sysroot's `libvcmbox.a`), so the probe
always speaks the current `/dev/vcmbox` ABI. Nothing in the image references the binary.

## Stage (coordinator only)

Netboot lane: copy to the live `fsid=0` export root and run it as `/kmsprobe` (the export's
`/bin` is repopulated each cycle):

```
EXPORT=$(awk '!/^#/ && /fsid=0/{print $1; exit}' /etc/exports)
sudo cp tools/gpu-lane/kmsprobe/kmsprobe "$EXPORT/kmsprobe"
```

## Run (psh)

```
kmsprobe [-t auto|xl|raw60|v48] [-b 00|c0] [-d display_index] [-B] [-P bounce_pa] <mode> [args]
```

| mode | what it does | typical time |
|---|---|---|
| `info` | firmware/board revision, displays, `GET_DISPLAY_TIMING`, firmware fb geometry and layer, SMI + GIC state, HVS registers and display lists, where the fb pointer sits in the list | 2 s |
| `vblank [secs=10]` | 1 s polling SMI (no handler), `secs` of SMI IRQ (rate, jitter), 30 blocking firmware wait-for-vsync calls | secs + 2 s |
| `stack [hold=8]` | 5 visual steps: green plane 1 at layer 1; + blue plane 0 at layer −127; fb blanked; fb unblanked; planes unset | 5 × hold |
| `plane [secs=20]` | full-screen primary flipping between two buffers each vblank + a 128×128 magenta overlay moving 4 px per vblank; latch time, SET_PLANE latency, flips/s, missed vblanks | secs + ~5 s |
| `flipmax [secs=10]` | primary flips as fast as `SET_PLANE` allows, no vsync (E4) | secs + 2 s |
| `range lo\|hi\|any\|fb1\|<pa_min> [hold=10]` | full-screen checkerboard from a buffer in the chosen physical window, once per bus convention (E6); `fb1` = the firmware fb's own second buffer, a known-good control | 2 × hold |
| `contig [cap_mib=512]` | largest single `MAP_CONTIGUOUS` (256…16 MiB) and how many 8 MiB chunks land below 1 GiB / 1–4 GiB / above | ~5 s |
| `restore` | unset all 8 planes of the display, unblank the fb, re-assert it if missing | 1 s |

Options:

* `-b 00` (default): `planes[0]` = CPU physical address — what Linux fkms passes (its `vc4` node
  sits at the DT root without `dma-ranges`, CMA below 768 MiB). `-b c0`: `0xC0000000 | PA`, the
  `soc` bus alias (1 GiB window) the firmware itself uses for the framebuffer address.
* `-B`: blank the firmware fb and put the primary at layer −127, as Linux fkms does.
* `-t`: `SET_PLANE` transport. `SET_PLANE` is 60 bytes; the current `/dev/vcmbox` carries at most
  48. `auto` uses the large-buffer call when the server has it
  ([`vcmbox-xl.patch`](vcmbox-xl.patch), **not applied** — the recommended way to run E3).
  Otherwise it uses `raw60`: the first 12 words go through the existing message, and the firmware
  reads the last 3 (`planes[2]`, `planes[3]`, `transform`) from bounce-buffer words 17–19. The
  server never writes those words, but they are **not zero** (`MAP_CONTIGUOUS` memory is not
  cleared). So the probe first locates the bounce page (`-P`, or the server's boot banner in
  `/dev/kmsg`), verifies it by fingerprinting a transaction it has just made, and zeroes words
  18–20. Every result line then carries `tail=clean|zeroed|dirty|unknown`; with `unknown`, a
  failure is not interpretable. `v48` declares a 48-byte value buffer instead.
* `-P <pa>`: the `/dev/vcmbox` bounce-buffer PA from its boot banner
  (`rpi4-vcmbox: … bounce buf_pa=0x…`). It changes every boot.
* `-d`: display index (default 0).

Every mode restores the display on exit and on SIGINT/SIGTERM. After a crash, run
`kmsprobe restore`.

Buffers are `MAP_CONTIGUOUS | MAP_UNCACHED` (Normal-NC; the kernel cleans stale lines when it maps
a page uncached, `5d8645f6`), fully drawn before use, with `dsb sy` before each `SET_PLANE`.
Physical addresses are checked for contiguity and never truncated: a buffer outside the chosen
convention's window is refused, not wrapped.

## Files

* `kmsprobe.c` — the probe (BSD-3-Clause)
* `vcmbox-xl.patch` — proposed additive `/dev/vcmbox` extension for value buffers up to 1 KiB
  (`git -C sources/phoenix-rtos-devices apply --check` passes; compiled standalone with
  `-Wall -Wextra`). Existing callers are unchanged; an old server rejects the new call with
  `-EINVAL` without touching the FIFO.
