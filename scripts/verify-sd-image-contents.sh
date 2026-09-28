#!/usr/bin/env bash
# verify-sd-image-contents.sh <image.img> — gate a built SD image's CONTENTS
# before anyone flashes it.
#
# Distinct from scripts/verify-rpi4b-sdimg.sh, which checks image INTEGRITY
# (sha256 + size against the .meta.txt, i.e. "did it copy correctly"). This one
# asks "is the right CODE inside", which integrity cannot tell you: a perfectly
# intact image can be built from a commit that was reverted an hour later.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
#
# Answers three questions that have each already shipped a bad artifact:
#   1. is every binary and every game's DATA actually in the rootfs?
#   2. does it contain the fixes it is supposed to (positive markers)?
#   3. does it contain something we deliberately reverted (negative markers)?
#
# (3) is the one that matters most: the 2026-09-03 clean image was green on (1)
# and (2) and still shipped a yQuake2 that wedged the GPU on nearly every frame,
# because it cloned a fork commit that was reverted an hour later.
set -uo pipefail

# --expect showcase (default) requires the games, X11 and their data; --expect base
# is for an image built WITHOUT --with-showcase, where those are legitimately
# absent. The caller must not guess this from the image itself -- that would make
# a staging failure ("the tree had Xphoenix, the image does not") downgrade itself
# to a SKIP. rebuild-rpi4b-fast.sh derives it from the staged _fs tree, which is an
# independent source, so a disagreement between tree and image still FAILS here.
expect=showcase
# The GPU programs to expect follow the image's GPU stack (GPU migration P1):
# RPI4B_GPU_LEGACY=1 = the legacy A/B image (in-process winsys, Xphoenix), else the
# default stack (the servers + the *_drm programs). rebuild-rpi4b-fast.sh exports it.
case "$(printf '%s' "${RPI4B_GPU_LEGACY:-}" | tr '[:upper:]' '[:lower:]')" in
	''|0|n|no|false) gpu_legacy=0 ;;
	*) gpu_legacy=1 ;;
esac
args=()
while [ $# -gt 0 ]; do
	case "$1" in
		--expect)
			case "${2:-}" in
				base|showcase) expect="$2"; shift 2 ;;
				*) echo "usage: --expect base|showcase" >&2; exit 2 ;;
			esac
			;;
		*) args+=("$1"); shift ;;
	esac
done
set -- ${args+"${args[@]}"}

IMG="${1:?usage: verify-sd-image-contents.sh [--expect base|showcase] <image.img>}"
[ -f "$IMG" ] || { echo "no such image: $IMG" >&2; exit 2; }
command -v debugfs >/dev/null || { echo "need e2fsprogs (debugfs)" >&2; exit 2; }

# ⚠ Until 2026-09-17 a SKIP left rc untouched, so a host missing mtools or
# e2fsck — or an image whose loader.disk could not be read — still ended in
# "RESULT: image PASSES — safe to flash" while the FAT boot partition, the
# filesystem check, or the SD-vs-netboot boot target had never been looked at.
# A check that did not run is not a check that passed.
skipped=0
skip() {
	printf '  SKIP %s\n' "$1"
	skipped=$((skipped + 1))
}

start=$(partx -g -o START -n 2 "$IMG" 2>/dev/null | tr -d ' ')
[ -n "$start" ] || { echo "cannot read partition 2 start" >&2; exit 2; }
E2="$IMG?offset=$((start * 512))"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
rc=0

dump() { rm -f "$TMP/x"; debugfs -R "dump /$1 $TMP/x" "$E2" >/dev/null 2>&1; [ -s "$TMP/x" ]; }

# Count occurrences of a string in the last dumped file.
#
# Deliberately grep -c, never grep -q: under `set -o pipefail` a `grep -q` exits
# on its FIRST match, `strings` then dies of SIGPIPE (141), and pipefail makes the
# pipeline fail -- so a successful match reads as "absent". That inverted BOTH
# marker checks in the first version of this script and made a bad image look
# clean. grep -c drains the pipe, so the pipeline exits 0.
marker_count() { strings "$TMP/x" | grep -c -- "$1" || true; }

