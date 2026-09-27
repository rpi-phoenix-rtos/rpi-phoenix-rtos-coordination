#!/usr/bin/env bash
#
# gpu-lane-compare.sh — are two builds of the same static archive or ELF the same code?
#
# Used to prove that a new-GPU-lane framework port (sources/phoenix-rtos-ports/<port>/)
# builds what the coordination repo's tools/gpu-lane/<x>/build.sh builds, when the bytes
# cannot match: the two builds compile from different directories, so DWARF, and any
# __FILE__ / assert() string, name different paths.
#
#   scripts/gpu-lane-compare.sh <a> <b>
#
# Verdicts, strongest first:
#   IDENTICAL      sha256 equal
#   CODE-IDENTICAL after `strip --strip-debug` the disassembly (objdump -dr, addresses and
#                  file names dropped) is equal, member for member for an archive; the
#                  report adds how many .rodata / string differences remain and lists them
#                  (normally paths only)
#   CODE-EQUIVALENT (archives) the same, with the addends of relocations into merged string
#                  sections (.rodata.*.str1.N+off) masked: a longer __FILE__ path moves the
#                  literals after it; the strings themselves are then listed
#                  (linked ELFs) the same, after also masking every hex address,
#                  immediate and symbol offset: a string of another length moves .rodata and
#                  so every literal address; the instruction stream and every symbol a
#                  branch or load names must still match line for line
#   DIFFERENT      anything else (the first differing lines are printed); exit status 1
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

a="${1:?usage: $0 <a> <b>}"
b="${2:?usage: $0 <a> <b>}"
tc="${PHOENIX_AARCH64_TOOLCHAIN:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/.toolchain/aarch64-phoenix/bin}/aarch64-phoenix-"
tmp="$(mktemp -d /tmp/gpu-lane-compare.XXXXXX)"
trap 'rm -rf "${tmp}"' EXIT

if cmp -s "${a}" "${b}"; then
	echo "IDENTICAL $(sha256sum "${a}" | cut -c1-16)  $(basename "${a}")"
	exit 0
fi

# Disassembly with relocations, without addresses, raw bytes or the file-name header, one
# section per paragraph; string literals are compared separately.
disasm() {
	"${tc}objdump" -dr --no-show-raw-insn --no-addresses "$1" 2>/dev/null \
		| grep -vE '^(In archive |.*:[[:space:]]+file format )' || true
}
strs() { "${tc}strings" -a "$1" | LC_ALL=C sort; }

"${tc}strip" --strip-debug -o "${tmp}/a" "${a}"
"${tc}strip" --strip-debug -o "${tmp}/b" "${b}"
if cmp -s "${tmp}/a" "${tmp}/b"; then
	echo "CODE-IDENTICAL (identical after strip --strip-debug)  $(basename "${a}")"
	exit 0
fi
if [ "$(head -c 7 "${tmp}/a")" = '!<arch>' ]; then
	ma="$("${tc}ar" t "${tmp}/a")"
	mb="$("${tc}ar" t "${tmp}/b")"
	[ "${ma}" = "${mb}" ] || { echo "DIFFERENT: archive member lists differ  $(basename "${a}")"; { diff <(echo "${ma}") <(echo "${mb}") || true; } | head -10 || true; exit 1; }
fi
disasm "${tmp}/a" > "${tmp}/da"
disasm "${tmp}/b" > "${tmp}/db"
verdict=CODE-IDENTICAL
if ! cmp -s "${tmp}/da" "${tmp}/db" && [ "$(head -c 7 "${tmp}/a")" = '!<arch>' ]; then
	# objects: a string literal of another length (a __FILE__ path) moves the ones after it in
	# the same merged string section, so relocation addends into .str sections may differ
	for x in a b; do
		sed -E 's/(\.str1\.[0-9]+)\+0x[0-9a-f]+/\1+S/g' "${tmp}/d${x}" > "${tmp}/n${x}"
	done
	cmp -s "${tmp}/na" "${tmp}/nb" && { verdict="CODE-EQUIVALENT (string-literal offsets masked)"; cp "${tmp}/na" "${tmp}/da"; cp "${tmp}/nb" "${tmp}/db"; }
fi
if ! cmp -s "${tmp}/da" "${tmp}/db" && [ "$(head -c 7 "${tmp}/a")" != '!<arch>' ]; then
	for x in a b; do
		sed -E -e 's/#-?0x[0-9a-f]+/#I/g' -e 's/#-?[0-9]+/#I/g' -e 's/\b[0-9a-f]{4,}\b/H/g' -e 's/\+0x[0-9a-f]+>/+O>/g' \
			"${tmp}/d${x}" > "${tmp}/n${x}"
	done
	cmp -s "${tmp}/na" "${tmp}/nb" && { verdict=CODE-EQUIVALENT; cp "${tmp}/na" "${tmp}/da"; cp "${tmp}/nb" "${tmp}/db"; }
fi
if ! cmp -s "${tmp}/da" "${tmp}/db"; then
	echo "DIFFERENT: disassembly differs ($(diff "${tmp}/da" "${tmp}/db" | grep -c '^[<>]') lines)  $(basename "${a}")"
	{ diff "${tmp}/da" "${tmp}/db" || true; } | head -20 || true
	exit 1
fi
strs "${tmp}/a" > "${tmp}/sa"
strs "${tmp}/b" > "${tmp}/sb"
nd="$(LC_ALL=C comm --nocheck-order -3 "${tmp}/sa" "${tmp}/sb" | grep -c . || true)"
echo "${verdict} (disassembly + relocations equal; ${nd} differing string line(s))  $(basename "${a}")"
if [ "${nd}" != 0 ]; then
	{ LC_ALL=C comm --nocheck-order -3 "${tmp}/sa" "${tmp}/sb" | head -"${GPU_LANE_COMPARE_SHOW:-8}" | sed 's/^/    /'; } || true
fi
