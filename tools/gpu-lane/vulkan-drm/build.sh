#!/usr/bin/env bash
#
# vulkan-drm (new GPU lane, M5): Vulkan on the DRM-shaped stack.
#
#   Mesa 26.2 v3dv built as a STATIC ICD      (tools/gpu-lane/mesa-drm/build.sh --vulkan)
#   + phxvk, a static stand-in for the Vulkan loader (phxvk/phxvk_loader.c)
#   + upstream vkcube (Vulkan-Tools, Apache-2.0) with its VK_KHR_display WSI only
#   + libdrm-phoenix, linked -Wl,--wrap=mmap
#   -> one static aarch64-phoenix binary, vkcube-drm.
#
# Writes only into build-out/ (gitignored):
#   Vulkan-Tools/           clone of Vulkan-Tools at VT_COMMIT + patches/vkcube/*.patch (re-applied each run)
#   libdrm-prefix/          snapshot of the libdrm-phoenix prefix this link used (+ libdrm-snapshot.txt)
#   obj/                    cube.o, phxvk_loader.o
#   vkcube-drm              static, unstripped (addr2line); vkcube-drm.map
#   vkcube-drm.stripped     stage this
#   BUILD-INFO.txt          inputs + sha256 of the outputs
# Reads: the Mesa --vulkan build (tools/gpu-lane/mesa-drm/build-out-vulkan, built by this script
# unless --skip-mesa), the tree sysroot, the toolchain, the ports' libz.a. No Pi, no
# rebuild-rpi4b-fast.sh, no /srv. The old lane's Vulkan port (ports/vkquake, the Mesa fork,
# tools/.gpu-libs) is never read or linked.
#
# Usage: tools/gpu-lane/vulkan-drm/build.sh [--clean] [--skip-mesa] [-j N]
#                                           [--libdrm-prefix <dir>]   (default libdrm-phoenix/build-out-m5/prefix)
# Stage (coordinator only):
#   sudo install -m 755 tools/gpu-lane/vulkan-drm/build-out/vkcube-drm.stripped <live NFS export>/bin/vkcube-drm
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out="${here}/build-out"
clean=0
skip_mesa=0
jobs="$(nproc)"
libdrm_src_prefix="${root}/tools/gpu-lane/libdrm-phoenix/build-out-m5/prefix"
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) clean=1 ;;
		--skip-mesa) skip_mesa=1 ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--libdrm-prefix) shift; libdrm_src_prefix="${1:?--libdrm-prefix needs a directory}" ;;
		--libdrm-prefix=*) libdrm_src_prefix="${1#--libdrm-prefix=}" ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
