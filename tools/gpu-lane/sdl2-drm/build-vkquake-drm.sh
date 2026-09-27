#!/usr/bin/env bash
#
# Build `vkquake-drm` + its launcher `vkq-drm`: vkQuake 1.34 (the vkquake port's pinned upstream
# commit) on the FULL standard stack's Vulkan path -- SDL 2.30.12's stock KMSDRM video driver with
# its stock Vulkan code (VK_KHR_display surface), Mesa 26.2 v3dv as a static ICD reached through
# phxvk (tools/gpu-lane/vulkan-drm, as vkcube-drm) and libdrm-phoenix (build-out-m5b) ->
# rpi4-kms (card0) + rpi4-v3d-async (renderD128). The shipped /usr/bin/vkquake, the ports/vkquake
# port, the old lane's Mesa fork and tools/.gpu-libs are never touched (PLAN rule 2).
#
# This is NOT a relink of the port's objects (unlike quake2-drm / quake3-drm / stk-drm): the port
# replaces every SDL/platform TU with Phoenix glue whose video half is a no-WSI /dev/fb0 shim
# (pl_phoenix_vk_vid.c) and carries engine hunks that exist only for that shim. vkquake-drm is
# UPSTREAM vkQuake built from the port's own tarball with upstream's TU list (meson.build: srcs +
# the non-Windows block, incl. gl_vidsdl.c, in_sdl*.c, snd_sdl*.c, main_sdl.c, sys_sdl_unix.c),
# the port's four engine fixes that do not concern video (patches-vkquake/0001-0004) and one new
# patch, 0005 (no timestamp query pool until the render server serves SUBMIT_CPU, gap G5). So no
# byte-identical control is possible; the inverse control is that the shipped binary carries
# none of the new-lane strings and does carry the old ones.
#
#   <out>/sdl-vk-src/     copy of sdl2-drm's patched SDL source + patches-sdl-vulkan/ (SDL_VULKAN on)
#   <out>/sdl-vk-build/, sdl-vk-prefix/   its cmake build + install (libSDL2.a with KMSDRM Vulkan)
#   <out>/vkq-src/        vkQuake tarball + patches-vkquake/*.patch + the generated embedded pak
#   <out>/obj/            engine objects, hooks, trampolines, GL stubs, phxvk
#   <out>/vkquake-drm     static, unstripped (addr2line); vkquake-drm.stripped (stage this); .map
#   <out>/vkq-drm         the launcher
#   <out>/BUILD-INFO.txt, logs
#
# The pieces that make it link and run without a Vulkan loader or dlopen:
#   * vkqdrm/vkqdrm_hooks.c answers SDL_LoadObject("libvulkan.so.1") / SDL_LoadFunction(
#     "vkGetInstanceProcAddr") (-Wl,--wrap=SDL_LoadObject,...) with phxvk, and counts presents
#     in the gate's `flipstat ... (total N)` shape;
#   * vkqdrm/gen-vk-trampolines.py emits the vk* link symbols vkQuake calls directly (the set is
#     taken from the objects' undefined symbols, the prototypes from vulkan_core.h);
#   * a generated GL stub object: SDL's KMSDRM driver binds GBM + EGL statically (sdl2-drm patch
#     0006) for its GL windows, which vkQuake never creates (SDL_WINDOW_VULKAN takes KMSDRM's
#     GBM/EGL-free branch). Linking the Mesa GL build as well would put a second copy of Mesa's
#     util/NIR/broadcom compiler next to the ICD's; instead every gbm_* / egl* the SDL archive
#     references is a stub that prints its name and fails (returns 0 / NULL / EGL_FALSE);
#   * the ICD whole-archive (weak dispatch-table references, as vkcube-drm), --wrap=mmap
#     (libdrm-phoenix BO maps) and --wrap=ioctl (sync_file ioctls, M5 section 9).
#
# Reads (never writes): sdl2-drm's build-out/sdl-src + pkg-config-sdl + mesa-gl prefix (headers
# for SDL's configure only), mesa-drm/build-out-vulkan (the ICD + Vulkan headers + compat shim),
# vulkan-drm/phxvk, libdrm-phoenix build-out-m5b, the vkquake port's tarball and its vendored
# glue/vkquake_shaders.c (SPIR-V of this commit's Shaders/; the port's alias_common.inc hunk in
# it is inert here -- it tests a ubo flag bit only the port's r_alias.c hunk sets, not taken), the
# toolchain, the tree sysroot, the ports' libz.a. No Pi, no rebuild-rpi4b-fast.sh, no /srv. It
# does NOT run sdl2-drm/build.sh, mesa-drm/build.sh or vulkan-drm/build.sh.
#
# Usage: tools/gpu-lane/sdl2-drm/build-vkquake-drm.sh [-j N] [--libdrm-prefix <dir>] [--clean]
# Env:   VKQDRM_OUT             output dir (default build-out/vkquake-drm)
#        VKQDRM_EXTRA_PATCHES   space-separated vkQuake patch files applied after patches-vkquake/
#                               (e.g. patches-vkquake-perf/*.patch for a variant; part of the
#                               source stamp and BUILD-INFO)
#        VKQDRM_TARGET          the engine path the launcher execs (default /usr/bin/vkquake-drm),
#                               for staging a variant under its own name
# Stage (coordinator only; the live export is the fsid=0 one):
#   install -m 755 $OUT/vkquake-drm.stripped <export>/usr/bin/vkquake-drm
#   install -m 755 $OUT/vkq-drm              <export>/bin/vkq-drm
#   (docs/gpu-new-lane/MIGRATION.md, "Pre-registered Pi cycles")
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="$(realpath -m "${VKQDRM_OUT:-${here}/build-out/vkquake-drm}")"
jobs="$(nproc)"
clean=0
libdrm_src="${root}/tools/gpu-lane/libdrm-phoenix/build-out-m5b/prefix"
target="${VKQDRM_TARGET:-/usr/bin/vkquake-drm}"
extra_patches=()
for p in ${VKQDRM_EXTRA_PATCHES:-}; do extra_patches+=("$(realpath -m "${p}")"); done
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--libdrm-prefix) shift; libdrm_src="${1:?--libdrm-prefix needs a directory}" ;;
		--libdrm-prefix=*) libdrm_src="${1#--libdrm-prefix=}" ;;
		-h|--help) sed -n '2,52p' "${BASH_SOURCE[0]}"; exit 0 ;;
		*) echo "build-vkquake-drm: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