echo "== required paths (expect: ${expect}, GPU stack: $([ "${gpu_legacy}" = 1 ] && echo legacy || echo default)) =="
# bin/wmsetbg: wmaker EXECS it to paint the root window (src/misc.c:953). Without
#   it the GPU desktop is a black screen with a live cursor, which was reported as
#   "no wmaker running" on 2026-09-04 when in fact the session was healthy.
# bin/fbprobe: the framebuffer channel-order probe. Cheap to ship, and the one
#   tool that settles an RGB-vs-BGR argument by looking at the screen.
required_paths=(bin/psh)
if [ "${expect}" = showcase ]; then
	required_paths+=(bin/python3 bin/bash bin/nano bin/mc
	                 usr/share/quake/id1/pak0.pak
	                 usr/share/quake2/baseq2/pak0.pak
	                 usr/share/quake3/demoq3/pak0.pk3 usr/share/quake3/demoq3/pak1.pk3
	                 usr/share/quake3/demoq3/q3key
	                 bin/wmsetbg)
	if [ "${gpu_legacy}" = 1 ]; then
		# TODO(TD-24): the legacy GPU stack.
		required_paths+=(usr/bin/quakespasm usr/bin/yquake2 usr/bin/quake3e usr/bin/vkquake
		                 usr/bin/supertuxkart usr/bin/Xphoenix bin/fbprobe)
	else
		required_paths+=(sbin/rpi4-v3d-async sbin/rpi4-kms bin/shmsrv bin/Xorg-drm
		                 usr/bin/quakespasm-drm usr/bin/yquake2-drm usr/bin/quake3e-drm
		                 usr/bin/vkquake-drm usr/bin/supertuxkart-drm
		                 usr/bin/quakespasm usr/bin/quake2 usr/bin/quake3 usr/bin/vkquake
		                 bin/stk bin/startx bin/xfce-session)
	fi
fi
for p in "${required_paths[@]}"; do
	if dump "$p"; then printf '  OK   %-40s %s\n' "$p" "$(stat -c%s "$TMP/x")"
	else printf '  MISS %s\n' "$p"; rc=1; fi
done

echo "== build provenance =="
# /etc/build-versions names the exact commit of every Phoenix repo that went into
# this image (coordination scripts/gen-build-versions.sh -> printed at boot by
# rpi4-sysinfo). An image without it can still boot, but every log it produces is
# ambiguous about what it was built from -- which is the problem the owner asked
# us to remove on 2026-09-05. Require a plausible list, not merely the file.
if dump etc/build-versions; then
	bv_repos="$(grep -avc '^#' "$TMP/x" || true)"
	if [ "${bv_repos:-0}" -ge 10 ]; then
		printf '  OK   %-40s %s repos\n' "etc/build-versions" "${bv_repos}"
		# A build from a modified tree is exactly where a bare sha misleads, so
		# say it out loud rather than letting it pass silently.
		bv_dirty="$(grep -ac '+dirty' "$TMP/x" || true)"
		[ "${bv_dirty:-0}" -eq 0 ] ||
			printf '  WARN %s repo(s) were DIRTY at build time (see etc/build-versions)\n' "${bv_dirty}"
	else
		printf '  FAIL etc/build-versions lists only %s repos (expected >= 10)\n' "${bv_repos}"; rc=1
	fi
else
	echo "  MISS etc/build-versions — the image cannot say which commits built it"; rc=1
fi

if [ "${expect}" != showcase ]; then
	echo "== showcase markers =="
	echo "  N/A  --expect base: no games/X in this image, so their markers do not apply"
fi
if [ "${expect}" = showcase ]; then
echo "== positive markers (fixes that must be present) =="
if [ "${gpu_legacy}" = 1 ]; then
# The V3D submit mutex: its failure fprintf string is unique to the fixed driver.
if dump usr/bin/vkquake && [ "$(marker_count 'submits UNSERIALIZED')" -gt 0 ]; then
	echo "  OK   v3d submit mutex present in vkquake"
else
	echo "  MISS v3d submit mutex marker absent from vkquake"; rc=1
