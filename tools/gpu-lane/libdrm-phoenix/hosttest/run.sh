#!/usr/bin/env bash
#
# Host-side test of libdrm-phoenix's pure marshalling logic (drm_phoenix_logic.c):
# atomic flattening, GETPROPERTY/VERSION fill, drmDevice layout, ioctl-number and
# mmap-token decoding, and the byte layouts rpi4-kms copies from DRM. Native gcc
# with ASan+UBSan; seconds, no Pi. Headers: pristine external/libdrm (the host
# takes drm.h's Linux branch) + the server wire header kms_proto.h.
#
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../../.." && pwd)"
bout="${DRMPHX_OUT:-${here}/../build-out}"   # the libdrm-phoenix build.sh --out directory
out="${bout}/hosttest"
mkdir -p "${out}"
gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
	-I"${here}/../src" -I"${root}/external/libdrm" -I"${root}/external/libdrm/include/drm" \
	-I"${root}/tools/gpu-lane/kms" \
	-o "${out}/hosttest" "${here}/hosttest.c" "${here}/../src/drm_phoenix_logic.c"
"${out}/hosttest"

# ---------------------------------------------------------------------------
# Stage 2: end-to-end - the real drmprobe + the PATCHED libdrm + the backend,
# native, against fake servers (mock/fake.c). Needs build.sh to have prepared
# build-out/src (the patched tree) and build-out/build (the generated fourcc table).
# The fake GPU executes nothing, so exactly the four pixel checks must fail:
# cl_clear, cl_clear_dep and (M3 part 2) import_clear, implicit_flip. Everything
# else of M3 part 2 is checked by name: fstat on every node, the dma-buf size,
# the render-node import + re-import, and the implicit (G13) flip fence, which
# the fake kms counts when a flip arrives with a still-pending render fence.
# ---------------------------------------------------------------------------
src="${bout}/src"
gen="${bout}/build"
if [ ! -f "${src}/phoenix/xf86drm_phoenix.c" ] || [ ! -f "${gen}/generated_static_table_fourcc.h" ]; then
	echo "HOSTE2E skipped: run tools/gpu-lane/libdrm-phoenix/build.sh --lib-only first"
	exit 0
fi
gcc -std=gnu11 -O1 -g -Wall -Wno-unused-parameter -Wno-deprecated-declarations -fsanitize=address,undefined \
	-fno-omit-frame-pointer -D__phoenix__ -Dmain=drmprobe_main -DDRMPROBE_NO_FORK \
	-include "${here}/mock/phx_mock.h" -include "${here}/mock/hostconfig.h" \
	-I"${here}/mock" -I"${src}/phoenix" -I"${src}" -I"${src}/include/drm" -I"${gen}" -I"${here}/../include" \
	-I"${root}/tools/gpu-lane/v3d-async" -I"${root}/tools/gpu-lane/kms" \
	-c "${here}/../drmprobe/drmprobe.c" -o "${out}/drmprobe.o"
gcc -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-omit-frame-pointer -D__phoenix__ \
	-include "${here}/mock/phx_mock.h" -include "${here}/mock/hostconfig.h" \
	-I"${here}/mock" -I"${src}/phoenix" -I"${src}" -I"${src}/include/drm" -I"${gen}" -I"${here}/../include" \
	-I"${root}/tools/gpu-lane/v3d-async" -I"${root}/tools/gpu-lane/kms" \
	-o "${out}/e2e" "${out}/drmprobe.o" "${here}/e2e_main.c" "${here}/mock/fake.c" \
	"${root}/tools/gpu-lane/v3d-async/v3da_clgen.c" \
	"${src}/xf86drm.c" "${src}/xf86drmMode.c" "${src}/xf86drmHash.c" "${src}/xf86drmRandom.c" "${src}/xf86drmSL.c" \
	"${src}/phoenix/xf86drm_phoenix.c" "${src}/phoenix/drm_phoenix_kms.c" "${src}/phoenix/drm_phoenix_v3d.c" \
	"${src}/phoenix/drm_phoenix_logic.c" "${src}/phoenix/drm_phoenix_wrap.c" \
	"${src}/phoenix/drm_phoenix_wrap_ioctl.c"
