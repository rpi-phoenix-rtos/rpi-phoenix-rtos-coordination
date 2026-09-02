# Revisiting the `PI_FW_REF` firmware pin (2026-09-02)

Research-only pass. No Pi cycle was run; no build input was changed.

Owner's hypothesis: *"maybe the Pi firmware repo shipped a broken build and later
shipped a fix — if we bump to current firmware the triple-buffering bug may be gone,
so we could track current firmware. And maybe a newer firmware needs an EEPROM
reflash — I can do that."*

---

## Recommendation (read this first)

1. **The pin's stated reason is not supported by our own evidence.** The comment in
   `scripts/bootstrap-linux-host.sh:150-159` says newer firmware "DENIES the over-tall
   virtual-height request: v3d-winsys reports virt_h=0 -> 1 buffer". But **the pinned
   firmware produced `virt_h=0` too** — `artifacts/rpi4b-uart/rpi4b-uart-20260623-061317-netboot-revertfionread.log`
   carries the firmware banner `ae9a8e…` **and** `virt_h=0 -> 1 buffer(s) single` — and
   that reading stopped occurring, on that same firmware, once the VideoCore mailbox was
   serialized (mailbox race → `/dev/vcmbox`, coord `3cc684c`, 2026-07-16). Since that
   fix: **323 UART logs, all `virt_h=3240`, zero `virt_h=0`.** So `virt_h=0` was never a
   firmware-version discriminator; it was the symptom of a bug we have since fixed
   properly.
2. **On the owner's "broken build, later fixed" theory: upstream shows no such thing.**
   `raspberrypi/firmware` has **no release notes at all** (`boot/release_notes.txt` does
   not exist), and of the six commits that touched `boot/start4.elf` after our pin,
   **none mentions framebuffer, virtual height/offset, dispmanx, or buffering** (§3). So
   there is no announced break and no announced fix to wait for. Equally, there is **no
   hardware evidence that newer firmware works**: we have never booted any firmware
   other than the pinned one (1669 of 1669 logs carrying a firmware banner show the same
   `ae9a8e` build). The bump still needs one test — but nothing in the record justifies
   *fearing* it.
3. **Do the 1-cycle test in §5, then bump the pin to a current sha.** It costs one
   netboot cycle, needs no rebuild, and rolls back with one command.
4. **Keep pinning as policy; do NOT track `master`.** The reproducibility argument for a
   pin (`docs/done/2026-07-04-clean-build-reproducibility.md`) stands on its own and is
   independent of the flicker story: a moving branch makes every clean/Docker build ship
   a different `start4.elf`. Bump the pin deliberately, re-verify, record it.
5. **The EEPROM is a red herring for this change.** A `start4.elf` bump does **not**
   require an EEPROM reflash (§4). The one thing the owner may eventually want to reflash
   for is unrelated: our lab netboot config, already done once (`BOOT_ORDER=0xf12`,
   2026-05-21).

---

## 1. The pin, and every place that records why

**The pin itself** — `scripts/bootstrap-linux-host.sh:148-168`:

```
148  # Raspberry Pi firmware blobs we need from raspberrypi/firmware boot tree.
149  #
150  # PIN TO A COMMIT SHA, never a moving branch. The firmware (start4.elf) decides
151  # whether it grants plo's request for a 3x-tall virtual framebuffer — the backing
152  # for the GLQuake triple-buffer page-flip present path. Firmware newer than the
153  # pin below (e.g. raspberrypi/firmware master as of 2026-07) DENIES the over-tall
154  # virtual-height request: v3d-winsys reports virt_h=0 -> 1 buffer, the renderer
155  # falls back to single-buffer render-in-place, and Quake tears/flickers on heavy
156  # frames. It also makes every clean build non-deterministic. This SHA is the last
157  # firmware verified to grant virt_h=3240 (TRIPLE-BUFFER). Bump deliberately, and
158  # re-verify the scanout-init line still reports TRIPLE-BUFFER after any bump.
159  #   41f4808 "kernel: Bump to 6.18.32" (2026-05-18) -> start4.elf VC_BUILD_ID ae9a8e
160  PI_FW_REF="${PI_FW_REF:-41f4808270a922f08fdd927edfeb60212800fe64}"
```

Pin verified against the on-disk checkout:

```
$ git -C .bootblobs/.firmware-checkout log -1
41f4808270a922f08fdd927edfeb60212800fe64  2026-05-18 20:17:55 +0100  kernel: Bump to 6.18.32
$ strings -a .bootblobs/start4.elf | grep VC_BUILD_ID
VC_BUILD_ID_VERSION: ae9a8ea9f3ca745de6f357bd7fc8307721ad38b7 (clean)
VC_BUILD_ID_TIME:    May  8 2026 / 18:13:05
VC_BUILD_ID_BRANCH:  bcm2711_2
```

So: repo commit **2026-05-18**, but the `start4.elf` blob inside it was **built
2026-05-08** (`ae9a8e`). Blobs lag the repo commit; the pin commit's own subject
("kernel: Bump to 6.18.32") is a Linux-kernel bump, unrelated to graphics.

