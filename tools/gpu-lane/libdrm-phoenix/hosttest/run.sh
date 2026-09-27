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
	grep -E 'DRMPROBE (RESULT|device |open |identity|fstat|card1|dmabuf_size|dmabuf_sync|atomic_universal|sync_merge|prime_|import_clear|implicit_flip)|HOSTE2E|ERROR|runtime error' "${log}" || true
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
	grep -q 'HOSTE2E g7 .* card0_imports=5 imports_live=0 imports_released=5' "${log}" || why="${why} g7-counters"   # G7 x2 + scanout_lowmem x2 + G6 flip
	# proto 5: a BO created with the scan-out placement hint is placed (the fake counts it) and
	# card0 takes it; the plain one lands in the fake's low arena here, so it is taken too
	grep -q 'DRMPROBE scanout_lowmem 1920x1080 pages=2026 bogus_flag_errno=22 create=0 low_addfb_errno=0 plain_create=0 plain_addfb_errno=0 ok=1' "${log}" || why="${why} lowmem"
	grep -q 'HOSTE2E lowmem .* server_proto=5 v3d_high=0 lowmem_bos=1$' "${log}" || why="${why} lowmem-counters"
	# G6: cross-process implicit sync. The producer is the fake's "foreign" job (a
	# client this library does not know), pending until something waits: the consumer
	# reads stale pixels without sync, exports the dma-buf's fences, waits, reads the
	# producer's colour; a flip of the buffer with no in-fence is held until it is done.
	grep -q 'DRMPROBE dmabuf_sync_probe export_errno=0 import_errno=0 idle_fences=0 ok=1' "${log}" || why="${why} g6-probe"
	grep -q 'DRMPROBE dmabuf_sync_import setup=0 import_errno=0 reexport_errno=0 pending_after_import=1 nfences=1 wait=0 chain_done=1 .* ok=1' "${log}" || why="${why} g6-import"
	grep -q 'DRMPROBE dmabuf_sync_read producer=foreign export_errno=0 pending_at_export=1 nfences=1 wait=0 early_stale=1 bad_words=0 done_at_read=1 ok=1' "${log}" || why="${why} g6-read"
	grep -q 'DRMPROBE dmabuf_sync_flip producer=foreign addfb=0 pending_at_commit=1 flipped=1 .* done_at_flip=1 bad_words=0 flipped_back=1 ok=1' "${log}" || why="${why} g6-flip"
	grep -qE 'HOSTE2E g6 .* last_fence_queries=[1-9]' "${log}" || why="${why} g6-counters"
	# deferred flips: G13's implicit_flip (1, as before G6) + the G6 foreign flip (1): the second
	# one exists only because the library asked the server (g6-negative shows 1)
	grep -q 'HOSTE2E g6 .* deferred_flips=2$' "${log}" || why="${why} g6-deferred"
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
grep -q 'DRMPROBE RESULT .*failed=cl_clear,cl_clear_dep,import_clear,implicit_flip,prime_export_render,prime_import_render2,prime_import_card0,prime_import_card0_neg,scanout_lowmem,dmabuf_sync_probe,dmabuf_sync_import,dmabuf_sync_read,dmabuf_sync_flip, ' "${log}" || why="${why} failed-set"
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
grep -q 'DRMPROBE RESULT .*failed=cl_clear,cl_clear_dep,import_clear,implicit_flip,prime_import_card0,prime_import_card0_neg,scanout_lowmem,dmabuf_sync_flip, ' "${log}" || why="${why} failed-set"   # the G6 flip and scanout_lowmem need a card0 import
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
# (scanout_lowmem fails here: this knob puts EVERY import above 1 GiB, placed or not)
grep -q 'DRMPROBE RESULT .*gap=2 failed=cl_clear,cl_clear_dep,import_clear,implicit_flip,scanout_lowmem, ' "${log}" || why="${why} failed-set"   # G7 + G6 flip: gap=1 each
grep -q 'DRMPROBE prime_import_card0 export=0 .* import=0 .* addfb=-22 addfb_errno=22 shown=0 .* gone_after_errno=2 gap=1 ' "${log}" || why="${why} not-refused"
grep -q 'HOSTE2E g7 .* imports_live=0 imports_released=5' "${log}" || why="${why} g7-counters"
grep -qE 'ERROR: AddressSanitizer|runtime error' "${log}" && why="${why} sanitizer"
if [ -z "${why}" ]; then
	echo "HOSTE2E g7-high verdict=PASS (an import above 1 GiB: ADDFB2 EINVAL, graded gap=1, released)"
else
	echo "HOSTE2E g7-high verdict=FAIL (${why# } - see ${log})"
fi