libdrm_src="$(realpath -m "${libdrm_src}")"
if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out}"
	exit 0
fi

SD="${here}/build-out"                                   # the sdl2-drm build (read-only here)
case "$out" in
	"${SD}"|"${SD}"/sdl-*|"${SD}"/mesa-gl|"${SD}"/mesa-gl/*|"${SD}"/qs-*|"${SD}"/pkg-config-sdl|"${SD}"/stk-drm*)
		echo "build-vkquake-drm: refusing output dir ${out}: it belongs to another sdl2-drm build" >&2; exit 1 ;;
esac

VKQ_COMMIT=1aa13a56cdf1b8c18a556e8e48a71a559b925d5a
PORT="${root}/sources/phoenix-rtos-ports/vkquake"
VKQ_TARBALL="${PORT}/${VKQ_COMMIT}.tar.gz"
VKQ_SHADERS_C="${PORT}/glue/vkquake_shaders.c"
B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
PHXCXX="${root}/tools/gpu-lane/e7-drm-build/bin/phx-g++"
MV="${root}/tools/gpu-lane/mesa-drm/build-out-vulkan"
ICD="${MV}/prefix/lib/libvulkan_broadcom.a"
VKINC="${MV}/prefix/include"
COMPAT_A="${MV}/compat/libmesadrm-compat.a"
PHXVK="${root}/tools/gpu-lane/vulkan-drm/phxvk"
SDL_SRC0="${SD}/sdl-src"
PKGC="${SD}/pkg-config-sdl"
shipped_prog="${B}/prog/vkquake"
shipped_bin="${root}/.buildroot/_fs/aarch64a72-generic-rpi4b/root/usr/bin/vkquake"

log() { printf '[vkquake-drm] %s\n' "$*"; }
die() { printf '[vkquake-drm] ERROR: %s\n' "$*" >&2; exit 1; }
sha() { if [ -e "$1" ]; then sha256sum "$1" | cut -d' ' -f1; else echo "absent"; fi; }

case "${target}" in
	/usr/bin/vkquake-drm*) ;;
	*) die "VKQDRM_TARGET ${target}: expected /usr/bin/vkquake-drm[-<variant>]" ;;
esac
for p in "${extra_patches[@]}"; do [ -f "${p}" ] || die "VKQDRM_EXTRA_PATCHES: no ${p}"; done
for p in "${VKQ_TARBALL}" "${VKQ_SHADERS_C}" "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-nm" "${TC}-strip" "${TC}-size" \
		"${TC}-readelf" "${TC}-objdump" "${PHXCXX}" "${B}/lib/libz.a" "${ICD}" "${VKINC}/vulkan/vulkan_core.h" "${COMPAT_A}" \
		"${PHXVK}/phxvk_loader.c" "${PHXVK}/phxvk_loader.h" "${SDL_SRC0}/CMakeLists.txt" "${SD}/sdl-src.stamp" "${PKGC}" \
		"${libdrm_src}/lib/libdrm.a" "${libdrm_src}/include/xf86drm.h" "${shipped_prog}" "${shipped_bin}"; do
	[ -e "${p}" ] || die "missing ${p} (sdl2-drm/build.sh, mesa-drm/build.sh --vulkan, libdrm-phoenix m5b, the vkquake port built?)"
done
grep -qE ' T __wrap_ioctl$' <<< "$("${TC}-nm" -g --defined-only "${libdrm_src}/lib/libdrm.a" 2>/dev/null)" \
	|| die "${libdrm_src}/lib/libdrm.a has no __wrap_ioctl (older than m5b)"
# the ICD was compiled against its own libdrm-phoenix snapshot; another libdrm.a is exact only
# with identical headers
[ -d "${MV}/libdrm-prefix/include" ] && { diff -r "${MV}/libdrm-prefix/include" "${libdrm_src}/include" > /dev/null \
	|| die "libdrm-phoenix headers of ${libdrm_src} differ from the ones the v3dv ICD was built with"; }

guarded=("${SD}/sdl-prefix/lib/libSDL2.a" "${SD}/sdl-src.stamp" "${SD}/quakespasm-drm" "${SD}/stk-drm/supertuxkart-drm"
	"${root}/tools/gpu-lane/vulkan-drm/build-out/vkcube-drm" "${ICD}" "${libdrm_src}/lib/libdrm.a" "${VKQ_TARBALL}"
	"${VKQ_SHADERS_C}" "${shipped_prog}" "${shipped_bin}" "${SDL_SRC0}/CMakeLists.txt")
declare -A before
for f in "${guarded[@]}"; do before["$f"]="$(sha "$f")"; done

mkdir -p "${out}"
TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/")

# --- 1. SDL with KMSDRM Vulkan ------------------------------------------------------------------
SVS="${out}/sdl-vk-src"
SVB="${out}/sdl-vk-build"
SVP="${out}/sdl-vk-prefix"
svstamp="$( (cat "${SD}/sdl-src.stamp"; cat "${here}"/patches-sdl-vulkan/*.patch) | sha256sum | cut -c1-16)"
if [ "$(cat "${out}/sdl-vk-src.stamp" 2>/dev/null || true)" != "${svstamp}" ]; then
	log "SDL source: sdl2-drm's patched tree (set $(cat "${SD}/sdl-src.stamp")) + $(ls "${here}"/patches-sdl-vulkan/*.patch | wc -l) Vulkan patch(es) (set ${svstamp})"
	rm -rf "${SVS}" "${SVB}" "${SVP}"
	cp -a "${SDL_SRC0}" "${SVS}"
	for p in "${here}"/patches-sdl-vulkan/*.patch; do
		patch -d "${SVS}" -p1 -s --no-backup-if-mismatch < "${p}" || die "patch failed: $(basename "${p}")"
	done
	echo "${svstamp}" > "${out}/sdl-vk-src.stamp"
fi
if [ ! -f "${SVB}/Makefile" ]; then
	log "SDL cmake configure (sdl2-drm/build.sh's options, SDL_VULKAN=ON)"
	mkdir -p "${SVB}"
	( cd "${SVB}" && PKG_CONFIG="${PKGC}" cmake "${SVS}" \
		-DCMAKE_INSTALL_PREFIX="${SVP}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
		-DCMAKE_SYSTEM_NAME=Generic -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
		-DCMAKE_C_COMPILER="${TC}-gcc" -DCMAKE_CXX_COMPILER="${TC}-g++" \
		-DCMAKE_AR="${TC}-gcc-ar" -DCMAKE_RANLIB="${TC}-gcc-ranlib" \
		-DCMAKE_C_FLAGS="${TFLAGS[*]} -O2 -g -std=gnu17" -DCMAKE_C_FLAGS_RELEASE="-DNDEBUG" \
		-DCMAKE_EXE_LINKER_FLAGS="${TFLAGS[*]} -Wl,-z,max-page-size=0x1000" -DPKG_CONFIG_EXECUTABLE="${PKGC}" \
		-DPHOENIX=ON -DSDL_LIBC=ON -DSDL_PTHREADS=ON -DSDL_CLOCK_GETTIME=ON -DSDL_SHARED=OFF -DSDL_STATIC=ON \
		-DSDL_TEST=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=ON -DSDL_KMSDRM_SHARED=OFF \
		-DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_VULKAN=ON -DSDL_HIDAPI=OFF -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF \
		-DSDL_IBUS=OFF -DSDL_PULSEAUDIO=OFF -DSDL_ALSA=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_OSS=OFF \
		-DSDL_SNDIO=OFF -DSDL_ESD=OFF -DSDL_NAS=OFF -DSDL_ARTS=OFF -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF \
		-DSDL_VIVANTE=OFF -DSDL_OFFSCREEN=OFF \
		> "${out}/sdl-vk-cmake.log" 2>&1 ) || { tail -40 "${out}/sdl-vk-cmake.log"; die "SDL cmake failed"; }
fi
log "SDL build (-j${jobs})"
make -C "${SVB}" -j"${jobs}" install > "${out}/sdl-vk-make.log" 2>&1 \
	|| { grep -E -B2 -A6 'error|Error' "${out}/sdl-vk-make.log" | head -60; die "SDL build failed"; }
SDL_A="${SVP}/lib/libSDL2.a"
[ -f "${SDL_A}" ] || die "no ${SDL_A}"
cfg="${SVP}/include/SDL2/SDL_config.h"
for d in SDL_VIDEO_DRIVER_KMSDRM SDL_VIDEO_VULKAN SDL_VIDEO_OPENGL_EGL SDL_INPUT_PHOENIX SDL_AUDIO_DRIVER_PHOENIX \
		SDL_THREAD_PTHREAD SDL_TIMER_UNIX; do
	grep -qE "^#define ${d} +1" "${cfg}" || die "SDL_config.h lacks ${d} 1"
done
for d in SDL_VIDEO_DRIVER_KMSDRM_DYNAMIC SDL_VIDEO_DRIVER_PHOENIX SDL_VIDEO_DRIVER_X11 SDL_VIDEO_DRIVER_WAYLAND \
		SDL_INPUT_LINUXEV SDL_LOADSO_DLOPEN; do
	if grep -qE "^#define ${d}( |$)" "${cfg}"; then die "SDL_config.h defines ${d}"; fi
done
grep -qE '^#define SDL_VIDEO_VULKAN +1' "${SD}/sdl-prefix/include/SDL2/SDL_config.h" \
	&& die "the default sdl2-drm SDL_config.h has SDL_VIDEO_VULKAN -- expected only in this variant"
grep -qE ' T KMSDRM_Vulkan_CreateSurface$' <<< "$("${TC}-nm" -g --defined-only "${SDL_A}" 2>/dev/null)" \
	|| die "libSDL2.a has no KMSDRM_Vulkan_CreateSurface"
log "  SDL_config.h: KMSDRM (static) + SDL_VIDEO_VULKAN, EGL, Phoenix HID input + audio; loadso dummy"

# --- 2. vkQuake source --------------------------------------------------------------------------
Q="${out}/vkq-src"
qstamp="$( (sha256sum "${VKQ_TARBALL}"; cat "${here}"/patches-vkquake/*.patch; for p in "${extra_patches[@]}"; do basename "${p}"; cat "${p}"; done) \
	| sha256sum | cut -c1-16)"
if [ "$(cat "${out}/vkq-src.stamp" 2>/dev/null || true)" != "${qstamp}" ]; then
	log "vkQuake source: ${VKQ_COMMIT:0:12} + $(ls "${here}"/patches-vkquake/*.patch | wc -l) patches + ${#extra_patches[@]} extra (set ${qstamp})"
	rm -rf "${Q}" "${out}/vkq-tmp"
	mkdir -p "${out}/vkq-tmp"
	tar -C "${out}/vkq-tmp" -xzf "${VKQ_TARBALL}"
	mv "${out}/vkq-tmp/vkQuake-${VKQ_COMMIT}" "${Q}"
	rmdir "${out}/vkq-tmp"
	for p in "${here}"/patches-vkquake/*.patch "${extra_patches[@]}"; do
		patch -d "${Q}" -p1 -s --no-backup-if-mismatch < "${p}" || die "patch failed: $(basename "${p}")"
	done
	# the embedded base pak (gfx/maps/default.cfg), built with the HOST compiler exactly as the
	# port does (Misc/vq_pak/Makefile uses its own HOST_CC)
	env -u CC make -C "${Q}/Misc/vq_pak" > "${out}/vq_pak.log" 2>&1 || { tail -20 "${out}/vq_pak.log"; die "embedded pak generation failed"; }
	[ -f "${Q}/Quake/embedded_pak.c" ] || die "Quake/embedded_pak.c not generated"
	echo "${qstamp}" > "${out}/vkq-src.stamp"
fi

# --- 3. compile ---------------------------------------------------------------------------------
# TU list = upstream meson.build `srcs` + its non-Windows block + embedded_pak + the SPIR-V arrays.
engine=(bgmusic cd_null cfgfile chase cl_demo cl_input cl_main cl_parse cl_tent cmd common console crc cvar
	gl_draw gl_fog gl_heap gl_mesh gl_model gl_refrag gl_rlight gl_rmain gl_rmisc gl_screen gl_sky gl_texmgr
	gl_vidsdl gl_warp host host_cmd image json in_sdl in_sdl2 in_sdl3 keys main_sdl mathlib mdfour mem menu
	net_dgrm net_loop net_main palette pr_cmds pr_edict pr_exec pr_ext r_alias r_brush r_part r_part_fte
	r_sprite r_world sbar snd_codec snd_dma snd_mem snd_mix snd_sdl snd_sdl3 snd_umx snd_wave steam strlcat
	strlcpy sv_main sv_move sv_phys sv_user sys_sdl tasks view wad world hash_map
	net_bsd net_udp pl_linux sys_sdl_unix embedded_pak)
# meson: c_std gnu11, -fno-omit-frame-pointer -fno-common, -D_FILE_OFFSET_BITS=64, USE_CODEC_WAVE (the
# only default-enabled codec; no mp3/flac/vorbis/opus libs here), release => NDEBUG; libphoenix's
# sched.h has no CPU_ZERO => TASK_AFFINITY_NOT_AVAILABLE (meson's own probe result). Two libphoenix
# gaps are bridged for these TUs only: vkqdrm_compat.h (<arm_neon.h>, which upstream gets from its
# PCH, and struct ipv6_mreq) and vkqdrm/include/execinfo.h (a zero-frame backtrace()).
QFLAGS=("${TFLAGS[@]}" -std=gnu11 -O2 -g -fno-omit-frame-pointer -fno-common -Wno-trigraphs -D_FILE_OFFSET_BITS=64
	-DUSE_CODEC_WAVE -DNDEBUG -DTASK_AFFINITY_NOT_AVAILABLE -include "${here}/vkqdrm/vkqdrm_compat.h"
	-I"${Q}/Quake" -I"${Q}/Quake/mimalloc" -I"${SVP}/include" -I"${SVP}/include/SDL2" -I"${VKINC}"
	-idirafter "${here}/vkqdrm/include" -c)
OB="${out}/obj"
rm -rf "${OB}"
mkdir -p "${OB}"
{
	for u in "${engine[@]}"; do printf '%s\n' "${Q}/Quake/${u}.c"; done
	printf '%s\n' "${VKQ_SHADERS_C}"
} > "${out}/tus.txt"
log "vkquake-drm: compiling $(grep -c . "${out}/tus.txt") TUs"
export TCGCC="${TC}-gcc" OB
vq_cc() { local f="$1" o; o="${OB}/$(basename "${f%.c}").o"; "${TCGCC}" "${@:2}" -o "${o}" "${f}" 2>> "${OB}/$(basename "${f%.c}").log" || { echo "compile FAILED: ${f}"; cat "${OB}/$(basename "${f%.c}").log"; exit 1; }; }
export -f vq_cc
xargs -P"${jobs}" -I{} bash -c 'vq_cc "$@"' _ {} "${QFLAGS[@]}" < "${out}/tus.txt" > "${out}/cc-fail.log" 2>&1 \
	|| { head -60 "${out}/cc-fail.log"; die "vkQuake compile failed"; }
cat "${OB}"/*.log > "${out}/cc.log" 2>/dev/null || true
rm -f "${OB}"/*.log
objs=()
while IFS= read -r f; do
	o="${OB}/$(basename "${f%.c}").o"
	[ -f "${o}" ] || die "object missing: ${o}"
	objs+=("${o}")
done < "${out}/tus.txt"
log "  compiled ${#objs[@]} objects ($(grep -c 'warning:' "${out}/cc.log" || true) warning line(s), ${out}/cc.log)"

# --- 4. the vk* trampolines (generated from the objects' own undefined vk* symbols) --------------
defined="$(for o in "${objs[@]}"; do "${TC}-nm" -g --defined-only "${o}"; done | awk '{ print $3 }' | LC_ALL=C sort -u)"
for o in "${objs[@]}"; do "${TC}-nm" -u "${o}"; done | awk '{ print $2 }' | grep -E '^vk[A-Z]' | LC_ALL=C sort -u \
	| LC_ALL=C comm -23 - <(printf '%s\n' "${defined}") > "${out}/vk-direct-calls.txt"
log "  vk* commands called as link symbols: $(grep -c . "${out}/vk-direct-calls.txt")"
python3 "${here}/vkqdrm/gen-vk-trampolines.py" "${VKINC}/vulkan/vulkan_core.h" "${out}/vk-direct-calls.txt" \
	"${OB}/vkqdrm_vk_trampolines.c" || die "trampoline generation failed"

# --- 5. GL stubs for the KMSDRM GL half SDL references ------------------------------------------
sdl_defs="$("${TC}-nm" -g --defined-only "${SDL_A}" 2>/dev/null | awk 'NF >= 3 { print $3 }' | LC_ALL=C sort -u)"
"${TC}-nm" -u "${SDL_A}" 2>/dev/null | awk '{ print $2 }' | grep -E '^(gbm_|egl[A-Z])' | LC_ALL=C sort -u \
	| LC_ALL=C comm -23 - <(printf '%s\n' "${sdl_defs}") > "${out}/gl-stub-names.txt"
log "  GBM/EGL symbols SDL's KMSDRM GL half references (stubbed): $(grep -c . "${out}/gl-stub-names.txt")"
{
	cat <<'EOF'
/*
 * GENERATED by build-vkquake-drm.sh -- do not edit. The gbm_* / egl* functions that libSDL2.a's
 * KMSDRM GL path references (sdl2-drm patch 0006 binds them statically). vkquake-drm creates only
 * an SDL_WINDOW_VULKAN window, whose KMSDRM path uses neither; Mesa's GL build is therefore not
 * linked, and each of these prints its name once and fails (0 / NULL / EGL_FALSE / EGL_NO_*).
 */
