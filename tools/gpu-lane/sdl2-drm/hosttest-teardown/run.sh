#!/usr/bin/env bash
# Host reproduction of the SDL 2.30.12 KMSDRM teardown-order use-after-free
# (docs/misc/2026-09-28-stk-scaled-exit-fault.md). Needs a Mesa render node
# (/dev/dri/renderD128), libgbm/libEGL/libGLESv2 dev files and valgrind.
#
# For each size, runs teardown.c under valgrind in SDL 2.30.12's order ("old")
# and in patch 0010's order ("fixed") and counts the invalid accesses whose
# call site is one of the gbm_surface_release_buffer() calls in teardown.c.
# Expected: old >= 1 at every size (the bug does not depend on the mode), fixed = 0.
#
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="${here}/out"
mkdir -p "${out}"

gcc -std=gnu11 -O0 -g -Wall -Wextra -Werror -o "${out}/teardown" "${here}/teardown.c" \
	$(pkg-config --cflags --libs gbm egl glesv2)

printf 'host Mesa: %s  node: %s\n' "$(pkg-config --modversion gbm)" \
	"$(readlink -f /sys/class/drm/renderD128/device/driver 2>/dev/null | xargs -r basename)"

# Source lines of the two release calls in each teardown branch.
rel_lines="$(grep -n 'gbm_surface_release_buffer(gs, \(bo\|next_bo\));' "${here}/teardown.c" | cut -d: -f1 | tr '\n' ' ')"
fails=0
for size in "1920 1080" "1280 720" "960 540"; do
	for order in old fixed; do
		# shellcheck disable=SC2086
		log="${out}/vg-${order}-${size// /x}.log"
		valgrind --error-limit=no --num-callers=30 "${out}/teardown" ${order} ${size} \
			>"${out}/stdout-${order}-${size// /x}.txt" 2>"${log}" || true
		# One valgrind record per "Invalid read/write". gbm_surface_release_buffer is a tail call
		# into Mesa's (static, stripped) release_buffer, so the frame that names the call site
		# is main() at one of the teardown lines that call gbm_surface_release_buffer.
		hits="$(awk -v lines="${rel_lines}" '
			BEGIN{n=split(lines, L, " "); for (i = 1; i <= n; i++) want["(teardown.c:" L[i] ")"]=1}
			/== Invalid (read|write)/{rec=1; next}
			rec && /Address /{rec=0; next}
			rec && /by 0x[0-9A-F]+: main \(teardown.c:[0-9]+\)/{if ($NF in want) c++; rec=0}
			END{print c+0}' "${log}")"
		freed="$(grep -c 'free.d' "${log}" || true)"
		done_line="$(grep -c 'teardown: done' "${out}/stdout-${order}-${size// /x}.txt" || true)"
		if [ "${order}" = old ]; then want='>=1'; ok=$([ "${hits}" -ge 1 ] && echo PASS || echo FAIL)
		else want='0'; ok=$([ "${hits}" -eq 0 ] && [ "${done_line}" -eq 1 ] && echo PASS || echo FAIL); fi
		[ "${ok}" = PASS ] || fails=$((fails + 1))
		printf '%-9s %-5s release_buffer-UAF=%s (want %s) freed-block-notes=%s ran=%s  %s\n' \
			"${size// /x}" "${order}" "${hits}" "${want}" "${freed}" "${done_line}" "${ok}"
	done
done
echo "result: fails=${fails} $([ ${fails} -eq 0 ] && echo PASS || echo FAIL)"
[ "${fails}" -eq 0 ]
