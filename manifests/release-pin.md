# Release Pin — reproducible Phoenix-RTOS RPi4 build

## Summary

- Date: 2026-07-02
- Task: #71 (publication readiness, Phase 1)
- Note: Pinned-SHA snapshot of the current validated integration state for a
  reproducible build from an empty dir. Records all 16 sibling source repos
  under `sources/` **and** the 3 build-required external deps under `external/`.
- Generator: hand-authored to match `scripts/snapshot-integration-state.sh`
  format; the `integration-state-v1` block is consumed by
  `scripts/restore-integration-state.sh` and `bootstrap-linux-host.sh --pinned`.
- Bump policy: after validating a new known-good image, regenerate the sibling
  block with `snapshot-integration-state.sh`, refresh the external-pins block +
  the `EXTERNAL_DEPS` refs in `bootstrap-linux-host.sh` together, and re-verify.

## Sibling repositories (16)

| Repository | Branch | Commit SHA |
| --- | --- | --- |
| libphoenix | master | 9128c5ddb7cb7ffdf4142619ea8d1fd2f9dbc542 |
| phoenix-rtos-build | master | 4ddabeef40778589374fa2c8078f06e7244d4847 |
| phoenix-rtos-corelibs | master | 2311290343e37cde2440ea7056119743150bf631 |
| phoenix-rtos-devices | master | 23bc6070ee90996b7b42ce1fe059478c10662e81 |
| phoenix-rtos-doc | master | deb40ffcf957bf72eaf0a4cedbb77922254c6439 |
| phoenix-rtos-filesystems | master | 463aec13fbea6b436feeb34c11644b68e82bc04e |
| phoenix-rtos-hostutils | master | aa0c55a1bc12cbdf6a169bd3c15f6c636bd5b7be |
| phoenix-rtos-kernel | master | 8834eaf35ed3f872dc3422ba6ca5a5aabacc8a96 |
| phoenix-rtos-lwip | master | dffa8140739e925f050e162b3815a00763bab5d5 |
| phoenix-rtos-ports | master | 205e4a9421da679c10026fb0e70a6c06c5b5df21 |
| phoenix-rtos-posixsrv | master | ff04a1b3a669238147ef8c7c5bc28c2e3652f76d |
| phoenix-rtos-project | master | 7d7db561225d562d43f0cb41449a1e599b97e1c4 |
| phoenix-rtos-tests | master | d5d4cb13145bb2f8d4717a7f8757895c96d107f4 |
| phoenix-rtos-usb | master | 12c4fe85b4a0d5731ffff110dda536a06905d35c |
| phoenix-rtos-utils | master | 92d23e09b274b5bf7aa0b7685b6625fa2e024b8a |
| plo | master | 93881dbabec04440b843b58c7a4f52363e0a365f |

## Build-required external dependencies (3)

These feed the V3D GPU / GL / Vulkan stack. Cloned + pinned by
`bootstrap-linux-host.sh` (see its `EXTERNAL_DEPS` array — keep the refs in
sync with the `external-pins-v1` block below).

| Dir | Upstream | Pinned SHA | Consumed by |
| --- | --- | --- | --- |
| external/mesa | https://gitlab.freedesktop.org/mesa/mesa.git | e8791b4bc1c10af74ddd3af029fbf06cafc11d56 | tools/v3d-driver-port → libGL/libv3d/libv3dv |
| external/quakespasm | https://github.com/sezero/quakespasm.git | 4abb3249fe45c835d3d8540845a18a114e283996 | tools/quakespasm-port → libquakespasm.a |
| external/vkquake | https://github.com/Novum/vkQuake.git | f4d923e36f6a2cbb6e796031eb81c88f23db8520 | tools/vkquake-port → libvkquake.a |

### NOT pinned here (deliberately)

- **external/linux** — research-only; the Pi 4 DTB is fetched ready-made from
  `raspberrypi/firmware`, never compiled. Not needed for the build.
- **external/rpi-eeprom** (`c5ea2eb4dc4de9700d4102cbb5517efb3c1eced6`) — Tier-2
  lab/netboot only; `prepare-pi-eeprom-netboot.sh` self-clones it on demand.
  Recorded here for completeness only.
- **raspberrypi/firmware** — boot blobs + DTB; staged by `stage_pi_firmware`
  (currently tracks `master`; pin via `PI_FW_REF` for a fully reproducible boot).

## Machine-Parseable State

Consumed by `scripts/restore-integration-state.sh`. Fields: `<repo>\t<sha>\t<branch>`.

```integration-state-v1
libphoenix	9128c5ddb7cb7ffdf4142619ea8d1fd2f9dbc542	master
phoenix-rtos-build	4ddabeef40778589374fa2c8078f06e7244d4847	master
phoenix-rtos-corelibs	2311290343e37cde2440ea7056119743150bf631	master
phoenix-rtos-devices	23bc6070ee90996b7b42ce1fe059478c10662e81	master
phoenix-rtos-doc	deb40ffcf957bf72eaf0a4cedbb77922254c6439	master
phoenix-rtos-filesystems	463aec13fbea6b436feeb34c11644b68e82bc04e	master
phoenix-rtos-hostutils	aa0c55a1bc12cbdf6a169bd3c15f6c636bd5b7be	master
phoenix-rtos-kernel	8834eaf35ed3f872dc3422ba6ca5a5aabacc8a96	master
phoenix-rtos-lwip	dffa8140739e925f050e162b3815a00763bab5d5	master
phoenix-rtos-ports	205e4a9421da679c10026fb0e70a6c06c5b5df21	master
phoenix-rtos-posixsrv	ff04a1b3a669238147ef8c7c5bc28c2e3652f76d	master
phoenix-rtos-project	7d7db561225d562d43f0cb41449a1e599b97e1c4	master
phoenix-rtos-tests	d5d4cb13145bb2f8d4717a7f8757895c96d107f4	master
phoenix-rtos-usb	12c4fe85b4a0d5731ffff110dda536a06905d35c	master
phoenix-rtos-utils	92d23e09b274b5bf7aa0b7685b6625fa2e024b8a	master
plo	93881dbabec04440b843b58c7a4f52363e0a365f	master
```

## Machine-Parseable External Pins

Kept in sync with `bootstrap-linux-host.sh`'s `EXTERNAL_DEPS` array. Fields:
`<subdir>\t<git-url>\t<pinned-sha>`.

```external-pins-v1
mesa	https://gitlab.freedesktop.org/mesa/mesa.git	e8791b4bc1c10af74ddd3af029fbf06cafc11d56
quakespasm	https://github.com/sezero/quakespasm.git	4abb3249fe45c835d3d8540845a18a114e283996
vkquake	https://github.com/Novum/vkQuake.git	f4d923e36f6a2cbb6e796031eb81c88f23db8520
```

## Host apt dependencies (Ubuntu x86_64)

Authoritative list also encoded in `bootstrap-linux-host.sh` (`APT_PACKAGES`).
For a clean build & flash:

```
sudo apt-get update && sudo apt-get install -y --no-install-recommends \
  build-essential bison flex texinfo libgmp-dev libmpfr-dev libmpc-dev wget xz-utils \
  gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu cmake pkg-config make \
  device-tree-compiler mtools dosfstools parted e2fsprogs \
  git git-lfs curl jq python3 python3-pip python3-venv
# uv (Python venv tool) is not in apt:
curl -LsSf https://astral.sh/uv/install.sh | sh
```

Add `dnsmasq iproute2 tio picocom ffmpeg v4l-utils gh` only for the optional
Tier-2 lab rig (netboot / UART / HDMI capture); they are not needed to build
and flash an SD image.
