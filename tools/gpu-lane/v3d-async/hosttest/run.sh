#!/usr/bin/env bash
#
# Host test of rpi4-v3d-async's scan-out placement policy (V3DA_BO_LOWMEM, proto 5,
# v3da_lowmem.h): native gcc + ASan/UBSan, seconds, no Pi. The server's own use of
# it (mmap(MAP_CONTIGUOUS), va2pa, the pool, the budget accounting at quarantine)
# needs the Phoenix kernel; the protocol half (libdrm-phoenix passing the flag,
# proto-5 negotiation, card0 ADDFB2 of a placed buffer) is covered by the
# libdrm-phoenix fake-server harness (libdrm-phoenix/hosttest/run.sh,
# scanout_lowmem), the rest by the Pi cycle m6i-low (M6-wayland.md section 18).
#
# Negative control: the same checks built with -DLOWMEM_TEST_NO_POLICY ("take the
# first block", the server before proto 5) must FAIL - proof that they can fail.
#
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${V3DA_HOSTTEST_OUT:-${here}/../out-hosttest}"
mkdir -p "${out}"
flags=(-std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I"${here}/..")

gcc "${flags[@]}" -o "${out}/lowmem_test" "${here}/lowmem_test.c"
set +e
"${out}/lowmem_test"
rc=$?
set -e

gcc "${flags[@]}" -DLOWMEM_TEST_NO_POLICY -o "${out}/lowmem_test_nopolicy" "${here}/lowmem_test.c"
set +e
"${out}/lowmem_test_nopolicy" > "${out}/nopolicy.log" 2>&1
nrc=$?
set -e
grep -E 'FAIL|RESULT' "${out}/nopolicy.log" || true
if [ "${nrc}" -ne 0 ] && grep -q 'LOWHOST RESULT .* verdict=FAIL' "${out}/nopolicy.log"; then
	echo "LOWHOST negative-control verdict=PASS (without the policy the placement checks fail)"
else
	echo "LOWHOST negative-control verdict=FAIL (the checks did not notice a missing policy)"
	exit 1
fi
exit "${rc}"
