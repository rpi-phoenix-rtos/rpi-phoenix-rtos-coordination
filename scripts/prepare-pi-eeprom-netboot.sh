#!/usr/bin/env bash
#
# One-time Pi 4 EEPROM-update SD image generator.
#
# Produces a small FAT32 SD image containing recovery.bin + pieeprom.upd +
# pieeprom.sig, configured to enable network boot in the Pi 4 EEPROM. After
# you flash this image to a microSD card and boot the Pi 4 once with that
# card inserted, the green LED will blink rapidly (success) — power off,
# remove the card, and the Pi will then attempt SD → USB → Network on every
# subsequent boot.
#
# Output: artifacts/eeprom-netboot/eeprom-prep-sd.img
#
# Default config written into the EEPROM:
#   BOOT_ORDER=0xf12    (network first, SD recovery fallback, then halt — fast)
#   TFTP_PREFIX=2       (no per-serial subdirectory; files at TFTP root)
#   BOOT_UART=0         (silent EEPROM — leaves PL011 untouched for plo/Phoenix.
#                        With BOOT_UART=1 the EEPROM pre-initialises PL011 with
#                        its own clock setup; start4.elf then inherits a baud
#                        divisor that doesn't match plo's expected 115200, and
#                        UART output gets garbled. See docs/netboot-test-cycle.md.)
#

set -euo pipefail

vm="${PHOENIX_VM:-phoenix-dev}"
eeprom_dir_vm="${PHOENIX_EEPROM_DIR:-/home/witoldbolt.guest/external/rpi-eeprom}"
work_dir_vm="${PHOENIX_EEPROM_WORK:-/home/witoldbolt.guest/phoenix-buildroots/eeprom-netboot}"
out_dir_host="${RPI4B_EEPROM_OUT_DIR:-/Users/witoldbolt/phoenix-rpi/artifacts/eeprom-netboot}"

boot_order="${PI_EEPROM_BOOT_ORDER:-0xf12}"
tftp_prefix="${PI_EEPROM_TFTP_PREFIX:-2}"
boot_uart="${PI_EEPROM_BOOT_UART:-0}"

mkdir -p "$out_dir_host"

limactl shell -y "$vm" -- /bin/bash -lc "
set -euo pipefail

eeprom_dir='$eeprom_dir_vm'
work_dir='$work_dir_vm'
boot_order='$boot_order'
tftp_prefix='$tftp_prefix'
boot_uart='$boot_uart'

