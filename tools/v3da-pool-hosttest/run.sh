#!/usr/bin/env bash
#
# Host test of rpi4-v3d-async's BO pool bound (v3da_pool.h, C17): native gcc +
# ASan/UBSan, seconds, no Pi.
#   usage: run.sh [<dir holding v3da_pool.h>]
#   default: sources/phoenix-rtos-devices/gpu/rpi4-v3d-async
#
# Negative control: the same checks built with -DPOOL_TEST_NO_TRIM (the server
# before C17: no cap, no idle sweep) must FAIL - proof that they can fail.
#
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="${1:-${here}/../../sources/phoenix-rtos-devices/gpu/rpi4-v3d-async}"
out="${POOLHOST_OUT:-${here}/out}"
mkdir -p "${out}"
flags=(-std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I"${src}")

gcc "${flags[@]}" -o "${out}/pool_test" "${here}/pool_test.c"
set +e
"${out}/pool_test"
rc=$?
set -e

gcc "${flags[@]}" -DPOOL_TEST_NO_TRIM -o "${out}/pool_test_notrim" "${here}/pool_test.c"
set +e
"${out}/pool_test_notrim" > "${out}/notrim.log" 2>&1
nrc=$?
set -e
grep -E 'FAIL|RESULT' "${out}/notrim.log" || true
if [ "${nrc}" -ne 0 ] && grep -q 'POOLHOST RESULT .* verdict=FAIL' "${out}/notrim.log"; then
	echo "POOLHOST negative-control verdict=PASS (without the trim the bound checks fail)"
else
	echo "POOLHOST negative-control verdict=FAIL (the checks did not notice a missing trim)"
	exit 1
fi
exit "${rc}"