case "${libdrm_src_prefix}" in
	/*) ;;
	*) libdrm_src_prefix="${PWD}/${libdrm_src_prefix}" ;;
esac

VT_URL=https://github.com/KhronosGroup/Vulkan-Tools.git
VT_TAG=vulkan-sdk-1.4.350.0
VT_COMMIT=1cb3a319969cf0d3e2315b0a87a27447f55b4167   # Vulkan headers 1.4.350 <= Mesa 26.2's 1.4.354
MESA_OUT="${root}/tools/gpu-lane/mesa-drm/build-out-vulkan"
B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
PHXCXX="${root}/tools/gpu-lane/e7-drm-build/bin/phx-g++"

if [ "${clean}" = 1 ]; then
	rm -rf "${out}"
	echo "cleaned ${out} (the Mesa --vulkan build: tools/gpu-lane/mesa-drm/build.sh --vulkan --clean)"
	exit 0
fi
for p in "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-nm" "${TC}-strip" "${PHXCXX}" "${B}/lib/libz.a" \
		"${libdrm_src_prefix}/lib/libdrm.a"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done
mkdir -p "${out}"

# --- Mesa v3dv (static ICD) ---------------------------------------------------------------
if [ "${skip_mesa}" = 0 ]; then
	echo "== Mesa --vulkan (tools/gpu-lane/mesa-drm/build.sh, log ${out}/mesa-vulkan.log)"
	"${root}/tools/gpu-lane/mesa-drm/build.sh" --vulkan -j "${jobs}" --libdrm-prefix "${libdrm_src_prefix}" \
		> "${out}/mesa-vulkan.log" 2>&1 || { tail -30 "${out}/mesa-vulkan.log"; exit 1; }
	sed -n '/== v3dv static ICD/,$p' "${out}/mesa-vulkan.log" | sed 's/^/  /'
fi
ICD="${MESA_OUT}/prefix/lib/libvulkan_broadcom.a"
VKINC="${MESA_OUT}/prefix/include"
COMPAT_A="${MESA_OUT}/compat/libmesadrm-compat.a"
for p in "${ICD}" "${VKINC}/vulkan/vulkan.h" "${COMPAT_A}"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p} (run without --skip-mesa)" >&2; exit 1; }
done

# --- libdrm-phoenix snapshot ---------------------------------------------------------------
echo "== libdrm-phoenix snapshot"
LD_PREFIX="${out}/libdrm-prefix"
rm -rf "${LD_PREFIX}"
mkdir -p "${LD_PREFIX}/lib"
cp -a "${libdrm_src_prefix}/include" "${LD_PREFIX}/"
cp -a "${libdrm_src_prefix}/lib/libdrm.a" "${LD_PREFIX}/lib/"
{
	echo "source: ${libdrm_src_prefix}"
	sha256sum "${LD_PREFIX}/lib/libdrm.a"
} > "${out}/libdrm-snapshot.txt"
sed 's/^/  /' "${out}/libdrm-snapshot.txt"

# --- vkcube source ------------------------------------------------------------------------
echo "== Vulkan-Tools ${VT_TAG} + $(ls "${here}"/patches/vkcube/*.patch | wc -l) patch(es)"
vt="${out}/Vulkan-Tools"
if [ ! -d "${vt}/.git" ]; then
	git clone -q --depth 1 --branch "${VT_TAG}" "${VT_URL}" "${vt}"
fi
[ "$(git -C "${vt}" rev-parse HEAD)" = "${VT_COMMIT}" ] || { echo "build.sh: ${vt} is not at ${VT_COMMIT}" >&2; exit 1; }
git -C "${vt}" checkout -q -f "${VT_COMMIT}"
git -C "${vt}" clean -qfdx
for p in "${here}"/patches/vkcube/*.patch; do
	echo "  apply $(basename "${p}")"
	git -C "${vt}" apply --whitespace=nowarn "${p}"
done

# --- compile ------------------------------------------------------------------------------
echo "== compile"
TFLAGS=(-mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics -ffunction-sections -fdata-sections
	--sysroot="${S}/" -B"${S}/lib/")
obj="${out}/obj"
rm -rf "${obj}"
mkdir -p "${obj}"
: > "${out}/cc.log"
# vkcube: display WSI only (no xcb/xlib/wayland), SPIR-V shaders are the committed .inc files.
"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wno-unused-function "${TFLAGS[@]}" -DVK_USE_PLATFORM_DISPLAY_KHR \
	-I"${vt}/cube" -I"${VKINC}" -c "${vt}/cube/cube.c" -o "${obj}/cube.o" 2>> "${out}/cc.log" \
	|| { grep -A3 'error' "${out}/cc.log" | head -40; exit 1; }
"${TC}-gcc" -O2 -g -std=gnu11 -Wall -Wextra -Werror "${TFLAGS[@]}" -I"${here}/phxvk" -I"${VKINC}" \
	-c "${here}/phxvk/phxvk_loader.c" -o "${obj}/phxvk_loader.o" 2>> "${out}/cc.log" \
	|| { cat "${out}/cc.log"; exit 1; }
echo "  cube.c + phxvk_loader.c: $(grep -c 'warning:' "${out}/cc.log" || true) warning line(s) (${out}/cc.log)"

# --- link ---------------------------------------------------------------------------------
# The ICD archive goes in WHOLE: Mesa's generated dispatch tables reference the driver's
# entry points (v3dv_*, wsi_*, vk_common_*) as WEAK symbols, and a weak reference never
# pulls an archive member -- linked normally, every entry point whose object nothing else
# references would silently resolve to NULL. --gc-sections then drops what the tables do
# not reach. -Wl,--wrap=mmap: v3dv maps BOs with mmap(render_fd, MMAP_BO token);
# libdrm-phoenix's __wrap_mmap resolves the tokens (M3 §2.6).
echo "== link"
"${PHXCXX}" "${TFLAGS[@]}" -static -Wl,--gc-sections -Wl,-z,max-page-size=0x1000 -Wl,--wrap=mmap \
	-Wl,-Map,"${out}/vkcube-drm.map" -o "${out}/vkcube-drm" "${obj}/cube.o" "${obj}/phxvk_loader.o" \
	-Wl,--whole-archive "${ICD}" -Wl,--no-whole-archive \
	-Wl,--start-group "${LD_PREFIX}/lib/libdrm.a" "${COMPAT_A}" "${B}/lib/libz.a" -Wl,--end-group -lm \
	> "${out}/link.log" 2>&1 || { head -60 "${out}/link.log"; exit 1; }
"${TC}-strip" -o "${out}/vkcube-drm.stripped" "${out}/vkcube-drm"
echo "  ${out}/vkcube-drm: $(stat -c %s "${out}/vkcube-drm") bytes; stripped $(stat -c %s "${out}/vkcube-drm.stripped") bytes"
[ -s "${out}/link.log" ] && sed 's/^/  link: /' "${out}/link.log" | head -20

# --- verification -------------------------------------------------------------------------
echo "== verify"
"${TC}-size" "${out}/vkcube-drm" | sed 's/^/  /'
und="$("${TC}-nm" -u "${out}/vkcube-drm" || true)"
echo "  undefined symbols (nm -u): $(grep -c . <<< "${und}" || true)"
[ -n "${und}" ] && sed 's/^/    /' <<< "${und}" | head -20
syms="$("${TC}-nm" "${out}/vkcube-drm")"
missing=0
for s in phxvk_GetInstanceProcAddr vk_icdGetInstanceProcAddr vk_icdNegotiateLoaderICDInterfaceVersion \
		v3dv_CreateInstance vk_common_QueueSubmit2 v3dv_queue_driver_submit v3dv_CmdDraw v3dv_CreateGraphicsPipelines v3dv_AllocateMemory \
		v3dv_GetMemoryFdKHR wsi_CreateDisplayPlaneSurfaceKHR wsi_GetPhysicalDeviceDisplayPropertiesKHR \
		wsi_CreateSwapchainKHR wsi_QueuePresentKHR wsi_AcquireNextImage2KHR \
		__wrap_mmap drmPhoenixMmap drm_phoenix_ioctl drmModeAtomicCommit drmCrtcQueueSequence; do
	if grep -qE " [TtWw] ${s}\$" <<< "${syms}"; then echo "  symbol ${s}: yes"; else echo "  symbol ${s}: NO"; missing=1; fi
done
strs="$(strings -a "${out}/vkcube-drm.stripped")"
for s in 'phxvk: new GPU lane' 'V3D %d.%d.%d.%d' VK_KHR_display VK_KHR_swapchain 'Failed to drmModeObjectGetProperties' \
		'libdrm-phoenix:' DRMPHX_TRACE /dev/dri/card0 /dev/dri/renderD128 /dev/dri/card1 /kmsbuf 'brcm,2711-v3d' \
		'Cannot find a plane compatible with the display'; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  strings '${s}': ${n}"
done
bad=0
for s in 'v3d-winsys:' 'v3da-winsys:' phoenix_v3d_ioctl peek_next_scanout v3d-srv V3DV_PHOENIX /dev/fb0 RPI4FB_GETMODE \
		pl_phoenix PL_VkHostAllocator vkquake; do
	n=$(grep -cF -- "${s}" <<< "${strs}" || true)
	echo "  old-lane string '${s}': ${n}"
	[ "${n}" = 0 ] || bad=1
done
if grep -qE ' [Tt] dlopen$' <<< "${syms}"; then
	echo "  note: dlopen is linked (referenced by: $(grep -B1 -E '^ +0x[0-9a-f]+ +dlopen$' "${out}/vkcube-drm.map" | head -1 | tr -s ' ' | cut -c1-120))"
fi
{
	echo "vkcube-drm build $(date -Is)"
	echo "Vulkan-Tools ${VT_TAG} ${VT_COMMIT} + $(cd "${here}/patches/vkcube" && ls *.patch | tr '\n' ' ')"
	echo "Mesa patch set: $(cat "${MESA_OUT}/mesa-src.stamp" 2>/dev/null || echo '?') ($(ls "${root}"/tools/gpu-lane/mesa-drm/patches/mesa/*.patch | wc -l) patches)"
	echo "ICD: $(sha256sum "${ICD}" | cut -c1-16) ${ICD}"
	sed -n 's/^\([0-9a-f]\{16\}\).*  \(.*\)$/libdrm.a: \1 \2/p' "${out}/libdrm-snapshot.txt"
	echo "libdrm source: ${libdrm_src_prefix}"
	(cd "${out}" && sha256sum vkcube-drm vkcube-drm.stripped)
} > "${out}/BUILD-INFO.txt"
sed 's/^/  /' "${out}/BUILD-INFO.txt"
[ "${bad}" = 0 ] || { echo "build.sh: old-lane strings present" >&2; exit 1; }
[ "${missing}" = 0 ] || { echo "build.sh: expected symbols missing" >&2; exit 1; }
[ -z "${und}" ] || { echo "build.sh: undefined symbols" >&2; exit 1; }
echo "done"