**Other places that record the rationale:**

- coord commit `2a11a9f43` (2026-07-04) — the pin itself:
  *"bootstrap: pin RPi firmware to a SHA (fixes GLQuake flicker + non-reproducible clean
  builds)"*.
- `docs/done/2026-07-04-clean-build-reproducibility.md:99-141` — the origin story
  (full quote in §2).
- `docs/review/2026-07-06-pre-publication/PENDING-USER-TASKS.md:25-27`:
  > "**Firmware pin bump policy.** `PI_FW_REF` is pinned to `41f4808` (firmware that
  > grants the 3x virtual framebuffer). Any future bump must re-verify the winsys
  > scanout-init line still reports TRIPLE-BUFFER."
- `docs/knowledge/rpi4-os-development-guide.md:802-806` — generalised into
  project-wide advice:
  > "### Firmware governs buffering — pin it
  > The RPi firmware (`start4.elf`) decides framebuffer double/triple buffering. A newer
  > `start4.elf` can silently deny the 3× virtual framebuffer the loader requested →
  > revert to single-buffered → tearing. **Pin the firmware SHA** in your build; treat
  > unexplained tearing after a firmware bump as this."
  (also `:825-831`, same claim in the display section)
- `manifests/release-pin.md:57-58` — **stale and now wrong**:
  > "**raspberrypi/firmware** — boot blobs + DTB; staged by `stage_pi_firmware`
  > (currently tracks `master`; pin via `PI_FW_REF` for a fully reproducible boot)."

That is the whole record. There is no other note, and no manifest that pins the
firmware sha alongside the sibling shas.

---

## 2. What was actually observed — and how solid it is

### The claim as written (2026-07-04)

`docs/done/2026-07-04-clean-build-reproducibility.md:99-126`:

> **Symptom:** clean-build (VM) SD boots fine but Quake flickers (HUD/enemies tear,
> worse on heavy frames — monsters, explosions). X11 unaffected. […]
> - Host `.bootblobs` staged 2026-05-20 → firmware `VC_BUILD_ID ae9a8e` (from
>   raspberrypi/firmware @ `41f4808`, 2026-05-18) → grants 3x → `virt_h=3240`.
> - Clean VM build 2026-07-04 → `master` had advanced → firmware `VC_BUILD_ID f68405`
>   → DENIES 3x → `virt_h=0`.
>
> ```
> smooth  (60 logs, 06-21..07-02):  scanout init ... virt_h=3240 -> 3 buffer(s) TRIPLE-BUFFER+page-flip
> flicker (07-04 VM build):         scanout init ... virt_h=0    -> 1 buffer(s) single (blit-resolve)
> ```
> `virt_h=0` → `nbuf=1` → single-buffer render-in-place → tearing. Every other boot
> artifact (plo source+provenance, config.txt, dtb, armstub, rpi4-fb allocation) was
> byte-identical; `start4.elf`/`fixup4.dat` were the only difference.

That reads convincingly. It does not hold up.

### Why it does not hold up

**(a) The isolating A/B was never confirmed on hardware.** The same doc, line 134,
labels the confirmation card "**awaiting user boot**". Two days later,
`PENDING-USER-TASKS.md:8-14` still has it open, and says of the first attempt:

> "**Boot is untested** (the earlier fwfix card failed only because a loop-mount `cp`
> corrupted the FAT — `Read start4.elf failed / bad cluster id: 0`; this image was
> built with `mcopy` and fsck's clean, so it should boot)."

No UART log in `artifacts/` records a result for either card. So the "swap only
`start4.elf`+`fixup4.dat` → smooth" experiment that the conclusion rests on was
**designed but not executed**.

**(b) `virt_h=0` is not a firmware answer — it is a *failed query*.**
`tools/v3d-driver-port/v3d_phoenix_power.c:271-307` returns `0` only when
`GET_VIRTUAL_WH` **fails** (via `/dev/vcmbox`, then after 8 direct-FIFO retries):

```
304  printf("v3d-winsys: GET_VIRTUAL_WH failed via /dev/vcmbox AND 8 direct retries -> single-buffer "
305         "fallback (render-to-scanout into live fb; expect tearing).\n");
306  return 0u;
```

A firmware that genuinely refused a 3×-tall surface reports the height it *did* grant,
and `v3d_phoenix_scanout_init()` (`v3d_phoenix_winsys.c:387-398`) prints that number —
we have seen exactly that happen (the `2160`/`2560` rows below, from the pre-`gpu_mem=128`
era). Across all 454 `scanout init` lines in `artifacts/rpi4b-uart/`:

| reading | count | meaning |
| --- | --- | --- |
| `virt_h=3240` → 3 buffers | 428 | 3× granted |
| `virt_h=0` → 1 buffer | 16 | **query failed** (not a firmware answer) |
| `virt_h=2560` → 2 buffers | 8 | all 2026-06-21, before `max_framebuffer_height=4096` |
| `virt_h=2160` → 2 buffers | 2 | all 2026-06-21 (`…-netboot-dbuf-1/2`) — a real partial grant |