# G4: prime_export_render / prime_import_render2 must pass against the fake G4 server,
# every export the probe made must be withdrawn and every BO released at the end
# (bos_live counts only the probe's leftovers: 0). The two-process case
# (prime_export_xproc: fork + SCM_RIGHTS) needs a real kernel and is compiled out here.
for mode in legacy dri; do
	log="${out}/e2e-${mode}.log"
	"${out}/e2e" "${mode}" > "${log}" 2>&1 || true
	grep -E 'DRMPROBE (RESULT|device |open |identity|fstat|card1|dmabuf_size|atomic_universal|sync_merge|prime_|import_clear|implicit_flip)|HOSTE2E|ERROR|runtime error' "${log}" || true
	why=""
	grep -q 'DRMPROBE RESULT .*failed=cl_clear,cl_clear_dep,import_clear,implicit_flip, ' "${log}" || why="${why} failed-set"
	grep -qE 'ERROR: AddressSanitizer|runtime error' "${log}" && why="${why} sanitizer"
	grep -q 'DRMPROBE fstat_nodes .* ok=1' "${log}" || why="${why} fstat"
	grep -q 'DRMPROBE dmabuf_size .* ok=1' "${log}" || why="${why} dmabuf_size"
	grep -q 'DRMPROBE prime_import_render rc=0 .* ok=1' "${log}" || why="${why} import"
	grep -q 'DRMPROBE prime_reimport .* ok=1' "${log}" || why="${why} reimport"
	grep -q 'DRMPROBE prime_reexport_render rc=0 .* ok=1' "${log}" || why="${why} reexport"   # M5 (G4a)
	grep -q 'DRMPROBE atomic_universal .* ok=1' "${log}" || why="${why} atomic_universal"   # M5
	grep -q 'DRMPROBE sync_merge setup=0 merge=0 .* ok=1' "${log}" || why="${why} sync_merge"   # M5b (G15)
	grep -q 'DRMPROBE implicit_flip submit=0 flip=0 events=1 ' "${log}" || why="${why} implicit_flip"
	grep -qE 'HOSTE2E m3p2 .* imports=1 imports_closed=1 deferred_flips=[1-9]' "${log}" || why="${why} m3p2-counters"
	grep -q 'DRMPROBE prime_export_render rc=0 errno=0 path=/v3dbuf/[0-9]* size=65536 fstat_chr=1 mmap=1 bad_words=0 xwrite=1 reexport_same_name=1 self_import=0 .* ok=1' "${log}" || why="${why} g4-export"
	grep -q 'DRMPROBE prime_import_render2 conn=1 rc=0 .* survives_creator_close=1 released=1 ok=1' "${log}" || why="${why} g4-import2"
	grep -q 'HOSTE2E g4 .* exports_live=0 v3dbuf_imports=1 bos_live=0' "${log}" || why="${why} g4-counters"
	# G7: a render BO on card0 (import, UIF/short-pitch refusals, ADDFB2, flip, all client
	# references dropped while shown, name alive until flip-off + RMFB, then gone)
	grep -q 'DRMPROBE prime_import_card0 export=0 errno=0 path=/v3dbuf/[0-9]* import=0 import_errno=0 reimport_same=1 .* uif_errno=22 short_pitch_errno=22 addfb=0 addfb_errno=0 shown=1 alive_while_shown=1 flipped_off=1 rmfb=0 gone_after_errno=2 ok=1' "${log}" || why="${why} g7-import"
	grep -q 'DRMPROBE prime_import_card0_neg badfd_errno=9 notbuf_errno=22 small_import=0 small_import_errno=0 small_addfb_errno=22 released=1 ok=1' "${log}" || why="${why} g7-neg"
	grep -q 'HOSTE2E g7 .* card0_imports=2 imports_live=0 imports_released=2' "${log}" || why="${why} g7-counters"
	if [ "${mode}" = dri ]; then
		grep -q 'DRMPROBE identity node=card1 version=v3d .* node_type=0 .* ok=1' "${log}" || why="${why} card1"
		grep -q 'DRMPROBE fstat_nodes n=3 ' "${log}" || why="${why} fstat-n3"
	fi
	if [ -z "${why}" ]; then
		echo "HOSTE2E ${mode} verdict=PASS (only the fake-GPU pixel checks failed, as expected)"
	else
		echo "HOSTE2E ${mode} verdict=FAIL (${why# } - see ${log})"
	fi
done