# G6 negative control: a fake render server from BEFORE G6 (proto 3: G4 export and
# /v3dbuf, no BO_LAST_FENCE / BO_ATTACH_FENCE). The library HELLOs 4, falls back and
# must answer the dma-buf ioctls ENOTTY (Mesa's WSI probe fails soft, as before G6);
# then the consumer reads the foreign producer's buffer WITHOUT waiting - stale
# pixels, the producer's job still pending - and the flip of it goes ungated (the
# foreign job still pending at the flip event). Exactly the four G6 keys must fail.
log="${out}/e2e-g6-negative.log"
FAKE_V3DA_PROTO=3 "${out}/e2e" dri > "${log}" 2>&1 || true
grep -E 'DRMPROBE (RESULT|dmabuf_sync)|HOSTE2E g[46]|ERROR|runtime error' "${log}" || true
why=""
grep -q 'DRMPROBE RESULT .*failed=cl_clear,cl_clear_dep,import_clear,implicit_flip,dmabuf_sync_probe,dmabuf_sync_import,dmabuf_sync_read,dmabuf_sync_flip, ' "${log}" || why="${why} failed-set"
grep -q 'DRMPROBE dmabuf_sync_probe export_errno=25 ' "${log}" || why="${why} probe-not-enotty"
grep -q 'DRMPROBE dmabuf_sync_read producer=foreign export_errno=25 .* bad_words=4096 done_at_read=0 ok=0' "${log}" || why="${why} read-not-stale"
grep -q 'DRMPROBE dmabuf_sync_flip producer=foreign addfb=0 .* flipped=1 .* done_at_flip=0 .* ok=0' "${log}" || why="${why} flip-not-ungated"
grep -q 'DRMPROBE prime_export_render rc=0 .* ok=1' "${log}" || why="${why} g4-regressed"
grep -q 'HOSTE2E g6 .* server_proto=3 last_fence_queries=0 deferred_flips=1$' "${log}" || why="${why} g6-counters"   # only G13's
grep -qE 'ERROR: AddressSanitizer|runtime error' "${log}" && why="${why} sanitizer"
if [ -z "${why}" ]; then
	echo "HOSTE2E g6-negative verdict=PASS (the G6 tests fail against a proto-3 server: stale read, ungated flip; the rest as before)"
else
	echo "HOSTE2E g6-negative verdict=FAIL (${why# } - see ${log})"
fi

# Scan-out placement (render server proto 5, V3DA_BO_LOWMEM): the fake render server
# puts every BO above 1 GiB unless it places it (FAKE_V3DA_HIGH=1) - m6h-g7's case, where
# 2 of ~6 client buffers landed at 0xf8000000 and card0 refused them (why=above_1g).
# The BO created with the hint must still be taken by card0, the plain one refused;
# the unplaced BOs of the G7 and G6 flip tests are refused as on the Pi (gap=1 each).
log="${out}/e2e-lowmem-high.log"
FAKE_V3DA_HIGH=1 "${out}/e2e" dri > "${log}" 2>&1 || true
grep -E 'DRMPROBE (RESULT|scanout_lowmem)|HOSTE2E lowmem|ERROR|runtime error' "${log}" || true
why=""
grep -q 'DRMPROBE RESULT .*gap=2 failed=cl_clear,cl_clear_dep,import_clear,implicit_flip, ' "${log}" || why="${why} failed-set"
grep -q 'DRMPROBE scanout_lowmem 1920x1080 pages=2026 bogus_flag_errno=22 create=0 low_addfb_errno=0 plain_create=0 plain_addfb_errno=22 ok=1' "${log}" || why="${why} not-placed"
grep -q 'DRMPROBE prime_import_card0 .* addfb=-22 addfb_errno=22 .* gap=1 ' "${log}" || why="${why} plain-not-high"
grep -q 'HOSTE2E lowmem .* server_proto=5 v3d_high=1 lowmem_bos=1$' "${log}" || why="${why} lowmem-counters"
grep -q 'HOSTE2E g4 .* exports_live=0 v3dbuf_imports=1 bos_live=0' "${log}" || why="${why} g4-counters"
grep -qE 'ERROR: AddressSanitizer|runtime error' "${log}" && why="${why} sanitizer"
if [ -z "${why}" ]; then
	echo "HOSTE2E lowmem-high verdict=PASS (render BOs above 1 GiB: the hinted one is placed and scanned out, the plain ones refused)"
else
	echo "HOSTE2E lowmem-high verdict=FAIL (${why# } - see ${log})"
fi

# Placement negative control: the same world against a proto-4 render server (G6,
# before proto 5). The library HELLOs 5, falls back, and drops the hint: the hinted BO
# lands high like any other and card0 refuses it - scanout_lowmem must FAIL.
log="${out}/e2e-lowmem-negative.log"
FAKE_V3DA_HIGH=1 FAKE_V3DA_PROTO=4 "${out}/e2e" dri > "${log}" 2>&1 || true
grep -E 'DRMPROBE (RESULT|scanout_lowmem)|HOSTE2E lowmem|ERROR|runtime error' "${log}" || true
why=""
grep -q 'DRMPROBE RESULT .*gap=2 failed=cl_clear,cl_clear_dep,import_clear,implicit_flip,scanout_lowmem, ' "${log}" || why="${why} failed-set"
grep -q 'DRMPROBE scanout_lowmem .* create=0 low_addfb_errno=22 .* plain_addfb_errno=22 .* ok=0' "${log}" || why="${why} placed-anyway"
grep -q 'HOSTE2E lowmem .* server_proto=4 v3d_high=1 lowmem_bos=0$' "${log}" || why="${why} lowmem-counters"
grep -q 'DRMPROBE dmabuf_sync_probe .* ok=1' "${log}" || why="${why} g6-regressed"
grep -qE 'ERROR: AddressSanitizer|runtime error' "${log}" && why="${why} sanitizer"
if [ -z "${why}" ]; then
	echo "HOSTE2E lowmem-negative verdict=PASS (against a proto-4 server the hint is dropped: the buffer lands high and card0 refuses it)"
else
	echo "HOSTE2E lowmem-negative verdict=FAIL (${why# } - see ${log})"
fi