**No log ever shows `virt_h=1080`** (a full refusal), and no log shows `virt_h=0`
together with a real grant.

*Qualification (from §3):* the property-interface spec says a `SET_VIRTUAL_WH` response
"may be the previous width/height **or 0 for unsupported**", so `0` is also a legal
firmware reply. `virt_h=0` is therefore **ambiguous**, not proof of a lost response — it
simply cannot tell the two apart, which is exactly why it was the wrong evidence to pin
on. What settles 2026-07-04 is (c) and (e) below, not this value's semantics.

Note also that the string
`GET_VIRTUAL_WH failed via /dev/vcmbox` appears **0 times** in the archive — that
diagnostic was added by the same 2026-07-16 commit that fixed the race, and no boot
since has taken the failure path.

**(c) The pinned firmware also produced `virt_h=0`.** The decisive artifact —
`artifacts/rpi4b-uart/rpi4b-uart-20260623-061317-netboot-revertfionread.log`, a netboot
from **2026-06-23**, i.e. before any firmware bump existed:

```
 41.33 Firmware: ae9a8ea9f3ca745de6f357bd7fc8307721ad38b7 May  8 2026 18:13:05
 41.69 Starting start4.elf @ 0xfec00200 partition -1
…
v3d-winsys: scanout init pa=0x3d3b2000 1920x1080 pitch=7680 virt_h=0 -> 1 buffer(s) single (blit-resolve)
```

That is the **pinned** `ae9a8e` firmware giving the exact symptom the pin exists to
prevent. Firmware version cannot be the discriminator.

**(d) The 07-04 flicker boots never recorded which firmware they ran.** The three
07-04 SD logs (`20260704-133024`, `-134912`, `-143111`, all `virt_h=0`) contain **no**
firmware banner — `config.txt` has `uart_2ndstage=0`, so the firmware-load stage is
silent. The `f68405` identification came from the VM's staged blob, not from the boots
that flickered. And of the 1669 logs that *do* carry a banner, **every single one** is
`ae9a8e` — one distinct value repo-wide.

**(e) The symptom was independently root-caused to something else, and that fix
worked.** `docs/done/2026-07-15-v3d-mesa-gl-code-review.md:127-141`:

> "**The dynamic-model flicker is single-buffer render-to-scanout TILE TEARING, gated by
> a non-deterministic per-boot framebuffer-buffering mode.** […] Across archived UART
> `scanout init` lines the grant is **non-deterministic per boot of the same build** […]
> **Same dsbfix build, two boots, opposite result:** netboot `20260715-003609` came up
> `virt_h=3240` TRIPLE-buffer […] The user's SD boot `20260715-074348` came up
> `virt_h=0` SINGLE-buffer → flicker. That is the whole mystery: the flicker tracks the
> boot's buffering mode, not the code."

Then `v3d_phoenix_power.c:273-282` (commit `3cc684c`, 2026-07-16) names the mechanism:

> "The BCM2711 has a single VideoCore property-mailbox FIFO with NO hardware
> arbitration: if two processes drive it concurrently, one read loop pops and discards
> the other's response. The winsys was driving the FIFO DIRECTLY […] a concurrent reader
> drained our GET_VIRTUAL_WH response -> spurious failure -> single-buffer -> dynamic-model
> flicker (netboot won the race often, SD lost it consistently -> always flickered)."

Commit trail (`git log -- tools/v3d-driver-port/v3d_phoenix_power.c`):

```
3cc684cd0  2026-07-16  v3d-winsys: fix dynamic-model flicker — route GET_VIRTUAL_WH through /dev/vcmbox
18b46df45  2026-07-16  v3d-winsys: retry GET_VIRTUAL_WH — recover multi-buffering from mailbox race (flicker root cause)
```

**After that fix: 323 logs dated 2026-07-17 or later, 323× `virt_h=3240`, 0×
`virt_h=0`.** The non-determinism is gone, on the same firmware.

**(f) What actually gates the 3× grant is `config.txt`, not the firmware version.**
`sources/phoenix-rtos-project/_projects/aarch64a72-generic-rpi4b/config.txt`:

```
gpu_mem=128                  # "gpu_mem=76 only fit 2x (~16.6 MB), so the firmware refused the 3rd buffer"
max_framebuffer_height=4096  # "The default cap (~2560) limited the grant to 2 buffers"
```

Both landed in one commit — project `2fb5a62`, **2026-06-21**, *"rpi4 config: gpu_mem=128
+ max_framebuffer_height=4096 to enable triple-buffer fb"* — which is exactly why the
`virt_h=2560` logs are all dated 06-21 and nothing later. These are the levers that
decide whether the grant succeeds. A newer firmware would have to change the meaning of
`max_framebuffer_height`/`gpu_mem` to break us, and it would show up as
`virt_h=1080`/`2160` — or, per the spec (§3), `0` for "unsupported".