fi
else
# Each engine of the default stack prints its own banner; an old-stack engine staged
# under the -drm name (or a failed relink) has none.
for eng_spec in usr/bin/quakespasm-drm:quakespasm-drm usr/bin/yquake2-drm:quake2-drm \
		usr/bin/quake3e-drm:quake3-drm usr/bin/vkquake-drm:vkquake-drm \
		usr/bin/supertuxkart-drm:stk-drm; do
	if dump "${eng_spec%%:*}" && [ "$(marker_count "${eng_spec#*:}: new GPU lane")" -gt 0 ]; then
		echo "  OK   ${eng_spec%%:*} is the new-stack engine (banner '${eng_spec#*:}: new GPU lane')"
	else
		echo "  MISS ${eng_spec%%:*}: no '${eng_spec#*:}: new GPU lane' banner"; rc=1
	fi
done
fi

# The shipped game CONFIGS, not just their presence. check-rootfs-complete.sh
# already asserts id1/autoexec.cfg EXISTS, and that is not enough: on 2026-09-09
# an image passed every gate while shipping an autoexec.cfg that had the vid_*
# block but had lost the on-screen fps readout, so the reel's headline figures
# were absent and nothing complained. The cause is worth knowing -- game data is
# staged by scripts/stage-game-data.sh, which the tree states plainly that
# "local builds do NOT run" (only the Dockerfile does), so an edit to that script
# does nothing for a local cut until it is run by hand. Assert the CONTENT.
for cfg_spec in \
	"usr/share/quake/id1/autoexec.cfg|scr_conscale|Quake 1/vkQuake fps readout" \
	"usr/share/quake3/demoq3/autoexec.cfg|cg_cameraOrbit|Quake III showcase camera"; do
	cfg_path=${cfg_spec%%|*}
	cfg_rest=${cfg_spec#*|}
	cfg_marker=${cfg_rest%%|*}
	cfg_what=${cfg_rest#*|}
	if dump "$cfg_path" && [ "$(marker_count "$cfg_marker")" -gt 0 ]; then
		echo "  OK   $cfg_what present ($cfg_path)"
	else
		echo "  MISS $cfg_what absent from $cfg_path — run scripts/stage-game-data.sh"; rc=1
	fi
done

echo "== negative markers (reverted code that must be ABSENT) =="
# gl3_discardfb: the pre-swap Z/S discard, reverted 2026-09-03 (fork d5413235).
# The cvar NAME string only exists in the binary if the code does.
# The default stack's yquake2-drm is a relink of the same engine objects.
q2_engine=usr/bin/yquake2
[ "${gpu_legacy}" = 1 ] || q2_engine=usr/bin/yquake2-drm
if dump "${q2_engine}"; then
	if [ "$(marker_count gl3_discardfb)" -gt 0 ]; then
		echo "  FAIL ${q2_engine} still contains gl3_discardfb (the reverted GPU-wedging discard)"; rc=1
	else
		echo "  OK   ${q2_engine} free of gl3_discardfb"
	fi
else
	echo "  MISS ${q2_engine} not in image"; rc=1
fi

fi  # expect = showcase

# The FAT section runs for EVERY variant: it is the boot path, not the payload.
echo "== FAT boot partition (the path no gate could reach) =="
# Everything above inspects the ext2 rootfs. Nothing did the FIRST thing the
# VideoCore firmware does: read the FAT partition. That matters more here than it
# looks, because SD boot is the ONE path that cannot be tested on this bench --
# verified 2026-09-09 that there is no card in the host reader (/dev/sda absent)
# and none in the Pi ("sdcard: no card present in slot 0"), so the image is flashed
# and booted for the first time by the owner. verify-rpi4b-sdimg.sh only checks
# size, sha256 and that `mdir` can list the root; a missing or renamed boot file
# would produce a black screen and no gate would have said a word.
#
# So: assert the required files exist, and cross-check that every file config.txt
# NAMES is actually present. That catches the realistic failure -- a rename or a
# staging omission that leaves config.txt pointing at nothing.
fat_off="$(python3 - "$IMG" <<'PYEOF'
import sys, struct
with open(sys.argv[1], 'rb') as f:
    mbr = f.read(512)
lba = struct.unpack('<I', mbr[446 + 8:446 + 12])[0]
print(lba * 512 if lba else 0)
PYEOF
)"
if [ -z "$fat_off" ] || [ "$fat_off" = "0" ]; then
	echo "  FAIL cannot locate the FAT partition from the MBR"; rc=1
elif ! command -v mdir >/dev/null 2>&1; then
	skip "mtools not installed — the ENTIRE FAT boot partition went unchecked (kernel8.img, config.txt, arm_64bit=1)"
else
	fat_ls="$(mdir -i "${IMG}@@${fat_off}" :: 2>/dev/null)"
	# mdir prints a valid 8.3 name as two space-separated COLUMNS with no dot
	# ("config   txt"), and only shows a literal long name when the 8.3 form had
	# to be truncated ("LOADER~1 DIS ... loader.disk"). Matching the literal
	# filename alone therefore misses exactly the short-named files -- which is how
	# the first version of this check reported config.txt/kernel8.img/start4.elf
	# missing from an image that plainly had them. Accept either form.
	fat_has() {
		local f="$1" base ext
		base="${f%.*}"; ext="${f##*.}"
		printf '%s\n' "$fat_ls" | grep -qiE "(^|[[:space:]])${f}([[:space:]]|$)" && return 0
		printf '%s\n' "$fat_ls" | grep -qiE "^${base}[[:space:]]+${ext}([[:space:]]|$)" && return 0
		return 1
	}
	for f in bcm2711-rpi-4-b.dtb config.txt kernel8.img loader.disk \
	         phoenix-armstub8-rpi4.bin start4.elf; do
		if fat_has "$f"; then
			echo "  OK   FAT: $f"
		else
			echo "  MISS FAT: $f — the firmware needs this to boot"; rc=1
		fi
	done

	cfg="$(mtype -i "${IMG}@@${fat_off}" ::config.txt 2>/dev/null)"
	if [ -z "$cfg" ]; then
		echo "  MISS FAT: config.txt unreadable"; rc=1
	else
		# A 32-bit boot would load the kernel and fail silently.
		if printf '%s\n' "$cfg" | grep -qE '^[[:space:]]*arm_64bit=1'; then
			echo "  OK   config.txt: arm_64bit=1"
		else
			echo "  FAIL config.txt: arm_64bit=1 missing — would boot 32-bit"; rc=1
		fi
		# Cross-check every file config.txt references against the FAT listing.
		refs="$(printf '%s\n' "$cfg" \
			| sed -nE 's/^[[:space:]]*(armstub|kernel|device_tree)=([^[:space:]#]+).*/\2/p;
			           s/^[[:space:]]*initramfs[[:space:]]+([^[:space:]#]+).*/\1/p')"
		for r in $refs; do
			if fat_has "$r"; then
				echo "  OK   config.txt -> $r present"
			else
				echo "  FAIL config.txt names $r but it is NOT in the FAT partition"; rc=1
			fi
		done
	fi
fi

# --- (4) is the filesystem sound, and does it boot the root it carries? --------
#
# Everything above reads FILES. None of it would notice an ext2 that fsck rejects,
# or a loader.disk that boots the WRONG ROOT -- and the second is a documented
# footgun on this project: a `--variant sd` build overwrites the netboot TFTP
# loader, and the mirror-image mistake (an SD image carrying a netboot loader that
# looks for `nfs;/`) would leave the first person to flash it staring at a hang.
# Nobody can test that here: this host has no card reader, so the owner boots it
# first. These three checks are the closest stand-in.
echo
echo "== filesystem + boot target =="

if command -v e2fsck >/dev/null 2>&1; then
	# `?offset=` is e2fsprogs' own syntax, so this needs no loopback and no root.
	if e2fsck -fn "$E2" >"$TMP/fsck" 2>&1; then
		echo "  OK   ext2 rootfs consistent ($(grep -oE '[0-9]+/[0-9]+ files' "$TMP/fsck" | tail -1))"
	else
		echo "  FAIL ext2 rootfs is NOT consistent:"
		sed 's/^/         /' "$TMP/fsck" | tail -20
		rc=1
	fi
else
	skip "e2fsck not installed (e2fsprogs) — the ext2 root was not checked for consistency"
fi

if [ -n "${fat_off:-}" ] && [ "${fat_off:-0}" != "0" ] && 		mtype -i "${IMG}@@${fat_off}" ::loader.disk >"$TMP/loader" 2>/dev/null && [ -s "$TMP/loader" ]; then
	ldr_sd=$(strings "$TMP/loader" | grep -c 'mmcblk0p2:ext2' || true)
	ldr_nfs=$(strings "$TMP/loader" | grep -c 'nfs;/' || true)
	if [ "$ldr_sd" -gt 0 ] && [ "$ldr_nfs" -eq 0 ]; then
		echo "  OK   loader.disk boots /dev/mmcblk0p2:ext2 (and carries no nfs root)"
	else
		echo "  FAIL loader.disk boot target wrong: mmcblk0p2:ext2=$ldr_sd nfs=$ldr_nfs"
		echo "         an SD image must boot the SD root; a netboot loader here hangs at first boot"
		rc=1
	fi

	# A demo image whose plo list omits usb has NO KEYBOARD, which no file-presence
	# check can see -- the binaries are all there, they are just never started.
	# grep -c, never grep -q -- see marker_count() above. Under `set -o pipefail`
	# a `grep -q` exits on its first match, `strings` dies of SIGPIPE, and the
	# pipeline fails, so a MATCH reads as absent. This check reported "no USB
	# keyboard" on an image whose plo list plainly starts usb, on the first run.
	for prog in usb lwip; do
		if [ "$(strings "$TMP/loader" | grep -cE "^app ram0 -x ${prog}[;[:space:]]" || true)" -gt 0 ]; then
			echo "  OK   plo starts ${prog}"
		else
			echo "  FAIL plo does not start ${prog} — $([ "$prog" = usb ] && echo 'no USB keyboard' || echo 'no network') on this image"
			rc=1
		fi
	done
else
	skip "loader.disk not readable — the SD-vs-netboot boot target and the plo usb/lwip checks were ALL skipped"
fi

echo
if [ "$rc" -ne 0 ]; then
	echo "RESULT: image FAILS — do not flash"
elif [ "$skipped" -gt 0 ]; then
	echo "RESULT: image UNVERIFIED — every check that RAN passed, but $skipped were skipped."
	echo "        Install the missing tool (mtools / e2fsprogs) and re-run before flashing."
	rc=3
else
	echo "RESULT: image PASSES — safe to flash"
fi
exit "$rc"