#include <unistd.h>

static void vkqdrm_nogl(const char *name, unsigned long len)
{
	static const char msg[] = "vkquake-drm: GL path not linked in this binary, called: ";
	(void)write(2, msg, sizeof(msg) - 1u);
	(void)write(2, name, len);
	(void)write(2, "\n", 1u);
}
EOF
	while IFS= read -r n; do
		printf '\nlong %s(void);\nlong %s(void)\n{\n\tvkqdrm_nogl("%s", %d);\n\treturn 0;\n}\n' "$n" "$n" "$n" "${#n}"
	done < "${out}/gl-stub-names.txt"
} > "${OB}/vkqdrm_nogl.c"

# --- 6. the rest of the program: hooks, trampolines, stubs, phxvk --------------------------------
cc_strict=("${TC}-gcc" -O2 -g -std=gnu17 -Wall -Wextra -Werror "${TFLAGS[@]}")
"${cc_strict[@]}" -I"${SVP}/include" -I"${PHXVK}" -I"${VKINC}" -c "${here}/vkqdrm/vkqdrm_hooks.c" -o "${OB}/vkqdrm_hooks.o" \
	|| die "vkqdrm_hooks.c compile failed"
"${cc_strict[@]}" -Wno-unused-parameter -I"${VKINC}" -c "${OB}/vkqdrm_vk_trampolines.c" -o "${OB}/vkqdrm_vk_trampolines.o" \
	|| die "trampolines compile failed"
