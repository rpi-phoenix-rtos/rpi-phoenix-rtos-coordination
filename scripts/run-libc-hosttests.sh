#!/usr/bin/env bash
#
# run-libc-hosttests.sh — run every libphoenix host harness in one go.
#
# These compile libphoenix's REAL source natively and diff it against the host
# glibc. No Pi cycle, no cross-build, a few seconds total. Run this BEFORE a Pi
# cycle for any libphoenix change: the bench is exclusive and a cycle costs
# 5-10 minutes, so the Pi should be the confirmation step, not the discovery
# loop. Ten defect clusters were found this way on 2026-09-24/25, three of them
# before any hardware was involved.
#
# Each harness proves itself before trusting a clean run:
#   * a canary that deliberately mis-compares, so "0 differences" cannot come
#     from a comparison path that never fires;
#   * libstring additionally has `make canfail`, which rebuilds against a copy
#     of string.c with the real 2026-08 strncmp over-read restored and requires
#     the guard page to catch it.
#
# Divergences that are understood and deliberately NOT changed are classified
# inside each tool (printed as *KNOWN*), so a non-zero exit still means
# something new. See each README for the reasoning.
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -u

cd "$(dirname "$0")/.." || exit 1
root=$PWD

tools="libstring-hosttest libwchar-hosttest libnum-hosttest libfmt-hosttest libtime-hosttest libscanf-hosttest"

fail=0
declare -a results

for t in $tools; do
	dir="$root/tools/$t"
	if [ ! -d "$dir" ]; then
		results+=("$t SKIP (not present)")
		continue
	fi

	printf '=== %s ===\n' "$t"
	out=$(cd "$dir" && make --no-print-directory clean >/dev/null 2>&1; cd "$dir" && make --no-print-directory run 2>&1)
	rc=$?
	printf '%s\n' "$out" | grep -E 'HOST|SFAULT|canary' | tail -4

	if [ $rc -ne 0 ]; then
		fail=1
		results+=("$t FAIL (rc=$rc)")
	else
		results+=("$t ok")
	fi
	printf '\n'
done

# libstring can prove it still catches a real historical defect
if [ -d "$root/tools/libstring-hosttest" ]; then
	printf '=== libstring-hosttest canfail (must catch the 2026-08 strncmp over-read) ===\n'
	if (cd "$root/tools/libstring-hosttest" && make --no-print-directory canfail >/dev/null 2>&1); then
		results+=("libstring canfail ok")
		printf 'canfail ok -- the guard page still catches the known defect\n\n'
	else
		fail=1
		results+=("libstring canfail FAILED -- harness cannot detect a known bug")
		printf 'canfail FAILED\n\n'
	fi
fi

printf '=== summary ===\n'
for r in "${results[@]}"; do
	printf '  %s\n' "$r"
done

if [ $fail -ne 0 ]; then
	printf '\nLIBC-HOSTTESTS: FAIL\n'
	exit 1
fi

printf '\nLIBC-HOSTTESTS: all clean\n'
exit 0
