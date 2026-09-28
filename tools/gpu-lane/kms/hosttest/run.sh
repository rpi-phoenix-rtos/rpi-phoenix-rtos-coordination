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
# mode_test.c (M9 scaled modes): the real server - kms_main.c's request handlers,
# kms_backend.c's SET_PLANE values, kms_bo.c - against stand-ins for the firmware,
# the vblank thread and the Phoenix calls. Negative control: the same test against
# the g8 source (f90f74b4a) must FAIL.
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
# --- scaled modes (M9): the real server (kms_main.c + kms_backend.c + kms_bo.c) ---
# mode_test.c #includes kms_main.c; each source tree is a host copy whose only
# changes replace the aarch64 instructions (checked: each exactly once, no __asm__
# left). Negative control: the g8 source (commit f90f74b4a = /bin/rpi4-kms-g8),
# which lists one mode and refuses every other, must FAIL.
mode_tree() {   # <git rev, or "work" for the working tree> <output dir>
	local rev="$1" dir="$2" f
	rm -rf "${dir}"
	mkdir -p "${dir}"
	for f in kms.h kms_proto.h kms_scanout.h kms_modes.h kms_main.c kms_backend.c kms_bo.c; do
		if [ "${rev}" = work ]; then
			[ ! -e "${here}/../${f}" ] || cp "${here}/../${f}" "${dir}/${f}"
		else
			git -C "${root}" show "${rev}:tools/gpu-lane/kms/${f}" > "${dir}/${f}" 2>/dev/null || rm -f "${dir}/${f}"
		fi
	done
	subst() {   # <file> <perl s///>: must change exactly one line
		local before
		before="$(cat "${dir}/$1")"
		perl -pi -e "$2" "${dir}/$1"
		[ "$(diff <(printf '%s\n' "${before}") "${dir}/$1" | grep -c '^>')" = 1 ] ||
			{ echo "KMSHOST mode: substitution in ${rev}:$1 did not change exactly one line" >&2; exit 1; }
	}
	subst kms.h 's/\Q__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v)::"memory");\E/extern uint64_t kms_host_now; v = kms_host_now;   \/* host *\//'
	subst kms_main.c 's/\Q__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));\E/f = 54000000u;   \/* host *\//'
	subst kms_backend.c 's/\Q__asm__ volatile("dsb sy" ::: "memory");\E/__atomic_thread_fence(__ATOMIC_SEQ_CST);   \/* host *\//'
	subst kms_bo.c 's/\Q__asm__ volatile("dsb sy" ::: "memory");\E/__atomic_thread_fence(__ATOMIC_SEQ_CST);   \/* host *\//'
	! grep -q '__asm__' "${dir}"/*.[ch] || { echo "KMSHOST mode: __asm__ left in ${rev}" >&2; exit 1; }
}
mode_build() {   # <tree dir> <output binary> [extra cc flags]; -Wno-unused-result: glibc's write() attribute (host only)
	gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-result -fsanitize=address,undefined -fno-omit-frame-pointer \
		-I"$1" -I"${here}/shim" -I"${here}/../../v3d-async" "${@:3}" \
		-o "$2" "${here}/mode_test.c" "$1/kms_backend.c" "$1/kms_bo.c"
}
mode_tree work "${out}/tree-g9"
mode_build "${out}/tree-g9" "${out}/mode_test"
set +e
"${out}/mode_test"
mrc=$?
set -e

mode_tree f90f74b4a "${out}/tree-g8"
mode_build "${out}/tree-g8" "${out}/mode_test_g8"
set +e
"${out}/mode_test_g8" > "${out}/mode_g8.log" 2>&1
m8rc=$?
set -e
grep -E 'FAIL$|RESULT' "${out}/mode_g8.log" | head -12 || true
if [ "${m8rc}" -ne 0 ] && grep -q 'KMSHOST RESULT mode .* verdict=FAIL' "${out}/mode_g8.log"; then
	echo "KMSHOST mode negative-control verdict=PASS (the g8 server fails the scaled-mode rows)"
else
	echo "KMSHOST mode negative-control verdict=FAIL (the g8 source passed: the test does not see the modes)"
	exit 1
fi

# the server's own switch back to one mode (-M native) must fail the same rows
mode_build "${out}/tree-g9" "${out}/mode_test_native" -DMODE_TEST_NATIVE_ONLY
set +e
"${out}/mode_test_native" > "${out}/mode_native.log" 2>&1
mnrc=$?
set -e
grep -E 'RESULT' "${out}/mode_native.log" || true
if [ "${mnrc}" -ne 0 ] && grep -q 'KMSHOST RESULT mode .* verdict=FAIL' "${out}/mode_native.log"; then
	echo "KMSHOST mode -M native control verdict=PASS (one mode: the scaled rows fail, as on g8)"
else
	echo "KMSHOST mode -M native control verdict=FAIL (-M native still offers scaled modes)"
	exit 1
fi

[ "${rc}" -eq 0 ] || exit "${rc}"
[ "${arc}" -eq 0 ] || exit "${arc}"
exit "${mrc}"
