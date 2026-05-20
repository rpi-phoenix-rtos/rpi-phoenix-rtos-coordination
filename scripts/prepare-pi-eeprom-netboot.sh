#!/usr/bin/env bash
#
# One-time Pi 4 EEPROM-update SD image generator.
#
# Produces a small FAT32 SD image containing recovery.bin + pieeprom.bin +
# pieeprom.sig, configured to enable network boot in the Pi 4 EEPROM. After
# you flash this image to a microSD card and boot the Pi 4 once with that
# card inserted, the green LED will blink rapidly (success) — power off,
# remove the card, and the Pi will then attempt Network → SD on every
# subsequent boot, saving ~30 s per netboot cycle (no SD/USB-MSD probe).
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
# Host OS dispatch:
#   Darwin  : shells into the phoenix-dev Lima VM, runs the staging there,
#             then `limactl copy`s the result back.
#   Linux   : runs everything locally (this dev box). Requires git, sfdisk,
#             mtools (mformat, mcopy), dd, sha256sum, python3 — all already
#             present in the standard bootstrap.
#

set -euo pipefail

repo="${PHOENIX_RPI_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
host_os="$(uname -s)"

if [ -f "$repo/.env.local" ]; then
	set -a
	# shellcheck disable=SC1091
	. "$repo/.env.local"
	set +a
fi

boot_order="${PI_EEPROM_BOOT_ORDER:-0xf12}"
tftp_prefix="${PI_EEPROM_TFTP_PREFIX:-2}"
boot_uart="${PI_EEPROM_BOOT_UART:-0}"

if [ "$host_os" = "Darwin" ]; then
	vm="${PHOENIX_VM:-phoenix-dev}"
	eeprom_dir_vm="${PHOENIX_EEPROM_DIR:-/home/witoldbolt.guest/external/rpi-eeprom}"
	work_dir_vm="${PHOENIX_EEPROM_WORK:-/home/witoldbolt.guest/phoenix-buildroots/eeprom-netboot}"
	out_dir_host="${RPI4B_EEPROM_OUT_DIR:-/Users/witoldbolt/phoenix-rpi/artifacts/eeprom-netboot}"
	mkdir -p "$out_dir_host"
else
	eeprom_dir="${PHOENIX_EEPROM_DIR:-$repo/external/rpi-eeprom}"
	work_dir="${PHOENIX_EEPROM_WORK:-$repo/.buildroot/eeprom-netboot}"
	out_dir_host="${RPI4B_EEPROM_OUT_DIR:-$repo/artifacts/eeprom-netboot}"
	mkdir -p "$out_dir_host" "$(dirname "$eeprom_dir")"
fi