# 1. Fetch or refresh rpi-eeprom upstream (shallow).
if [ ! -d \"\$eeprom_dir/.git\" ]; then
	mkdir -p \"\$(dirname \"\$eeprom_dir\")\"
	git clone --depth=1 https://github.com/raspberrypi/rpi-eeprom.git \"\$eeprom_dir\"
else
	git -C \"\$eeprom_dir\" fetch --depth=1 origin master
	git -C \"\$eeprom_dir\" reset --hard origin/master
fi

stable_dir=\"\$eeprom_dir/firmware-2711/stable\"
if [ ! -d \"\$stable_dir\" ]; then
	# Newer repo layout uses firmware-2711/latest or firmware/stable.
	for alt in firmware-2711/latest firmware/stable firmware/latest; do
		if [ -d \"\$eeprom_dir/\$alt\" ]; then
			stable_dir=\"\$eeprom_dir/\$alt\"
			break
		fi
	done
fi
if [ ! -d \"\$stable_dir\" ]; then
	printf 'cannot find Pi 4 EEPROM firmware directory under %s\n' \"\$eeprom_dir\" >&2
	exit 1
fi

pieeprom_src=\$(ls \"\$stable_dir\"/pieeprom-*.bin 2>/dev/null | sort | tail -n1)
recovery_src=\"\$stable_dir/recovery.bin\"
if [ -z \"\$pieeprom_src\" ] || [ ! -f \"\$recovery_src\" ]; then
	printf 'missing pieeprom-*.bin or recovery.bin in %s\n' \"\$stable_dir\" >&2
	exit 1
fi

# 2. Build a fresh staging dir with the 3 files the EEPROM updater needs.
rm -rf \"\$work_dir\"
mkdir -p \"\$work_dir/staging\"

cp -f \"\$recovery_src\" \"\$work_dir/staging/recovery.bin\"

# 3. Embed our boot-order config into pieeprom.bin via rpi-eeprom-config.
config_file=\"\$work_dir/bootconf.txt\"
{
	printf 'BOOT_UART=%s\n' \"\$boot_uart\"
	printf 'WAKE_ON_GPIO=1\n'
	printf 'POWER_OFF_ON_HALT=0\n'
	printf 'BOOT_ORDER=%s\n' \"\$boot_order\"
	printf 'TFTP_PREFIX=%s\n' \"\$tftp_prefix\"
	printf 'NET_INSTALL_AT_POWER_ON=0\n'
	printf 'ENABLE_SELF_UPDATE=0\n'
} > \"\$config_file\"

if [ ! -x \"\$eeprom_dir/rpi-eeprom-config\" ]; then
	printf 'rpi-eeprom-config helper not found in %s\n' \"\$eeprom_dir\" >&2
	exit 1
fi

# Note: the file is named pieeprom.bin (NOT .upd). recovery.bin uses
# pieeprom.bin and unconditionally reflashes the EEPROM. The .upd name
# would instead trigger the bootloader's SELF-UPDATE path, which compares
# embedded timestamps and may skip with 'SELF-UPDATE ... new 0 skip'.
\"\$eeprom_dir/rpi-eeprom-config\" \\
	--config \"\$config_file\" \\
	--out \"\$work_dir/staging/pieeprom.bin\" \\
	\"\$pieeprom_src\"

# 4. Generate pieeprom.sig via rpi-eeprom-digest (sha256 + 'ts: <epoch>').
\"\$eeprom_dir/rpi-eeprom-digest\" \\
	-i \"\$work_dir/staging/pieeprom.bin\" \\
	-o \"\$work_dir/staging/pieeprom.sig\"

# 4b. If the chosen firmware tree has a vl805 blob (older Pi 4 revs with
# separate USB controller EEPROM), bundle it too. New Pi 4 boards have
# vl805 baked into pieeprom.bin and don't need this; recovery.bin will
# silently skip if the file is absent.
vl805_src=\$(ls \"\$stable_dir\"/vl805-*.bin 2>/dev/null | sort | tail -n1 || true)
if [ -n \"\$vl805_src\" ] && [ -f \"\$vl805_src\" ]; then
	cp -f \"\$vl805_src\" \"\$work_dir/staging/vl805.bin\"
	sha256sum \"\$work_dir/staging/vl805.bin\" | awk '{print \$1}' > \"\$work_dir/staging/vl805.sig\"
fi

# 5. Build a 256 MiB FAT32 filesystem image with the files at the root.
# Size and cluster geometry match the upstream make-recovery-images script:
# the Pi 4 EEPROM bootloader requires a true FAT32 (>= 65525 clusters), so
# the partition must be large enough that 1-sector clusters land above the
# FAT32 minimum (32 MiB falls short and silently downgrades).
fat_img=\"\$work_dir/eeprom-bootfs.img\"
fat_size_mib=256
rm -f \"\$fat_img\"
truncate -s \"\${fat_size_mib}M\" \"\$fat_img\"
# -F 32 forces FAT32, -c 1 sets sectors-per-cluster to 1 (matches upstream
# 'mkfs.fat -F 32 -s 1') so cluster counts are well above the FAT32 floor.
mformat -i \"\$fat_img\" -F -c 1 -v EEPROMRCV ::
for f in recovery.bin pieeprom.bin pieeprom.sig vl805.bin vl805.sig; do
	if [ -f \"\$work_dir/staging/\$f\" ]; then
		mcopy -i \"\$fat_img\" \"\$work_dir/staging/\$f\" \"::\$f\"
	fi
done

# 6. Wrap that filesystem in a single-partition MBR SD image.
sd_img=\"\$work_dir/eeprom-prep-sd.img\"
sector_size=512
part_start_sectors=2048
tail_sectors=2048
fat_bytes=\$(stat -c %s \"\$fat_img\")
part_sectors=\$(( (fat_bytes + sector_size - 1) / sector_size ))
total_sectors=\$(( part_start_sectors + part_sectors + tail_sectors ))
part_offset_bytes=\$(( part_start_sectors * sector_size ))

rm -f \"\$sd_img\"
truncate -s \$(( total_sectors * sector_size )) \"\$sd_img\"
printf 'label: dos\nunit: sectors\n\nstart=%s, size=%s, type=c, bootable\n' \\
	\"\$part_start_sectors\" \"\$part_sectors\" | sfdisk \"\$sd_img\" >/dev/null
dd if=\"\$fat_img\" of=\"\$sd_img\" bs=\"\$sector_size\" seek=\"\$part_start_sectors\" \\
	conv=notrunc status=none

printf 'pieeprom_src=%s\n' \"\$(basename \"\$pieeprom_src\")\"
printf 'config:\n'
sed 's/^/  /' \"\$config_file\"
printf 'image: %s\n' \"\$sd_img\"
printf 'fat_offset_bytes: %s\n' \"\$part_offset_bytes\"
fdisk -l \"\$sd_img\" || true
mdir -i \"\$sd_img@@\$part_offset_bytes\" ::
"

# 7. Pull the resulting image and a meta sidecar back to the host.
sd_img_vm="$work_dir_vm/eeprom-prep-sd.img"
out_img="$out_dir_host/eeprom-prep-sd.img"
out_meta="$out_dir_host/eeprom-prep-sd.img.meta.txt"

limactl copy -y "$vm:$sd_img_vm" "$out_img"

size=$(stat -f %z "$out_img")
sha=$(shasum -a 256 "$out_img" | awk '{print $1}')

{
	printf 'image: eeprom-prep-sd.img\n'
	printf 'size_bytes: %s\n' "$size"
	printf 'sha256: %s\n' "$sha"
	printf 'boot_order: %s\n' "$boot_order"
	printf 'tftp_prefix: %s\n' "$tftp_prefix"
	printf 'boot_uart: %s\n' "$boot_uart"
	printf 'generated_at_utc: %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$out_meta"

printf '\n=== exported ===\n'
printf 'image: %s\n' "$out_img"
printf 'meta:  %s\n' "$out_meta"
printf 'size:  %s bytes\n' "$size"
printf 'sha256: %s\n' "$sha"