"${cc_strict[@]}" -c "${OB}/vkqdrm_nogl.c" -o "${OB}/vkqdrm_nogl.o" || die "GL stubs compile failed"
"${cc_strict[@]}" -I"${PHXVK}" -I"${VKINC}" -c "${PHXVK}/phxvk_loader.c" -o "${OB}/phxvk_loader.o" || die "phxvk compile failed"
extra=("${OB}/vkqdrm_hooks.o" "${OB}/vkqdrm_vk_trampolines.o" "${OB}/vkqdrm_nogl.o" "${OB}/phxvk_loader.o")

LDP="${out}/libdrm-prefix"
rm -rf "${LDP}"
mkdir -p "${LDP}/lib"
cp -a "${libdrm_src}/include" "${LDP}/"
cp -a "${libdrm_src}/lib/libdrm.a" "${LDP}/lib/"

# --- 7. link --------------------------------------------------------------------------------------
# vkcube-drm's shape (C++ driver: Mesa's compiler is C++; -static; --gc-sections; 4 KiB pages; the
# ICD whole-archive; --wrap=mmap/ioctl for libdrm-phoenix) + SDL's loadso wrapped for the Vulkan
# "library" + the port's 32 MiB main stack (vkQuake runs Host_Frame on the main thread).
elf="${out}/vkquake-drm"
log "vkquake-drm: link"
rm -f "${elf}"
"${PHXCXX}" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,--wrap=mmap -Wl,--wrap=ioctl \
	-Wl,--wrap=SDL_LoadObject -Wl,--wrap=SDL_LoadFunction -Wl,--wrap=SDL_UnloadObject \
	-Wl,-z,stack-size=33554432 -Wl,-Map,"${elf}.map" -o "${elf}" "${objs[@]}" "${extra[@]}" \
	-Wl,--whole-archive "${ICD}" -Wl,--no-whole-archive \
	-Wl,--start-group "${SDL_A}" "${LDP}/lib/libdrm.a" "${COMPAT_A}" "${B}/lib/libz.a" -Wl,--end-group -lm \
	> "${out}/link.log" 2>&1 || { head -80 "${out}/link.log"; die "vkquake-drm link failed"; }