### Honest verdict

The pin's rationale is **anecdotal and confounded**, not root-caused:

- one uncontrolled observation (clean-VM SD build flickered) attributed to the only
  input the author had noticed changing;
- the isolating experiment was written down but never run;
- the diagnostic used (`virt_h=0`) cannot distinguish "firmware said unsupported" from
  "mailbox response lost" — and the balance of evidence is firmly on the second reading,
  because the same value appeared on the **pinned** firmware and stopped appearing, on
  that same firmware, once the mailbox was serialized.

It is **not** disproven that newer firmware might deny 3× — nothing tested it. What is
disproven is the evidence that was used to claim it. The pin currently rests on a
misdiagnosis of a bug we have since fixed properly.

---

## 3. What firmware files we consume, and what upstream looks like now

### Files we consume

`scripts/bootstrap-linux-host.sh:162-168` fetches exactly five paths from
`https://github.com/raspberrypi/firmware.git` at `$PI_FW_REF` (sparse checkout of
`boot/`, `fetch --depth 1` by sha) into `.bootblobs/`:

| file | role |
| --- | --- |
| `boot/start4.elf` | **the VideoCore firmware** (2 304 512 B) — owns display, mailbox, clocks, FB allocation |
| `boot/fixup4.dat` | memory-split fixups paired with `start4.elf` (5 499 B) |
| `boot/bcm2711-rpi-4-b.dtb` | the Pi 4B DTB — **fetched, never compiled** (`prepare-rpi4b-dtb.sh:9-13`) |
| `boot/overlays/miniuart-bt.dtbo` | frees PL011 for our console |
| `boot/overlays/vc4-fkms-v3d.dtbo` | makes the firmware ungate V3D while keeping the firmware FB |

Note what we deliberately do **not** take: `bootcode.bin` and `start.elf` both exist in
`boot/`, but they are the Pi 0-3 path — on a Pi 4 `bootcode.bin`'s job moved into the SPI
EEPROM (§4), and `start.elf` is the 32-bit/older-SoC firmware. `start4x.elf` /
`start4cd.elf` / `start4db.elf` are the extended / cut-down / debug variants; we ship the
plain one.

`scripts/assemble-rpi4b-bootfs.sh:34-79` copies these into the boot tree
`.buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/` alongside our own
`config.txt`, `kernel8.img`, `loader.disk`, `phoenix-armstub8-rpi4.bin`. That directory
**is** both the FAT boot partition content and the TFTP root
(`netboot-server-up.sh:38`: `RPI4B_NETBOOT_TFTPROOT=…/rpi4b-bootfs`). It prefers the
*staged* DTB (`_boot/…/rpi4b/bcm2711-rpi-4-b.dtb`, produced by `prepare-rpi4b-dtb.sh`
from `.bootblobs`) over the raw firmware DTB, and opportunistically copies
`start4db.elf` / `fixup4db.dat` / `start4cd.elf` / `fixup4cd.dat` if present.

### Provenance correction: the pin commit is not where our `start4.elf` comes from

