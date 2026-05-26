# BCM43455 firmware blobs — provenance, licensing, staging

The Phoenix-RTOS Pi 4 WiFi driver (impl plan
[`plans/wifi-bcm43455-impl.md`](plans/wifi-bcm43455-impl.md), phase
P3+) needs three Cypress firmware files at runtime to bring the
BCM43455c0 radio out of reset:

| File                                  | Size      | Role                              |
|---------------------------------------|----------:|-----------------------------------|
| `brcmfmac43455-sdio.bin`              | 643 651 B | Main firmware — downloaded into BCM43455 SOCRAM, executed by the chip's ARM-CR4 |
| `brcmfmac43455-sdio.clm_blob`         |   4 733 B | Country Locale Matrix — regulatory tables for channel/power masks per country |
| `brcmfmac43455-sdio.txt`              |   1 883 B | NVRAM / MFGTEST — Pi 4-specific board parameters (antennas, MAC OTP location, PA tuning) |

These blobs are **not vendored in this git repository**. Bring them
in via `scripts/stage-bcm43455-firmware.sh` which writes them to a
gitignored `.firmware/` directory:

```sh
./scripts/stage-bcm43455-firmware.sh
ls .firmware/
  brcmfmac43455-sdio.bin
  brcmfmac43455-sdio.clm_blob
  brcmfmac43455-sdio.txt
```

The script tries sources in order:

1. **Local Debian/Ubuntu firmware packages** (zstd-compressed, fastest):
   - `/lib/firmware/cypress/cyfmac43455-sdio.bin.zst`
   - `/lib/firmware/cypress/cyfmac43455-sdio.clm_blob.zst`
   - `/lib/firmware/brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt.zst`

   Installed by `apt install firmware-cypress firmware-brcm80211` on
   Debian bookworm and similar. The Cypress and brcm tracks of
   `firmware-nonfree` symlink to the same physical files.

2. **Plain (non-zstd) variants** of the same paths, for distros that
   don't compress.

3. **Direct download from raspberrypi/firmware-nonfree** on GitHub
   (canonical upstream).

Each candidate is checked against a known-good SHA256 (recorded in
the script). Mismatches print a warning but don't fail — Cypress
ships periodic firmware updates and the SHA may shift over time.

## Licensing

- **Phoenix-RTOS code** in this repo is **BSD-3-Clause**.
- **The firmware blobs are NOT BSD.** They are binary firmware
  redistributed under the **Cypress firmware redistribution
  permission** as shipped alongside the files in the
  `firmware-cypress` and `firmware-brcm80211` Debian packages and in
  the `raspberrypi/firmware-nonfree` repo. Cypress permits
  redistribution but reserves modification rights; ship the blobs
  unmodified.
- The blob redistribution rights cover both downstream binary
  distribution (e.g., a Phoenix-RTOS image containing the blob in a
  C-array form) and embedded use. They do not extend to disassembly
  or reverse-engineering.
- When the Phoenix Pi 4 image starts shipping the blob, add a
  `LICENSE.firmware` file alongside the image artifact pointing at
  the Cypress redistribution notice.

## Cypress 7.45.241 BCM43455c0 firmware (current)

The SHA256 fingerprints recorded in
`scripts/stage-bcm43455-firmware.sh` correspond to the Cypress
7.45.241 release, packaged as the Debian `firmware-cypress 20230625-2`
or equivalent. This is the version Linux's `brcmfmac` driver loads
by default on Pi 4 with mainline kernel 6.x.

Source URL for direct download:
- `https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/master/debian/config/brcm80211/brcm/brcmfmac43455-sdio.bin`
- `https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/master/debian/config/brcm80211/brcm/brcmfmac43455-sdio.clm_blob`
- `https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/master/debian/config/brcm80211/brcm/brcmfmac43455-sdio.raspberrypi,4-model-b.txt`

## How the firmware reaches the chip at runtime

Per the impl-plan P3 design:

1. Phoenix WiFi server binary embeds the staged blobs as a `const
   uint8_t[]` C array via a build-time `xxd -i` pre-step. The
   server's `Makefile` includes the staged file from `.firmware/`.
2. At driver init, the server programs the SDIO backplane window
   onto SOCRAM (base 0x18004000 for BCM43455c0), holds the chip's
   ARM-CR4 in reset, and streams the firmware blob via CMD53
   block writes into SOCRAM offset 0x0.
3. The NVRAM `.txt` file is parsed, packed into key=value records
   per Broadcom format, length-prefixed, and written into the *end*
   of SOCRAM (per the brcmfmac convention).
4. The CLM blob is uploaded via IOCTL (`clmload_status` + `clmload`)
   after firmware is alive.
5. ARM-CR4 is released; the chip boots, prints a firmware-version
   string over the SDIO BCDC mailbox.

This is the standard brcmfmac + WHD bring-up sequence; any
embedded WiFi reference implementation (NetBSD bwfm, Pico-SDK
cyw43-driver, OpenWRT brcm-bus) follows the same shape.

## What to do when Cypress ships a new firmware version

When `firmware-cypress` upstream bumps the version:

1. Update the SHA256s in `scripts/stage-bcm43455-firmware.sh`
   (the `want_sha256` array).
2. Rerun the script on the dev host — fresh download / re-stage.
3. Rebuild the WiFi server (the blob is embedded at build time).
4. Run a smoke-test cycle and verify the chip's reported firmware
   version line over BCDC matches the new upstream version.
5. Commit the SHA256 updates (no blob updates needed in git since
   the blobs are gitignored).

Older firmware versions can be retained by checking out the
relevant `firmware-nonfree` commit before staging — the script does
not enforce a particular version.
