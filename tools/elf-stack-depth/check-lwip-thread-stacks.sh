#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
#
# Host-side check: does every lwip thread that runs on a STATIC stack fit it?
# Static stacks overflow silently into the neighbouring .bss (the kernel's user-stack
# canary is compiled out under NDEBUG). The collector thread did, onto rt_table:
# docs/misc/2026-09-28-lwip-route-find-crash.md.
#
# Usage: tools/elf-stack-depth/check-lwip-thread-stacks.sh [LWIP_ELF] [LWIP_SRC_DIR]
#   LWIP_ELF      unstripped lwip (default .buildroot/_build/aarch64a72-generic-rpi4b/prog/lwip)
#   LWIP_SRC_DIR  the lwip tree the ELF was built from (default sources/phoenix-rtos-lwip),
#                 read for the collector's stack size
# Exit 0 = every root has at least RESERVE (default 256) spare bytes on its worst static path.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
elf="${1:-${root}/.buildroot/_build/aarch64a72-generic-rpi4b/prog/lwip}"
src="${2:-${root}/sources/phoenix-rtos-lwip}"
reserve="${RESERVE:-256}"

# The collector's size: the first numeric WAITTID_THREAD_STACKSZ define (the aarch64
# branch comes first), else the literal array size of a tree without the fix.
csz=$(grep -m1 -oE '^#define WAITTID_THREAD_STACKSZ[[:space:]]+[0-9]+' "${src}/port/threads.c" \
      | grep -oE '[0-9]+$' || true)
if [ -z "$csz" ]; then
    csz=$(grep -m1 -oE 'collector_stack\[[0-9]+\]' "${src}/port/threads.c" | grep -oE '[0-9]+' || true)
fi
[ -n "$csz" ] || { echo "check-lwip-thread-stacks: cannot read the collector stack size from ${src}/port/threads.c" >&2; exit 2; }

# The other static stacks, sizes from their declarations (uint32_t arrays: x4 bytes).
exec python3 "${root}/tools/elf-stack-depth/elf-stack-depth.py" "$elf" --reserve "$reserve" \
    --path-to rb_transplant \
    --root "thread_waittid_thr:${csz}" \
    --root ephy_linkThread:2048 \
    --root genet_linkPollThread:8192 \
    --root genet_irqThread:16384 \
    --root wifi_rxThread:16384 \
    --root wifi_joinThread:8192