`41f4808` (2026-05-18T19:17:55Z) is a **kernel bump** — *"kernel: Bump to 6.18.32"* — and
touches only `kernel*.img`, `modules/`, `System.map`, `uname_string`
([API](https://api.github.com/repos/raspberrypi/firmware/commits/41f4808270a922f08fdd927edfeb60212800fe64)).
The last commit before the pin that actually changed `boot/start4.elf` is
**`0e05e17`, 2026-05-08** (*"firmware: bootloader_state: pi4: Re-add decompressor for
VL805…"*) — which matches the `VC_BUILD_ID_TIME: May 8 2026` in our blob
([start4.elf history](https://api.github.com/repos/raspberrypi/firmware/commits?path=boot/start4.elf)).
So the graphics-relevant firmware we ship is the **2026-05-08** build; the pin sha is
just the tree we happened to fetch it from. `ae9a8e…` and `f68405…` are VideoCore
`VC_BUILD_ID` strings, **not** firmware-repo commits (the GitHub commits API returns
422 for `ae9a8e`) — do not go looking for them on GitHub.

### Latest upstream

- **master HEAD `eef9c230a0e70e23a54be98f90d5237ab6e0b0fd`, 2026-08-24**, *"kernel: Bump
  to 6.18.46"*, also tagged **`1.20260824`**
  ([tags](https://api.github.com/repos/raspberrypi/firmware/tags)). Our pin sits between
  tags `1.20260513` and `1.20260521`.
- The newest commit that actually changed `start4.elf` is **`3d301dd`, 2026-08-10**.
- `boot/` layout is **unchanged** — `start4.elf` (2 298 048 B) and `fixup4.dat` (5 512 B)
  still ship, no `firmware-2712`-style split, no renames
  ([boot/ listing](https://api.github.com/repos/raspberrypi/firmware/contents/boot)).
  Our five-file copy list is still valid.

### There are no release notes

`boot/release_notes.txt` **does not exist** in this repo and never has (raw URL 404,
absent from the `boot/` listing, empty commit history for that path). The de-facto
changelog is commit subjects. Every commit touching `boot/start4.elf` **after our pin**:

| commit | date | subject (firmware lines only) |
| --- | --- | --- |
| `495ed91` | 2026-05-21 | rpi-fw-crypto key usage in OTP; pi4 board info in OTP; **arm_loader: Firmware clock rework part 1** |
| `980e91e` | 2026-06-19 | Restrict `SET_VOLTAGE` to core voltage on Pi4+; avoid a relocation; `arm_dt` optimisations + overlay memory leak; add `CLOCK_EMMC2` |
| `ea3bc5a` | 2026-07-01 | Fix `auto_initramfs` take 3 |
| `8402891` | 2026-07-23 | kernel 6.18.39; camera autodetect I2C caching; 2835 SDRAM column trim |
| `2cfe163` | 2026-08-06 | **arm_loader: Cache state needed for clocks that is slow to access**; i2c/spi splash via `splash.bin` |
| `3d301dd` | 2026-08-10 | `arm_loader_dvfs` trace null-deref fix; **config: tidyup after cached mailbox merge**; video_decode VPU0 option |

**Negative finding: none of them mentions framebuffer, virtual width/height, virtual
offset, dispmanx, `FB_ALLOCATE`, double/triple buffering, or vc4/kms.** So the owner's
"they shipped a broken build and later fixed it" hypothesis has **no upstream paper
trail** — neither a break nor a fix is announced. If a behaviour change is real it is an
undocumented side effect.

Two entries are at least *thematically* adjacent to our symptom, because our symptom is
mailbox-shaped: `2cfe163` "Cache state needed for clocks that is slow to access" and
`3d301dd` "config: tidyup after **cached mailbox** merge" (2026-08). That is suggestive
only — worth knowing if a bump misbehaves, not evidence of anything.

**If a bisect is ever needed** (only if the §5 test fails): the historical window is
good = `0e05e17` (2026-05-08, `ae9a8e`) → bad = `ea3bc5a` (2026-07-01, `f68405`, the
build the 07-04 clean VM picked up), i.e. three candidates: `495ed91`, `980e91e`,
`ea3bc5a`. Each is one `PI_FW_REF` value and one cycle.

### What the spec actually says about the grant — this matters

[Mailbox property interface wiki](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface),
tag `0x00048004` **Set virtual (buffer) width/height**, verbatim:

> "The response may not be the same as the request so it must be checked. **May be the
> previous width/height or 0 for unsupported.**"

and `0x00048009` Set virtual offset, verbatim:

> "The response may not be the same as the request so it must be checked. May be the
> previous offset or **0 for unsupported**."

Two consequences:

1. **A partial or refused grant is legal and expected.** Firmware returning 2160 or 1080
   instead of 3240 is within contract; our fallback-to-fewer-buffers logic is correct by
   spec, and "the firmware regressed" would be the wrong framing even if it happened.
2. **`0` is a legal firmware reply for "unsupported"** — so `virt_h=0` is genuinely
   *ambiguous* between "lost mailbox response" and "firmware says unsupported". This is
   the honest qualification to §2(b): the reading cannot distinguish the two. What still
   settles the 2026-07-04 question is §2(c) and §2(e) — the **pinned** firmware produced
   `virt_h=0` too, and serializing the mailbox made it stop happening — not the value's
   semantics. It also means the post-bump check must look for `3240`, and must not treat
   a `0` as "firmware denied" without re-running.

Worth noting: plo's `video_framebufferInit()`
(`sources/plo/hal/aarch64/generic/video.c:148-189`) issues the `SET_VIRTUAL_WH` but
**never inspects the response** (`video_mailbox[10]`/`[11]`) and never logs it — it
validates only depth, base and pitch. That is why the granted height is only ever
observed much later, in userspace, through the winsys `GET`. A 3-line plo print of the
SET response would make any future firmware question answerable from the boot log alone,
with no GL app needed. Not proposed as part of this test (it is a build-input change),
but it is the cheap permanent fix to the observability gap.

### Deprecation status of what we rely on

- The **channel-1 single-struct framebuffer interface** is explicitly deprecated —
  [wiki](https://github.com/raspberrypi/firmware/wiki/Mailbox-framebuffer-interface):
  *"This particular Mailbox call is deprecated, and not guaranteed to work as expected."*
  **We do not use it** — we use the property interface (channel 8), which carries no
  deprecation note.
- **DispmanX and the legacy/FKMS stacks were removed in Raspberry Pi OS Bookworm** and do
  not exist on Pi 5 ([forum](https://forums.raspberrypi.com/viewtopic.php?t=358280),
  [forum](https://forums.raspberrypi.com/viewtopic.php?t=365082)). That is an **OS-side**
  removal (Linux DRM/userland), not a firmware one; `start4.elf` still ships and still
  owns the Pi 4 display. No Raspberry Pi statement was found retiring the firmware
  framebuffer on Pi 4. Still, this is the one strategic risk worth naming: the legacy
  firmware-FB path is on a long-term deprecation curve, so the further we drift from
  2026-05 firmware the more likely we eventually meet a real change.
- **Nobody else has reported our regression.** Searches of raspberrypi/firmware issues
  and commits for framebuffer/virtual-height changes turn up nothing after 2021; the
  newest FB-titled issues are 2025 and unrelated. There is one loose precedent that
  `start4.elf` *can* silently change legacy-FB behaviour: RISC OS Open reports the
  2024-05 firmware builds stopped honouring `framebuffer_swap=0`
  ([thread](https://www.riscosopen.org/forum/forums/4/topics/19344) — fetch returned
  HTTP 429, so this is a search snippet, not verified verbatim).
- **Untested config lever, if a bump does come back short:** the legacy config docs define
  `max_framebuffer_width`/`max_framebuffer_height` as *"the maximum dimensions of the
  internal frame buffer"* and `max_framebuffers` as the number of firmware framebuffers
  (0/1/2, default 2 on Pi 4)
  ([legacy video.adoc](https://raw.githubusercontent.com/raspberrypi/documentation/master/documentation/asciidoc/computers/legacy_config_txt/video.adoc)).
  We already set `max_framebuffer_height=4096` but **not** `max_framebuffer_width`. If
  newer firmware clamps the virtual grant against both, adding
  `max_framebuffer_width=1920` is the first thing to try. Caveat: these options are
  documented only on the *legacy* config page, whose scope is options that "don't work
  with Raspberry Pi OS Bookworm and later, and are no longer officially supported", and
  `max_framebuffer*` is absent from the current `config_txt/video.adoc`.

---

## 4. The EEPROM question, precisely

**Two different things are called "firmware" on a Pi 4:**

| | Pi 4 SPI EEPROM (`rpi-eeprom`) | `start4.elf` (`raspberrypi/firmware`) |
| --- | --- | --- |
| repo | `raspberrypi/rpi-eeprom`, dir `firmware-2711/` | `raspberrypi/firmware`, dir `boot/` |
| what it is | the **second-stage bootloader** (replaces the Pi 0-3 `bootcode.bin`) | the **VideoCore GPU firmware / OS** |
| where it lives | 512 KB SPI flash soldered on the board | the FAT boot partition, or fetched over TFTP |
| how it is updated | `recovery.bin` + `pieeprom.bin` flash cycle — a **write to the chip** | copy a file; nothing is written to the board |
| what it does for us | POST, DRAM init, `BOOT_ORDER` (SD / USB / **network**), TFTP client, then loads `start4.elf` | HDMI mode, framebuffer allocation, mailbox property interface, clocks, V3D power |
| our knob | `scripts/prepare-pi-eeprom-netboot.sh` | `PI_FW_REF` |

**Does bumping `start4.elf` require an EEPROM update? No.** The bootloader's contract
with `start4.elf` is "read this file into memory at a fixed address and jump" — visible
verbatim in our own netboot log
(`rpi4b-uart-20260623-061317-…log:204-208`):

```
 41.34 Read fixup4.dat bytes     5499 hnd 0x0
 41.41 MEM GPU: 128 ARM: 896 TOTAL: 1024
 41.33 Firmware: ae9a8ea9f3ca745de6f357bd7fc8307721ad38b7 May  8 2026 18:13:05
 41.69 Starting start4.elf @ 0xfec00200 partition -1
```

The bootloader loads `fixup4.dat`, applies the memory split, reports the `start4.elf`
build id it just loaded, and jumps. That interface has been stable for the life of the
Pi 4. Newer `start4.elf` on an older bootloader is the normal state of every
`apt upgrade` on Raspberry Pi OS.

The official docs describe the same split of duties and impose **no** version lock.
[`bootflow-eeprom.adoc`](https://raw.githubusercontent.com/raspberrypi/documentation/master/documentation/asciidoc/computers/raspberry-pi/bootflow-eeprom.adoc):
the ROM first stage loads `recovery.bin` / the SPI EEPROM; the **second-stage (EEPROM)
bootloader** initialises hardware, reads the EEPROM config, then walks `BOOT_ORDER` to
load the firmware.
[`eeprom-bootloader.adoc`](https://raw.githubusercontent.com/raspberrypi/documentation/master/documentation/asciidoc/computers/raspberry-pi/eeprom-bootloader.adoc),
verbatim: *"After reading `config.txt` the GPU firmware `start4.elf` reads the bootloader
EEPROM config and checks for a section called `[config.txt]`"* — the only coupling being
that `start4.elf` may *read* EEPROM config. **Neither doc states that a newer `start4.elf`
requires a newer bootloader.**

Coupling in `rpi-eeprom`'s own release notes is always **feature-specific and
announced**, never blanket — historical examples:
[`firmware-2711/release-notes.md`](https://github.com/raspberrypi/rpi-eeprom/blob/master/firmware-2711/release-notes.md)
2020-05-15: *"USB mass storage boot will NOT work without the updated firmware start.elf
binaries."*; 2021-03-04 (NVMe boot, beta): *"This requires the latest rpi-update firmware
to work or else you will see a compatibility error on boot."* Nothing comparable appears
for anything we use, and nothing in the 2026 entries
(**2026-05-17** "Update Broadcom DDR firmware to 2.35"; **2026-08-04** "arm_mbox: Avoid
slow calls every mbox message") requires a matching `start4.elf`. The reverse direction
exists too — a *newer board revision* can need a newer `start4.elf` ("This board requires
newer software") — but that is about hardware revisions, not our case.

**One clarification of our own notes:** `bootstrap-linux-host.sh:159` and the 07-04 doc
read as if `ae9a8e` identified repo commit `41f4808`. It does not: `ae9a8e` is
`start4.elf`'s own `VC_BUILD_ID_VERSION` (verified by `strings`, §1) and the blob is
really the 2026-05-08 `0e05e17` build (§3). The
`Firmware: ae9a8e…` boot line is the **bootloader reporting the `start4.elf` it
loaded** — not the bootloader's own version. That line is therefore a valid firmware
discriminator, and it says every logged boot we have ran `ae9a8e`.

**When *would* the owner need to reflash the EEPROM?** Only for bootloader-side
behaviour, none of which is triggered by a `start4.elf` bump:

- **Boot order / netboot.** Already done once: `artifacts/eeprom-netboot/eeprom-prep-sd.img`
  (generated 2026-05-21) wrote `BOOT_ORDER=0xf12`, `TFTP_PREFIX=2`, `BOOT_UART=0`.
  `prepare-pi-eeprom-netboot.sh:14-21` documents why `BOOT_UART=0` matters (a
  `BOOT_UART=1` bootloader pre-programs PL011 with its own divisor and garbles plo).
- **TFTP / DHCP client fixes** in the bootloader itself (a netboot reliability issue,
  not a graphics issue).
- **A release that explicitly states a minimum bootloader version.** This is the only
  coupling that could exist. `raspberrypi/firmware` publishes no notes at all (§3), so
  the place such a requirement has historically appeared is `rpi-eeprom`'s own
  `firmware-2711/release-notes.md` — and no 2026 entry there states one.

Vintages, for the record. Our `external/rpi-eeprom` clone is at `c5ea2eb` =
`pieeprom-2026-05-20` (`manifests/release-pin.md:54-56` records it as Tier-2, lab-only,
deliberately unpinned); the board itself was flashed from
`artifacts/eeprom-netboot/eeprom-prep-sd.img`, generated **2026-05-21**. Upstream today
([`firmware-2711/versions.txt`](https://raw.githubusercontent.com/raspberrypi/rpi-eeprom/master/firmware-2711/versions.txt))
offers `default = pieeprom-2026-05-17` and `latest = pieeprom-2026-08-04`. So our
bootloader is roughly at upstream *default* — i.e. current for the stable channel, three
months behind `latest`. Nothing about that gates a `start4.elf` bump.

**Bottom line for the owner: do not reflash anything for this test.** Nothing found in
either repo's documentation couples a `start4.elf` bump to a bootloader version. If a
future bump ever does state such a requirement, an EEPROM update is a separate,
independent step with its own one-boot verification (rapid green-LED blink), and it is
fully reversible by flashing an older `pieeprom.bin`.

---

## 5. Test plan — one netboot cycle, no rebuild, one-command rollback

The whole test is a **file swap in the TFTP root**. No source change, no
`rebuild-rpi4b-fast.sh`, no SD card, no `config.txt` edit, nothing committed.

### Setup (host only, no Pi)

1. Fetch a candidate firmware into a scratch tree (leave `.bootblobs/` untouched):

   ```
   git init -q /tmp/fw-new && git -C /tmp/fw-new remote add origin https://github.com/raspberrypi/firmware.git
   git -C /tmp/fw-new config core.sparseCheckout true
   git -C /tmp/fw-new sparse-checkout init --cone
   git -C /tmp/fw-new sparse-checkout set boot
   git -C /tmp/fw-new fetch --depth 1 --filter=blob:none origin <CANDIDATE_SHA>
   git -C /tmp/fw-new checkout -q --detach FETCH_HEAD
   ```

   **Which candidate: `eef9c230a0e70e23a54be98f90d5237ab6e0b0fd`** — master HEAD /
   tag `1.20260824`, 2026-08-24, whose `start4.elf` is the 2026-08-10 `3d301dd` build
   (§3). Test **this one only**; if it passes it becomes the new pin. If it fails, the
   bisect candidates are `495ed91` → `980e91e` → `ea3bc5a` (§3), one cycle each.

2. Record what you are about to boot, so the log is self-identifying even with
   `uart_2ndstage=0`:

   ```
   strings -a /tmp/fw-new/boot/start4.elf | grep VC_BUILD_ID_VERSION
   strings -a /home/houp/phoenix-rpi/.bootblobs/start4.elf | grep VC_BUILD_ID_VERSION   # ae9a8e… baseline
   ```

3. Restage the boot tree from the new firmware — one command, and it only rewrites the
   firmware-owned files (our `kernel8.img` / `loader.disk` / `config.txt` come from the
   buildroot, and the **DTB stays the pinned one** because `assemble-rpi4b-bootfs.sh:57-61`
   prefers the already-staged `_boot/…/rpi4b/bcm2711-rpi-4-b.dtb`):

   ```
   RPI4B_FIRMWARE_DIR=/tmp/fw-new/boot /home/houp/phoenix-rpi/scripts/assemble-rpi4b-bootfs.sh
   ```

   This isolates the variable to `start4.elf` + `fixup4.dat` + overlays — exactly the A/B
   that was designed on 2026-07-04 and never run.

4. **Prove the swap is actually what will boot** (do this after the swap, and again after
   the cycle if the result surprises you):

   ```
   strings -a /home/houp/phoenix-rpi/.buildroot/_boot/aarch64a72-generic-rpi4b/rpi4b-bootfs/start4.elf \
     | grep VC_BUILD_ID_VERSION
   ```

   It must **not** read `ae9a8e…`. `test-cycle-netboot.sh` does no restaging of its own
   (verified: it only *documents* `rebuild-rpi4b-fast.sh` as a separate preceding step),
   so the swap survives the cycle — but **do not run `rebuild-rpi4b-fast.sh` between the
   swap and the boot**: it calls `assemble-rpi4b-bootfs.sh` with the default
   `RPI4B_FIRMWARE_DIR` and would silently restore the pinned blobs, yielding a false
   PASS. This one command makes the plan immune to that.

### The cycle

```
./scripts/test-cycle-netboot.sh --capture-secs 240        # Bash timeout: 420000
```

`test-cycle-netboot.sh` is passive capture, so to type at `(psh)%` use the interactive
cycle instead — `./scripts/test-cycle-psh-interact.sh` (or the `rpi4-run` skill recipe) —
and run any GL app that brings the winsys up: `quakespasm-sdl` is the historical
reference, `stk` or `gl_es_smoke` are cheaper. The `scanout init` line prints during GL
context creation, before any frame is drawn, so **even a boot that fails later still
answers the question.**

```
./scripts/uart-summary.sh <label>
grep "scanout init" artifacts/rpi4b-uart/<log>
```

### Pass / fail criterion

Read the one line. Unlike in July, it is now a trustworthy signal: since `3cc684c` the
query is serialized and has been 323/323 deterministic, so a *changed* reading means the
firmware changed something rather than "this boot lost the race":

| `scanout init` reading | verdict | action |
| --- | --- | --- |
| `virt_h=3240 -> 3 buffer(s) TRIPLE-BUFFER+page-flip` | **PASS** — no firmware regression exists (or it was fixed upstream) | bump `PI_FW_REF` to the candidate sha, rewrite the comment at `bootstrap-linux-host.sh:150-159`, fix `manifests/release-pin.md:57-58`, correct `rpi4-os-development-guide.md:802-806` |
| `virt_h=1080` or `virt_h=2160 -> 1/2 buffer(s)` | **FAIL, and genuinely firmware** — a real partial grant came back through a clean channel | keep the pin; first try the config levers (§2f, §3): add `max_framebuffer_width=1920`, confirm `max_framebuffer_height=4096` + `gpu_mem=128` still apply, re-test; only then bisect `495ed91`/`980e91e`/`ea3bc5a` |
| `virt_h=0 -> 1 buffer(s) single` | **inconclusive** — either the mailbox query was lost (the old race signature) or the firmware replied "unsupported" (both are legal, §3) | check for the `GET_VIRTUAL_WH failed via /dev/vcmbox` line and whether `rpi4-vcmbox` started; re-run before drawing any conclusion — do **not** record this as "firmware denied", which is the mistake of 2026-07-04 |

Optional second cycle (only if the first passes): 3 boots via
`./scripts/test-cycle-bench.sh 3 fwbump` to confirm the reading is stable, plus one HDMI
snapshot of a GL app for an eyeball check that nothing else regressed (HDMI mode,
overscan, colours). `artifacts/hdmi/` grabs are automatic.

### Rollback — one command

```
/home/houp/phoenix-rpi/scripts/assemble-rpi4b-bootfs.sh
```

With `RPI4B_FIRMWARE_DIR` unset it defaults to `$repo/.bootblobs`
(`assemble-rpi4b-bootfs.sh:13`), which still holds the pinned `ae9a8e` blobs — the test
never wrote to it. That restores the TFTP root **and** the FAT image source in one step.
Belt and braces: `rm -rf /tmp/fw-new`. Nothing in git changed, so there is nothing to
revert; the known-good rollback manifest
`manifests/2026-07-16-flicker-vcmbox-fixed-knowngood.md` remains the anchor if the
software side ever needs it.

### Cost

One cycle, ~7 minutes wall clock, zero source changes, zero risk to the SD card (the
test is netboot-only). The UART line answers the owner's question outright.