"${TC}-strip" -o "${elf}.stripped" "${elf}"
[ -s "${out}/link.log" ] && sed 's/^/[vkquake-drm]   link: /' "${out}/link.log" | head -20

# --- 8. verification ----------------------------------------------------------------------------
log "verify"
bad=0
if "${TC}-readelf" -l "${elf}" | grep -q INTERP; then log "  PT_INTERP present"; bad=1; fi
und="$("${TC}-nm" -u "${elf}" || true)"
log "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
[ -n "${und}" ] && { sed 's/^/[vkquake-drm]     /' <<< "${und}" | head -20; bad=1; }
syms="$("${TC}-nm" "${elf}")"
for s in KMSDRM_CreateDevice KMSDRM_Vulkan_LoadLibrary KMSDRM_Vulkan_CreateSurface KMSDRM_Vulkan_GetInstanceExtensions \
		SDL_Vulkan_CreateSurface SDL_PHOENIX_HID_Poll __wrap_SDL_LoadObject __wrap_SDL_LoadFunction \
		vkqdrm_GetInstanceProcAddr phxvk_GetInstanceProcAddr vk_icdGetInstanceProcAddr v3dv_CreateInstance \
		v3dv_queue_driver_submit wsi_CreateDisplayPlaneSurfaceKHR wsi_CreateSwapchainKHR wsi_QueuePresentKHR \
		vkCreateInstance vkQueueSubmit vkCmdDraw __wrap_mmap __wrap_ioctl drmPhoenixMmap drm_phoenix_ioctl \
		drmModeAtomicCommit Host_Init VID_Init; do
	if grep -qE " [TtWw] ${s}\$" <<< "${syms}"; then log "  symbol ${s}: yes"; else log "  symbol ${s}: NO"; bad=1; fi
