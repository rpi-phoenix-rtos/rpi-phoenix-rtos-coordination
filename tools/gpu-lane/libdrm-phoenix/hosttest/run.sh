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
	-fno-omit-frame-pointer -D__phoenix__ -Dmain=drmprobe_main \
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
