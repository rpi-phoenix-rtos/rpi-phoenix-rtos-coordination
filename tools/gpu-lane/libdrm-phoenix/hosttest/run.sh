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
out="${here}/../build-out/hosttest"
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
# The fake GPU executes nothing, so exactly cl_clear and cl_clear_dep must fail.
# ---------------------------------------------------------------------------
src="${here}/../build-out/src"
gen="${here}/../build-out/build"
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
	"${src}/phoenix/drm_phoenix_logic.c" "${src}/phoenix/drm_phoenix_wrap.c"
for mode in legacy dri; do
	"${out}/e2e" "${mode}" > "${out}/e2e-${mode}.log" 2>&1 || true
	grep -E 'DRMPROBE (RESULT|device |open |identity)|HOSTE2E|ERROR|runtime error' "${out}/e2e-${mode}.log" || true
	if grep -q 'DRMPROBE RESULT .*failed=cl_clear,cl_clear_dep, ' "${out}/e2e-${mode}.log" && \
			! grep -qE 'ERROR: AddressSanitizer|runtime error' "${out}/e2e-${mode}.log"; then
		echo "HOSTE2E ${mode} verdict=PASS (only the fake-GPU pixel checks failed, as expected)"
	else
		echo "HOSTE2E ${mode} verdict=FAIL (see ${out}/e2e-${mode}.log)"
	fi
done