done
# no Mesa GL in this binary (the stubs stand in), no old-lane glue (the port's patch made
# R_CreateBasicPipelines global for its fb0 shim; upstream has it file-static)
for s in gbmint_get_backend kmsro_drm_screen_create v3d_drm_screen_create_renderonly _mesa_glapi_get_proc_address \
		PL_VkHostAllocator R_CreateBasicPipelines g_vk_device PHOENIX_bootstrap phoenix_v3d_ioctl winsys_init; do
	if grep -qE " [TWDBR] ${s}\$" <<< "${syms}"; then log "  unexpected global symbol ${s}: PRESENT"; bad=1; fi
done
calls="$("${TC}-objdump" -d --no-show-raw-insn "${elf}" | awk '
	/^[0-9a-f]+ <.*>:$/ { fn = $2; gsub(/[<>:]/, "", fn); next }
	/\tbl?\t/ && / <(ioctl|mmap|SDL_LoadObject|SDL_LoadFunction|__wrap_SDL_LoadObject|__wrap_SDL_LoadFunction)>$/ {
		t = $NF; gsub(/[<>]/, "", t); print t, fn }' | sort | uniq -c)"
printf '%s\n' "${calls}" > "${out}/call-sites.txt"
awk '$2 == "ioctl" && $3 != "__wrap_ioctl" { b = 1 } END { exit b }' <<< "${calls}" \
	|| { log "  real ioctl() called from outside __wrap_ioctl"; bad=1; }