# Negative control: the same probe against a fake render server from BEFORE G4
# (HELLO exactly 2, opcode 22 unknown, no /v3dbuf). The library must fall back to
# proto 2 (everything else passes as before) and the G4 tests must FAIL, with ENOSYS
# from the library - proof that they can fail.
log="${out}/e2e-g4-negative.log"
FAKE_V3DA_PROTO=2 "${out}/e2e" dri > "${log}" 2>&1 || true
grep -E 'DRMPROBE (RESULT|prime_export_render|prime_import_render2)|HOSTE2E g4|ERROR|runtime error' "${log}" || true
why=""
# (the G7 card0 tests need a /v3dbuf export, so they fail here too)
grep -q 'DRMPROBE RESULT .*failed=cl_clear,cl_clear_dep,import_clear,implicit_flip,prime_export_render,prime_import_render2,prime_import_card0,prime_import_card0_neg, ' "${log}" || why="${why} failed-set"
grep -q 'DRMPROBE prime_export_render rc=-1 errno=38 .* ok=0' "${log}" || why="${why} export-not-enosys"
grep -qE 'ERROR: AddressSanitizer|runtime error' "${log}" && why="${why} sanitizer"
if [ -z "${why}" ]; then
	echo "HOSTE2E g4-negative verdict=PASS (the G4 tests fail against a proto-2 server, the rest as before)"
else
	echo "HOSTE2E g4-negative verdict=FAIL (${why# } - see ${log})"
fi

# G7 negative control: a fake display server from BEFORE G7 (HELLO exactly 1, no
# PRIME_IMPORT). The library must fall back to proto 1 (every KMS test passes as
# before) and exactly the two G7 tests must FAIL, with ENOSYS from the library.
log="${out}/e2e-g7-negative.log"
FAKE_KMS_PROTO=1 "${out}/e2e" dri > "${log}" 2>&1 || true
grep -E 'DRMPROBE (RESULT|prime_import_card0)|HOSTE2E g7|ERROR|runtime error' "${log}" || true
why=""
grep -q 'DRMPROBE RESULT .*failed=cl_clear,cl_clear_dep,import_clear,implicit_flip,prime_import_card0,prime_import_card0_neg, ' "${log}" || why="${why} failed-set"
grep -q 'DRMPROBE prime_import_card0 export=0 .* import=-1 import_errno=38 .* ok=0' "${log}" || why="${why} import-not-enosys"
grep -q 'HOSTE2E g7 .* card0_imports=0 imports_live=0' "${log}" || why="${why} g7-counters"
grep -qE 'ERROR: AddressSanitizer|runtime error' "${log}" && why="${why} sanitizer"
if [ -z "${why}" ]; then
	echo "HOSTE2E g7-negative verdict=PASS (the G7 tests fail against a proto-1 display server, the rest as before)"
else
	echo "HOSTE2E g7-negative verdict=FAIL (${why# } - see ${log})"
fi

# G7 above 1 GiB: every import lands where the firmware plane cannot fetch (the Pi
# cannot be made to produce it on demand). ADDFB2 must answer EINVAL - the probe
# grades that as gap=1, not a failure - and the import must still be released.
log="${out}/e2e-g7-high.log"
FAKE_KMS_IMPORT_HIGH=1 "${out}/e2e" dri > "${log}" 2>&1 || true
grep -E 'DRMPROBE (RESULT|prime_import_card0)|HOSTE2E g7|ERROR|runtime error' "${log}" || true
why=""
grep -q 'DRMPROBE RESULT .*gap=1 failed=cl_clear,cl_clear_dep,import_clear,implicit_flip, ' "${log}" || why="${why} failed-set"
grep -q 'DRMPROBE prime_import_card0 export=0 .* import=0 .* addfb=-22 addfb_errno=22 shown=0 .* gone_after_errno=2 gap=1 ' "${log}" || why="${why} not-refused"
grep -q 'HOSTE2E g7 .* imports_live=0 imports_released=2' "${log}" || why="${why} g7-counters"
grep -qE 'ERROR: AddressSanitizer|runtime error' "${log}" && why="${why} sanitizer"
if [ -z "${why}" ]; then
	echo "HOSTE2E g7-high verdict=PASS (an import above 1 GiB: ADDFB2 EINVAL, graded gap=1, released)"
else
	echo "HOSTE2E g7-high verdict=FAIL (${why# } - see ${log})"
fi
