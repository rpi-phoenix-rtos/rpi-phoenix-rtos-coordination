#!/usr/bin/env bash
#
# restore-export-data.sh — after make-pristine-nfs-export.sh, bring back the export's DATA that no
# build produces, from the backup it kept (<export>.PREV-cruft). Data only, never an executable
# (owner rule 2026-09-03: every program on the export comes from the build):
#
#   etc/wifi.conf         the lab WiFi credentials; they live ONLY on the export, never in the
#                         rootfs overlay or in git
#   data/test-artifacts   the storage-test reference images (rpi4-storage-test skill)
#   usr/share/m10         the M10 video test clips (tools/gpu-lane/video-player/gen-clips.sh)
#
# Each path is copied only when it is absent from the new export and present in the backup;
# anything else is reported, not guessed. An ELF file is never restored: a path that is one is
# refused, and one inside a restored directory is removed from the copy and named.
#
# Usage: scripts/restore-export-data.sh [--dry-run]
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause
#
set -euo pipefail

dry=0
[ "${1:-}" = "--dry-run" ] && dry=1

EXP=$(awk '$0 ~ /fsid=0/ && $1 ~ /^\// {print $1; exit}' /etc/exports /etc/exports.d/*.exports 2>/dev/null)
[ -n "${EXP}" ] && [ -d "${EXP}" ] || { echo "FATAL: no fsid=0 export found" >&2; exit 1; }
BAK="${EXP%/}.PREV-cruft"
[ -d "${BAK}" ] || { echo "FATAL: no backup ${BAK} (run make-pristine-nfs-export.sh first)" >&2; exit 1; }

PATHS=(etc/wifi.conf data/test-artifacts usr/share/m10)
rc=0
for p in "${PATHS[@]}"; do
	if [ ! -e "${BAK}/${p}" ]; then
		echo "skip    ${p}: not in the backup"
		continue
	fi
	if [ -e "${EXP}/${p}" ]; then
		echo "keep    ${p}: already in the new export"
		continue
	fi
	if [ -f "${BAK}/${p}" ] && sudo -n head -c 4 "${BAK}/${p}" | grep -aq $'\x7fELF'; then
		echo "REFUSE  ${p}: an ELF file (executables come from the build)" >&2
		rc=1
		continue
	fi
	if [ "${dry}" = 1 ]; then
		echo "would   ${p}"
		continue
	fi
	sudo -n mkdir -p "$(dirname "${EXP}/${p}")"
	sudo -n cp -a "${BAK}/${p}" "${EXP}/${p}"
	# a directory keeps its data; any executable inside is removed from the copy, and named
	if [ -d "${EXP}/${p}" ]; then
		while IFS= read -r -d '' f; do
			if sudo -n head -c 4 "${f}" | grep -aq $'\x7fELF'; then
				sudo -n rm -f "${f}"
				echo "skip    ${f#"${EXP}/"}: ELF (executables come from the build)"
			fi
		done < <(sudo -n find "${EXP}/${p}" -type f -print0)
	fi
	echo "restore ${p}"
done
exit "${rc}"