awk '$2 == "mmap" && $3 != "__wrap_mmap" { b = 1 } END { exit b }' <<< "${calls}" \
	|| { log "  real mmap() called from outside __wrap_mmap"; bad=1; }
if grep -qE ' __wrap_SDL_LoadObject KMSDRM_Vulkan_LoadLibrary$' <<< "${calls}" \
		&& grep -qE ' __wrap_SDL_LoadFunction KMSDRM_Vulkan_LoadLibrary$' <<< "${calls}"; then
	log "  PROOF: KMSDRM_Vulkan_LoadLibrary -> __wrap_SDL_LoadObject/__wrap_SDL_LoadFunction (the linked-in ICD)"
else
	log "  KMSDRM_Vulkan_LoadLibrary does not reach the loadso wraps:"; sed 's/^/[vkquake-drm]     /' <<< "${calls}"; bad=1
fi
for s in 'KMS/DRM Video Driver' '/dev/dri/' 'libdrm-phoenix:' 'DRMPHX_TRACE' 'DRMPHX sync' '/dev/kbd0' '/dev/audio0' \
		'VK_KHR_display' 'VK_KHR_swapchain' 'V3D %d.%d.%d.%d' 'phxvk: new GPU lane' 'vkquake-drm: new GPU lane' \
		'vkquake-drm flipstat' 'vkquake-drm presentstat' 'Vulkan couldn'"'"'t find an appropriate plane' 'vkQuake'; do
	n="$(grep -acF -- "${s}" "${elf}.stripped" || true)"
	log "  string '${s}': ${n}"
	[ "${n}" != 0 ] || bad=1
done
for s in 'v3d-winsys:' 'v3da-winsys:' 'phoenix_v3d_ioctl' 'peek_next_scanout' 'v3d-srv' 'V3DV_PHOENIX' '/dev/fb0' 'RPI4FB_GETMODE' \
		'pl_phoenix' 'PL_VkHostAllocator' 'vkvid:' 'phoenix-map.cfg' 'vktramp:'; do
	n="$(grep -acF -- "${s}" "${elf}.stripped" || true)"
	log "  old-lane string '${s}': ${n}"
	[ "${n}" = 0 ] || bad=1
done
for s in 'vkquake-drm: new GPU lane' 'KMS/DRM Video Driver' 'libdrm-phoenix:' 'phxvk: new GPU lane'; do
	if grep -aqF -- "${s}" "${shipped_bin}"; then log "  the SHIPPED vkquake carries '${s}'"; bad=1; fi
