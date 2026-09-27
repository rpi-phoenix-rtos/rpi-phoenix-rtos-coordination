#!/usr/bin/env bash
#
# Host test of rpi4-kms's scan-out rules for imported buffers (gap G7,
# kms_scanout.h): native gcc + ASan/UBSan, seconds, no Pi. The server's import
# code itself (open/lseek/mmap of /v3dbuf, va2pa, the descriptor's lifetime) needs
# the Phoenix kernel and is covered by the libdrm-phoenix fake-server harness
# (libdrm-phoenix/hosttest/run.sh, prime_import_card0) and the Pi cycle m6h-g7.
#
# Negative control: the same checks built with -DSCANOUT_TEST_NO_RULES ("accept
# every import") must FAIL - proof that the checks can fail.
#
# bo_alias_test.c: the real kms_bo.c (PRIME import of another client's /kmsbuf
# export as an alias, and the lifetime of both BOs) against stand-ins for the
# Phoenix calls (shim/). kms_bo.c is compiled from a copy whose one aarch64
# barrier (dsb sy after zeroing a new BO) is replaced by a host fence; nothing else
# differs. Negative control: the same test linked with the G7 kms_bo.c (commit
# b5386948a, the rpi4-kms-g7 source), which refused every foreign /kmsbuf import,
# must FAIL.
#
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${KMS_HOSTTEST_OUT:-${here}/../out-hosttest}"
mkdir -p "${out}"
flags=(-std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I"${here}/..")

gcc "${flags[@]}" -o "${out}/scanout_test" "${here}/scanout_test.c"
"${out}/scanout_test"
rc=$?

gcc "${flags[@]}" -DSCANOUT_TEST_NO_RULES -o "${out}/scanout_test_norules" "${here}/scanout_test.c"
set +e
"${out}/scanout_test_norules" > "${out}/norules.log" 2>&1
nrc=$?
set -e
grep -E 'FAIL|RESULT' "${out}/norules.log" || true
if [ "${nrc}" -ne 0 ] && grep -q 'KMSHOST RESULT .* verdict=FAIL' "${out}/norules.log"; then
	echo "KMSHOST negative-control verdict=PASS (without the rules the refusal checks fail)"
else
	echo "KMSHOST negative-control verdict=FAIL (the checks did not notice missing rules)"
	exit 1
fi

# --- the alias (kms_bo.c) ---
root="$(cd "${here}/../../../.." && pwd)"
aflags=("${flags[@]}" -I"${here}/shim" -I"${here}/../../v3d-async")
host_copy() {   # <kms_bo.c source> <output>: the host copy (one barrier replaced, checked)
	sed 's/__asm__ volatile("dsb sy" ::: "memory");/__atomic_thread_fence(__ATOMIC_SEQ_CST);   \/* host *\//' "$1" > "$2"
	[ "$(grep -c '__atomic_thread_fence' "$2")" = 1 ] && ! grep -q '__asm__' "$2" ||
		{ echo "KMSHOST alias: the barrier substitution did not match exactly once in $1" >&2; exit 1; }
}
host_copy "${here}/../kms_bo.c" "${out}/kms_bo_host.c"
gcc "${aflags[@]}" -o "${out}/bo_alias_test" "${here}/bo_alias_test.c" "${out}/kms_bo_host.c"
set +e
"${out}/bo_alias_test"
arc=$?
set -e

git -C "${root}" show b5386948a:tools/gpu-lane/kms/kms_bo.c > "${out}/kms_bo_g7.c"
host_copy "${out}/kms_bo_g7.c" "${out}/kms_bo_g7_host.c"
gcc "${aflags[@]}" -o "${out}/bo_alias_test_g7" "${here}/bo_alias_test.c" "${out}/kms_bo_g7_host.c"
set +e
"${out}/bo_alias_test_g7" > "${out}/alias_g7.log" 2>&1
grc=$?
set -e
grep -E 'foreign_kmsbuf|FAIL$|RESULT' "${out}/alias_g7.log" | head -8 || true
if [ "${grc}" -ne 0 ] && grep -q 'KMSHOST RESULT alias .* verdict=FAIL' "${out}/alias_g7.log"; then
	echo "KMSHOST alias negative-control verdict=PASS (the G7 kms_bo.c fails the alias rows)"
else
	echo "KMSHOST alias negative-control verdict=FAIL (the G7 source passed: the test does not see the alias)"
	exit 1
fi
[ "${rc}" -eq 0 ] || exit "${rc}"
exit "${arc}"