# The body that does the actual work. Identical between hosts; only the
# location of the working directories changes.
run_eeprom_staging() {
	local _eeprom_dir="$1"
	local _work_dir="$2"

	# 1. Fetch or refresh rpi-eeprom upstream (shallow).
	if [ ! -d "$_eeprom_dir/.git" ]; then
		mkdir -p "$(dirname "$_eeprom_dir")"
		git clone --depth=1 https://github.com/raspberrypi/rpi-eeprom.git "$_eeprom_dir"
	else
		git -C "$_eeprom_dir" fetch --depth=1 origin master
		git -C "$_eeprom_dir" reset --hard origin/master
	fi

	local stable_dir="$_eeprom_dir/firmware-2711/stable"
	if [ ! -d "$stable_dir" ]; then
		# Newer repo layout uses firmware-2711/latest or firmware/stable.
		for alt in firmware-2711/latest firmware/stable firmware/latest; do
			if [ -d "$_eeprom_dir/$alt" ]; then
				stable_dir="$_eeprom_dir/$alt"
				break
			fi
		done
	fi
	if [ ! -d "$stable_dir" ]; then
		printf 'cannot find Pi 4 EEPROM firmware directory under %s\n' "$_eeprom_dir" >&2
		exit 1
	fi

	local pieeprom_src
	pieeprom_src=$(ls "$stable_dir"/pieeprom-*.bin 2>/dev/null | sort | tail -n1)
	local recovery_src="$stable_dir/recovery.bin"
	if [ -z "$pieeprom_src" ] || [ ! -f "$recovery_src" ]; then
		printf 'missing pieeprom-*.bin or recovery.bin in %s\n' "$stable_dir" >&2
		exit 1
	fi

	# 2. Build a fresh staging dir with the 3 files the EEPROM updater needs.
	rm -rf "$_work_dir"
	mkdir -p "$_work_dir/staging"

	cp -f "$recovery_src" "$_work_dir/staging/recovery.bin"

	# 3. Embed our boot-order config into pieeprom.bin via rpi-eeprom-config.
	local config_file="$_work_dir/bootconf.txt"
	{
		printf 'BOOT_UART=%s\n' "$boot_uart"
		printf 'WAKE_ON_GPIO=1\n'
		printf 'POWER_OFF_ON_HALT=0\n'
		printf 'BOOT_ORDER=%s\n' "$boot_order"
		printf 'TFTP_PREFIX=%s\n' "$tftp_prefix"
		printf 'NET_INSTALL_AT_POWER_ON=0\n'
		printf 'ENABLE_SELF_UPDATE=0\n'
	} > "$config_file"

	if [ ! -x "$_eeprom_dir/rpi-eeprom-config" ]; then
		printf 'rpi-eeprom-config helper not found in %s\n' "$_eeprom_dir" >&2
		exit 1
	fi

	# Note: the file is named pieeprom.bin (NOT .upd). recovery.bin uses
	# pieeprom.bin and unconditionally reflashes the EEPROM. The .upd name
	# would instead trigger the bootloader's SELF-UPDATE path, which compares
	# embedded timestamps and may skip with 'SELF-UPDATE ... new 0 skip'.
	"$_eeprom_dir/rpi-eeprom-config" \
		--config "$config_file" \
		--out "$_work_dir/staging/pieeprom.bin" \
		"$pieeprom_src"

	# 4. Generate pieeprom.sig via rpi-eeprom-digest (sha256 + 'ts: <epoch>').
	"$_eeprom_dir/rpi-eeprom-digest" \
		-i "$_work_dir/staging/pieeprom.bin" \
		-o "$_work_dir/staging/pieeprom.sig"

	# 4b. If the chosen firmware tree has a vl805 blob (older Pi 4 revs with
	# separate USB controller EEPROM), bundle it too. New Pi 4 boards have
	# vl805 baked into pieeprom.bin and don't need this; recovery.bin will
	# silently skip if the file is absent.
	local vl805_src
	vl805_src=$(ls "$stable_dir"/vl805-*.bin 2>/dev/null | sort | tail -n1 || true)
	if [ -n "$vl805_src" ] && [ -f "$vl805_src" ]; then
		cp -f "$vl805_src" "$_work_dir/staging/vl805.bin"
		sha256sum "$_work_dir/staging/vl805.bin" | awk '{print $1}' > "$_work_dir/staging/vl805.sig"
	fi

	# 5. Build a 256 MiB FAT32 filesystem image with the files at the root.
	# Size and cluster geometry match the upstream make-recovery-images script:
	# the Pi 4 EEPROM bootloader requires a true FAT32 (>= 65525 clusters), so
	# the partition must be large enough that 1-sector clusters land above the
	# FAT32 minimum (32 MiB falls short and silently downgrades).
	local fat_img="$_work_dir/eeprom-bootfs.img"
	local fat_size_mib=256
	rm -f "$fat_img"
	truncate -s "${fat_size_mib}M" "$fat_img"
	# -F 32 forces FAT32, -c 1 sets sectors-per-cluster to 1 (matches upstream
	# 'mkfs.fat -F 32 -s 1') so cluster counts are well above the FAT32 floor.
	mformat -i "$fat_img" -F -c 1 -v EEPROMRCV ::
	local f
	for f in recovery.bin pieeprom.bin pieeprom.sig vl805.bin vl805.sig; do
		if [ -f "$_work_dir/staging/$f" ]; then
			mcopy -i "$fat_img" "$_work_dir/staging/$f" "::$f"
		fi
	done

	# 6. Wrap that filesystem in a single-partition MBR SD image.
	local sd_img="$_work_dir/eeprom-prep-sd.img"
	local sector_size=512
	local part_start_sectors=2048
	local tail_sectors=2048
	local fat_bytes
	fat_bytes=$(stat -c %s "$fat_img")
	local part_sectors=$(( (fat_bytes + sector_size - 1) / sector_size ))
	local total_sectors=$(( part_start_sectors + part_sectors + tail_sectors ))
	local part_offset_bytes=$(( part_start_sectors * sector_size ))

	rm -f "$sd_img"
	truncate -s $(( total_sectors * sector_size )) "$sd_img"
	printf 'label: dos\nunit: sectors\n\nstart=%s, size=%s, type=c, bootable\n' \
		"$part_start_sectors" "$part_sectors" | sfdisk "$sd_img" >/dev/null
	dd if="$fat_img" of="$sd_img" bs="$sector_size" seek="$part_start_sectors" \
		conv=notrunc status=none

	printf 'pieeprom_src=%s\n' "$(basename "$pieeprom_src")"
	printf 'config:\n'
	sed 's/^/  /' "$config_file"
	printf 'image: %s\n' "$sd_img"
	printf 'fat_offset_bytes: %s\n' "$part_offset_bytes"
	fdisk -l "$sd_img" || true
	mdir -i "$sd_img@@$part_offset_bytes" :: || true
}