done
for s in 'phoenix-map.cfg' '/dev/fb0'; do
	grep -aqF -- "${s}" "${shipped_bin}" || { log "  shipped vkquake lacks '${s}' -- the negative checks prove nothing"; bad=1; }
done
log "  inverse control: shipped vkquake = old lane (fb0 shim, phoenix-map.cfg; no KMSDRM / libdrm-phoenix / phxvk)"
if grep -qE ' [Tt] dlopen$' <<< "${syms}"; then log "  note: dlopen is linked (libphoenix); SDL's loadso is the dummy one + the wraps"; fi

# --- 9. launcher ----------------------------------------------------------------------------------
"${TC}-gcc" -O2 -static -Wall -Wextra -Werror --sysroot="${S}/" -B"${S}/lib/" -iprefix "${S}/" \
	-DVKQDRM_TARGET="\"${target}\"" -o "${out}/vkq-drm" "${here}/vkqdrm/vkq-drm-launcher.c" || die "launcher compile failed"
if "${TC}-readelf" -l "${out}/vkq-drm" 2>/dev/null | grep -q INTERP; then die "vkq-drm has a PT_INTERP segment"; fi
grep -aqF "${target}" "${out}/vkq-drm" || die "launcher ELF lacks its exec target ${target}"
[ -z "$("${TC}-nm" -u "${out}/vkq-drm" || true)" ] || die "launcher has undefined symbols"

# --- provenance -----------------------------------------------------------------------------------
"${TC}-size" "${elf}" | sed 's/^/[vkquake-drm]   /'
src_git="$(git -C "${root}" status --porcelain -- tools/gpu-lane/sdl2-drm/vkqdrm tools/gpu-lane/sdl2-drm/patches-vkquake \
	tools/gpu-lane/sdl2-drm/patches-sdl-vulkan tools/gpu-lane/sdl2-drm/patches-vkquake-perf tools/gpu-lane/sdl2-drm/build-vkquake-drm.sh)"
{
	echo "built:               $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "sources git:         $( [ -n "${src_git}" ] && echo "DIRTY/untracked" || echo "clean at $(git -C "${root}" rev-parse HEAD)")"
	echo "vkQuake:             ${VKQ_COMMIT} + patches-vkquake set ${qstamp} ($(cd "${here}/patches-vkquake" && ls *.patch | tr '\n' ' '))"
	echo "extra patches:       ${#extra_patches[@]}$(for p in "${extra_patches[@]}"; do printf ' %s' "$(basename "${p}")"; done)"
	echo "launcher target:     ${target}"
	echo "shaders:             $(sha "${VKQ_SHADERS_C}" | cut -c1-16) ${VKQ_SHADERS_C}"
	echo "SDL:                 sdl2-drm set $(cat "${SD}/sdl-src.stamp") + patches-sdl-vulkan -> set ${svstamp}"
	echo "libSDL2.a (vk):      $(sha "${SDL_A}")"
	echo "v3dv ICD:            $(sha "${ICD}" | cut -c1-16) (Mesa patch set $(cat "${MV}/mesa-src.stamp" 2>/dev/null || echo '?'))"
	echo "phxvk_loader.c:      $(sha "${PHXVK}/phxvk_loader.c" | cut -c1-16)"
	echo "libdrm-phoenix:      $(sha "${LDP}/lib/libdrm.a") from ${libdrm_src}"
	echo "vk trampolines:      $(grep -c . "${out}/vk-direct-calls.txt") commands; GL stubs: $(grep -c . "${out}/gl-stub-names.txt") symbols"
	echo "vkquake-drm:         $(sha "${elf}") ($(stat -c%s "${elf}") B)"
	echo "vkquake-drm.stripped: $(sha "${elf}.stripped") ($(stat -c%s "${elf}.stripped") B)"
	echo "vkq-drm:             $(sha "${out}/vkq-drm") ($(stat -c%s "${out}/vkq-drm") B)"
	echo "shipped vkquake:     $(sha "${shipped_bin}") ($(stat -c%s "${shipped_bin}") B)"
	echo "libphoenix.a:        $(sha "${S}/lib/libphoenix.a")"
} > "${out}/BUILD-INFO.txt"
sed 's/^/[vkquake-drm]   /' "${out}/BUILD-INFO.txt"

gbad=0
for f in "${guarded[@]}"; do
	if [ "${before[$f]}" != "$(sha "$f")" ]; then log "  ERROR: shared file CHANGED during this build: $f"; gbad=1; fi
done
[ "${gbad}" = 0 ] || exit 1
log "guarded shared files unchanged (${#guarded[@]} checked)"
[ "${bad}" = 0 ] || die "verification failed (see above)"
log "done: stage ${elf}.stripped as ${target} and ${out}/vkq-drm as /bin/vkq-drm${target#/usr/bin/vkquake-drm}"