case "$host_os" in
	Darwin)
		limactl shell -y "$vm" -- /bin/bash -lc "
set -euo pipefail
$(declare -f run_eeprom_staging)
boot_uart='$boot_uart'
boot_order='$boot_order'
tftp_prefix='$tftp_prefix'
run_eeprom_staging '$eeprom_dir_vm' '$work_dir_vm'
"
		# Pull the resulting image and a meta sidecar back to the host.
		sd_img_vm="$work_dir_vm/eeprom-prep-sd.img"
		out_img="$out_dir_host/eeprom-prep-sd.img"
		out_meta="$out_dir_host/eeprom-prep-sd.img.meta.txt"

		limactl copy -y "$vm:$sd_img_vm" "$out_img"

		size=$(stat -f %z "$out_img")
		sha=$(shasum -a 256 "$out_img" | awk '{print $1}')
		;;
	Linux)
		run_eeprom_staging "$eeprom_dir" "$work_dir"

		# Image is already on local filesystem; just copy + meta.
		out_img="$out_dir_host/eeprom-prep-sd.img"
		out_meta="$out_dir_host/eeprom-prep-sd.img.meta.txt"
		cp -f "$work_dir/eeprom-prep-sd.img" "$out_img"

		size=$(stat -c %s "$out_img")
		sha=$(sha256sum "$out_img" | awk '{print $1}')
		;;
	*)
		printf 'unsupported host OS: %s\n' "$host_os" >&2
		exit 1
		;;
esac

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
printf '\n'
printf 'NEXT STEPS:\n'
printf '  1. Flash this image to a microSD card:\n'
printf '       sudo dd if=%s of=/dev/sdX bs=4M conv=fsync status=progress\n' "$out_img"
printf '     (replace /dev/sdX with your actual SD card device — be careful!)\n'
printf '  2. Insert the SD into the Pi 4, power on.\n'
printf '  3. Wait for the green ACT LED to blink rapidly (10s+) — EEPROM updated.\n'
printf '  4. Power off, remove SD card.\n'
printf '  5. Subsequent boots will try Network first (saves ~30 s per cycle).\n'
