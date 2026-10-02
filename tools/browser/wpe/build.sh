#!/usr/bin/env bash
#
# Browser track D / milestones B4-B5: WPE WebKit 2.54.0 (PORT=WPE, WPEPlatform: Wayland +
# headless) cross-built STATIC for aarch64-phoenix as ONE multi-call program, wpe-browser:
# the UI process, the WebProcess and the NetworkProcess are the same ELF (docs/browser/PLAN.md
# decision 2). See README.md next to this script.
#
# Builds on track C (tools/browser/jsc): the same pinned tarball, host ruby, OS(PHOENIX)
# patches (jsc/patches/webkit/0001-0005, applied first), mimalloc configuration and libphoenix
# compat shims; this directory adds patches/webkit/0006+ and the launcher.
#
# Developed outside the ports framework (PLAN decision 3). Reads the tree (sysroot, toolchain,
# the installed ports) and writes only into <out> (and <dl>); never into the repository,
# .buildroot or sources/.
#
#   <out>/jsc-tools/          track C's build.sh state: host ruby (when the host has none)
#   <out>/deps/               ONE prefix holding copies of exactly the libraries WPE links
#                             (from the tree's ports: gtk3_wayland's GLib 2.88 + views,
#                             webkit_deps, icu, harfbuzz(_icu), openssl, libepoxy, mesa_drm
#                             headers), its pkg-config wrapper and the CMake toolchain file
#   <out>/src/webkit/         tarball + jsc patches + wpe patches (its own git repo)
#   <out>/compat/             the libphoenix compat shims still needed (jsc/compat)
#   <out>/webkit-build/       the CMake/Ninja tree
#   <out>/wpe-browser         unstripped (addr2line); <out>/wpe-browser-stripped (stage this)
#
# Usage: tools/browser/wpe/build.sh --out <dir> [--dl <dir>] [-j N]
#            [--stage deps|compat|extract|configure|build|all] [--mesa-variant gles|wayland] [--clean]
#   --mesa-variant  which mesa_drm build is linked (default gles: EGL on GBM + surfaceless,
#                   which is what the WebProcess uses with SHM buffers; wayland adds the
#                   EGL Wayland platform the UI process would use for dma-buf, B7)
#   environment: WEBKIT_SRC=<patched tree> builds an existing tree (development);
#                PHX_TREE=<_build/<target>> reads another tree than the repository's;
#                PHX_WEBKIT_DEPS=<webkit_deps install>, PHX_ICU_PREFIX=<prefix with icu +
#                harfbuzz_icu> take those two from a scratch ports build.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
jsc="${root}/tools/browser/jsc"
out=""
dl=""
jobs=8
clean=0
stage=all
mesa_variant=gles
while [ $# -gt 0 ]; do
	case "$1" in
		--out) shift; out="${1:?--out needs a directory}" ;;
		--out=*) out="${1#--out=}" ;;
		--dl) shift; dl="${1:?--dl needs a directory}" ;;
		--dl=*) dl="${1#--dl=}" ;;
		-j) shift; jobs="${1:?-j needs a number}" ;;
		-j*) jobs="${1#-j}" ;;
		--clean) clean=1 ;;
		--stage) shift; stage="${1:?}" ;;
		--stage=*) stage="${1#--stage=}" ;;
		--mesa-variant) shift; mesa_variant="${1:?}" ;;
		--mesa-variant=*) mesa_variant="${1#--mesa-variant=}" ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
[ "${jobs}" -le 8 ] || { echo "build.sh: -j ${jobs}: at most 8 WebKit jobs on this host (scripts/heavy-build.sh)" >&2; jobs=8; }
[ -n "${out}" ] || { echo "build.sh: --out <dir> is required (the build is ~15 GB; keep it out of the repo)" >&2; exit 2; }
case "${out}" in /*) ;; *) out="${PWD}/${out}" ;; esac
[ -n "${dl}" ] || dl="${out}/dl"
case "${dl}" in /*) ;; *) dl="${PWD}/${dl}" ;; esac
case "${out}/" in "${root}/"*) echo "build.sh: --out must be outside the repository" >&2; exit 2 ;; esac
case "${mesa_variant}" in gles | wayland) ;; *) echo "build.sh: --mesa-variant gles|wayland" >&2; exit 2 ;; esac

if [ "${clean}" = 1 ]; then
	rm -rf "${out:?}/src" "${out}/webkit-build" "${out}/deps" "${out}/compat" "${out}"/*.stamp
	echo "cleaned ${out} (kept ${dl} and ${out}/jsc-tools)"
	exit 0
fi

B="${PHX_TREE:-${root}/.buildroot/_build/aarch64a72-generic-rpi4b}"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"
VP="${B}/versioned-ports"
GTK="${VP}/gtk3_wayland-3.24.52"
WKD="${PHX_WEBKIT_DEPS:-${VP}/webkit_deps-2.54.0}"
# ICU + harfbuzz(+icu) install into the tree's flat prefix; PHX_ICU_PREFIX: another such prefix
ICUP="${PHX_ICU_PREFIX:-${B}}"
OSSL="${VP}/openssl-3.5.9"
WLP="${VP}/wayland_phoenix-1.24.0"
EPOXY="${VP}/libepoxy-1.5.10"
MESA="${VP}/mesa_drm-26.2.0"
V="${out}/deps"
VN="${out}/deps.new"   # assembled here, then synced into V by content (unchanged files keep
                       # their mtime, so refreshing the view does not rebuild WebKit)

for p in "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-g++" "${TC}-gcc-ar" "${TC}-strip" "${jsc}/build.sh"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done
for t in cmake ninja perl python3 gperf gcc curl sha256sum git pkg-config glib-mkenums glib-compile-resources \
		wayland-scanner gdbus-codegen; do
	command -v "${t}" > /dev/null || { echo "build.sh: host tool ${t} not found" >&2; exit 1; }
done

mkdir -p "${out}" "${dl}" "${out}/src"
log() { echo "[wpe-build] $*"; }

# --- track C: the pinned tarball and host ruby ------------------------------------------------
WEBKIT_TARBALL="wpewebkit-2.54.0.tar.xz"
WEBKIT_SHA256="efa9bcc3cb891c2d88f50eec710d9ccee71cbdf1040420361eb98c17355eb452"
RUBY=""
stage_jsc_tools() {
	"${jsc}/build.sh" --out "${out}/jsc-tools" --dl "${dl}" --stage fetch > "${out}/jsc-tools.log" 2>&1 \
		|| { echo "build.sh: tools/browser/jsc/build.sh --stage fetch failed, see ${out}/jsc-tools.log" >&2; exit 1; }
	"${jsc}/build.sh" --out "${out}/jsc-tools" --dl "${dl}" -j "${jobs}" --stage ruby >> "${out}/jsc-tools.log" 2>&1 \
		|| { echo "build.sh: tools/browser/jsc/build.sh --stage ruby failed, see ${out}/jsc-tools.log" >&2; exit 1; }
	RUBY="$(command -v ruby || true)"
	[ -n "${RUBY}" ] || RUBY="${out}/jsc-tools/host/bin/ruby"
	[ -x "${RUBY}" ] || { echo "build.sh: no host ruby (${RUBY})" >&2; exit 1; }
	echo "${WEBKIT_SHA256}  ${dl}/${WEBKIT_TARBALL}" | sha256sum -c --quiet - \
		|| { echo "build.sh: ${WEBKIT_TARBALL}: sha256 mismatch" >&2; exit 1; }
	log "host ruby: ${RUBY}"
}

# --- sources: tarball + jsc patches + wpe patches, one commit each ----------------------------
webkit_src_dir() { echo "${WEBKIT_SRC:-${out}/src/webkit}"; }
stage_extract() {
	[ -z "${WEBKIT_SRC:-}" ] || { log "WEBKIT_SRC=${WEBKIT_SRC} (not extracted)"; return; }
	local dir="${out}/src/webkit" stamp p
	stamp="$( { echo "${WEBKIT_SHA256}"; cat "${jsc}"/patches/webkit/*.patch "${here}"/patches/webkit/*.patch; } | sha256sum | cut -c1-16)"
	if [ "$(cat "${dir}.stamp" 2>/dev/null || true)" = "${stamp}" ]; then
		return
	fi
	log "extract ${WEBKIT_TARBALL}"
	rm -rf "${dir}" "${dir}.tmp"
	mkdir -p "${dir}.tmp"
	tar -xf "${dl}/${WEBKIT_TARBALL}" -C "${dir}.tmp" --strip-components=1
	mv "${dir}.tmp" "${dir}"
	# its own git repository: `git apply` inside another repository's tree silently skips paths
	git -C "${dir}" init -q
	git -C "${dir}" add -A
	git -C "${dir}" -c user.name=build -c user.email=build@invalid commit -q -m "${WEBKIT_TARBALL}"
	for p in "${jsc}"/patches/webkit/*.patch "${here}"/patches/webkit/*.patch; do
		log "apply $(basename "$(dirname "$(dirname "$(dirname "${p}")")")")/$(basename "${p}")"
		git -C "${dir}" apply --whitespace=nowarn "${p}"
		git -C "${dir}" add -A
		git -C "${dir}" -c user.name=build -c user.email=build@invalid commit -q -m "$(basename "${p}")"
	done
	echo "${stamp}" > "${dir}.stamp"
	rm -f "${out}/configure.stamp"
}

# --- target flags (as track C) ------------------------------------------------------------------
TFLAGS="-mcpu=cortex-a72 -mtune=cortex-a72 -mno-outline-atomics --sysroot=${S}/ -B${S}/lib/ -ffunction-sections -fdata-sections"

# --- compat shims: track C's set, only those this sysroot's libphoenix still lacks -------------
stage_compat() {
	# assembled in compat.new and synced by content: an unchanged header keeps its mtime (fenv.h
	# reaches almost every WebKit object through SIMDe)
	local I="${S}/usr/include" cn="${out}/compat.new" ci="${out}/compat.new/include" cdefs="" syms C="${jsc}/compat"
	rm -rf "${cn}"
	mkdir -p "${ci}/sys"
	syms="$("${TC}-nm" -g --defined-only "${S}/lib/libphoenix.a" 2>/dev/null || true)"
	has_sym() { grep -qE " [TW] $1\$" <<< "${syms}"; }
	need() {
		local m="$1"; shift
		if "$@"; then cdefs="${cdefs} -D${m}=1"; log "  shim ${m#PHX_COMPAT_}"; else cdefs="${cdefs} -D${m}=0"; fi
	}
	log "compat objects"
	need PHX_COMPAT_PTHREAD_GETATTR_NP eval '! has_sym pthread_getattr_np'
	need PHX_COMPAT_SEM eval '! has_sym sem_init'
	need PHX_COMPAT_MADVISE eval '! has_sym madvise'
	# msync: wayland_phoenix's compat library (linked for memfd_create) defines it already
	cdefs="${cdefs} -DPHX_COMPAT_MSYNC=0"
	[ -f "${I}/semaphore.h" ] || cp "${C}/include/semaphore.h" "${ci}/"
	[ -f "${I}/uchar.h" ] || cp "${C}/include/uchar.h" "${ci}/"
	if grep -q '#error' "${I}/fenv.h" 2> /dev/null || [ ! -f "${I}/fenv.h" ]; then
		cp "${C}/include/fenv.h" "${ci}/"
	else
		# libphoenix's <fenv.h> is real (b20), but the toolchain's libstdc++ was built without
		# _GLIBCXX_HAVE_FENV_H, so its <fenv.h>/<cfenv> wrappers hide it from C++ (SIMDe in WTF):
		# reach the C header by path
		printf '%s\n' "/* tools/browser/wpe/build.sh: libphoenix <fenv.h> past libstdc++'s empty wrapper */" \
			"#include \"${I}/fenv.h\"" > "${ci}/fenv.h"
	fi
	grep -q 'define LC_MESSAGES' "${I}/locale.h" || cp "${C}/include/locale.h" "${ci}/"
	grep -qE 'define UINT8_MAX +\(0xffU\)' "${I}/stdint.h" && cp "${C}/include/stdint.h" "${ci}/"
	cp "${C}/include/sys/mman.h" "${ci}/sys/"
	log "  compat headers: $(cd "${ci}" && find . -name '*.h' | sort | tr '\n' ' ')"
	"${TC}-gcc" -O2 ${TFLAGS} -Wall -Wextra -Werror ${cdefs} -isystem "${ci}" -c "${C}/phoenix-jsc-compat.c" \
		-o "${cn}/phoenix-jsc-compat.o"
	"${TC}-gcc" -O2 ${TFLAGS} -Wall -Wextra -Werror -c "${here}/compat/phoenix-wpe-compat.c" -o "${cn}/phoenix-wpe-compat.o"
	mkdir -p "${out}/compat"
	rsync -rc --delete "${cn}/" "${out}/compat/"
	rm -rf "${cn}"
}

# --- dependency prefix ---------------------------------------------------------------------------
# One prefix (<out>/deps) of COPIES: the tree's flat _build/<target>/{include,lib} holds every
# port's headers (incl. the old ports GLib 2.56) and must never reach WebKit's compile lines;
# copies also keep a running WebKit build stable while the tree is rebuilt.
cp_into() {  # cp_into <dst-subdir> <src...>: dereferencing copy
	local d="${VN}/$1"; shift
	mkdir -p "${d}"
	cp -rL "$@" "${d}/"
}
pc_into() {  # pc_into <src.pc...>: into <V>/lib/pkgconfig (pkg-config --define-prefix resolves prefix)
	local f
	mkdir -p "${VN}/lib/pkgconfig"
	for f in "$@"; do
		[ -f "${f}" ] || { echo "build.sh: missing ${f}" >&2; exit 1; }
		cp -L "${f}" "${VN}/lib/pkgconfig/"
	done
}
stage_deps() {
	local req
	for req in "${GTK}/destdir/usr/lib/libglib-2.0.a" "${WKD}/destdir/usr/lib/libsoup-3.0.a" "${ICUP}/lib/libicuuc.a" \
			"${ICUP}/lib/libharfbuzz-icu.a" "${OSSL}/lib/libcrypto.a" "${EPOXY}/lib/libepoxy.a" "${MESA}/${mesa_variant}/link-gles.txt"; do
		[ -f "${req}" ] || { echo "build.sh: ${req} missing: build the ports (gtk3_wayland webkit_deps icu harfbuzz_icu openssl libepoxy mesa_drm) first" >&2; exit 1; }
	done
	log "deps: ${V} (webkit_deps ${WKD}, icu/harfbuzz ${ICUP})"
	rm -rf "${VN}"
	mkdir -p "${VN}/include" "${VN}/lib/pkgconfig" "${VN}/share/pkgconfig"
	local G="${GTK}/destdir/usr" GD="${GTK}/deps" W="${WKD}/destdir/usr" f n

	# GLib 2.88 / GIO (gtk3_wayland) + the private views GLib's .pc files require
	cp_into include "${G}/include/glib-2.0" "${G}/include/gio-unix-2.0"
	cp_into lib "${G}/lib/glib-2.0"
	for n in glib-2.0 gio-2.0 gobject-2.0 gmodule-2.0 gthread-2.0 pcre2-8; do cp_into lib "${G}/lib/lib${n}.a"; done
	cp_into include "${G}/include/pcre2.h"
	for n in glib-2.0 gio-2.0 gio-unix-2.0 gobject-2.0 gmodule-2.0 gmodule-no-export-2.0 gmodule-export-2.0 gthread-2.0 libpcre2-8; do
		pc_into "${G}/lib/pkgconfig/${n}.pc"
	done
	for n in zlib libffi expat libpng16 libjpeg freetype2 fontconfig; do
		[ -d "${GD}/${n}/include" ] && cp_into include "${GD}/${n}/include"/*
		cp_into lib "${GD}/${n}/lib"/*.a
		for f in "${GD}/${n}/lib/pkgconfig"/*.pc; do [ -e "${f}" ] && pc_into "${f}"; done
	done
	# libpng installs png.h both in include/libpng16/ and (as links) in include/: CMake's FindPNG
	# looks only for the latter
	cp_into include "${GD}/libpng16/include/libpng16"/*.h
	# libintl/libiconv stand-ins GLib links against (its headers stay out: resolv.h etc. would
	# shadow libphoenix's)
	cp_into lib "${GD}/sys/lib"/*.a
	cp_into include "${GD}/sys/include/libintl.h" "${GD}/sys/include/iconv.h"
	# Wayland 1.24 (client, server: FindWayland wants both, cursor, egl) + wayland-protocols +
	# xkbcommon + wlphx-compat (memfd_create over shmsrv, epoll & co.): the wayland_phoenix port
	local WL="${WLP}/prefix"
	cp_into include "${WL}/include"/wayland-*.h "${WL}/include/xkbcommon" "${WL}/include/wayland-protocols" "${WL}/include/linux" "${WL}/include/evdev"
	for n in wayland-client wayland-server wayland-cursor wayland-egl xkbcommon wlphx-compat; do
		cp_into lib "${WL}/lib/lib${n}.a"
	done
	for n in wayland-client wayland-server wayland-cursor wayland-egl wayland-egl-backend xkbcommon wlphx-compat; do
		pc_into "${WL}/lib/pkgconfig/${n}.pc"
	done
	cp_into share "${WL}/share/wayland-protocols"
	mkdir -p "${VN}/share/pkgconfig"
	sed "s|${WL}|${V}|g" "${WL}/share/pkgconfig/wayland-protocols.pc" > "${VN}/share/pkgconfig/wayland-protocols.pc"

	# webkit_deps: libsoup 3, glib-networking (OpenSSL), psl, nghttp2, brotli, woff2, webp, xml2, xslt
	cp_into include "${W}/include"/*
	cp_into lib "${W}/lib"/*.a
	cp_into lib/gio "${W}/lib/gio/modules"
	for f in "${W}/lib/pkgconfig"/*.pc; do pc_into "${f}"; done
	for f in "${WKD}/deps/sqlite3/lib/pkgconfig"/*.pc; do pc_into "${f}"; done
	cp_into lib "${WKD}/deps/sqlite3/lib"/*.a
	[ -d "${WKD}/deps/sqlite3/include" ] && cp_into include "${WKD}/deps/sqlite3/include"/*

	# ICU 78.3 (port icu, data filtered for WebKit) + harfbuzz 14.4 with hb-icu (harfbuzz_icu):
	# the tree's flat prefix, file by file
	cp_into include "${ICUP}/include/unicode" "${ICUP}/include/harfbuzz"
	for n in icuuc icui18n icudata harfbuzz harfbuzz-icu; do cp_into lib "${ICUP}/lib/lib${n}.a"; done
	for n in icu-uc icu-i18n harfbuzz harfbuzz-icu; do pc_into "${ICUP}/lib/pkgconfig/${n}.pc"; done

	# OpenSSL 3.5 (PAL digests, patch 0006; glib-networking)
	cp_into include "${OSSL}/include/openssl"
	cp_into lib "${OSSL}/lib/libssl.a" "${OSSL}/lib/libcrypto.a"
	for f in "${OSSL}/lib/pkgconfig"/*.pc; do pc_into "${f}"; done

	# libepoxy (static-EGL dispatch through eglGetProcAddress) + Mesa's EGL/GLES/KHR headers
	cp_into include "${EPOXY}/include/epoxy"
	cp_into lib "${EPOXY}/lib/libepoxy.a"
	local mp="${MESA}/${mesa_variant}/prefix/include"
	cp_into include "${mp}/EGL" "${mp}/GLES2" "${mp}/GLES3" "${mp}/KHR"
	printf '%s\n' "prefix=${V}" "includedir=\${prefix}/include" "libdir=\${prefix}/lib" "epoxy_has_glx=0" \
		"epoxy_has_egl=1" "epoxy_has_wgl=0" "" "Name: epoxy" \
		"Description: libepoxy 1.5.10, static-EGL dispatch (EGL/GLES from mesa_drm ${mesa_variant}, linked by the program)" \
		"Version: 1.5.10" "Libs: -L\${libdir} -lepoxy" "Cflags: -I\${includedir} -DEGL_NO_X11" > "${VN}/lib/pkgconfig/epoxy.pc"

	# every .pc: prefix = this view (pkg-config --define-prefix also does this), and the
	# source roots it may name spelled as the view
	for f in "${VN}"/lib/pkgconfig/*.pc; do
		# (-pthread: the Phoenix gcc rejects it; pthreads are libphoenix)
		sed -i -e "s|^prefix=.*|prefix=${V}|" -e "s|${GTK}/deps/[a-z0-9_-]*|${V}|g" -e "s|${WKD}/deps/sqlite3|${V}|g" \
			-e "s|${WKD}|${V}|g" -e "s|${OSSL}|${V}|g" -e "s|${ICUP}|${V}|g" -e "s|${WLP}/prefix|${V}|g" -e "s|${B}|${V}|g" \
			-e "s/ -pthread\b//g" -e "s/^\(Cflags\|Libs\): -pthread\b/\1:/" "${f}"
	done

	cat > "${VN}/pkg-config" <<EOF
#!/bin/sh
# pkg-config over the WPE dependency prefix only (tools/browser/wpe/build.sh)
export PKG_CONFIG_LIBDIR=${V}/lib/pkgconfig:${V}/share/pkgconfig
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR
exec /usr/bin/pkg-config --static --define-prefix "\$@"
EOF
	chmod +x "${VN}/pkg-config"

	# the static link closure the multi-call program adds to WebKit's own interface
	# (appended to every C++ link: CMAKE_CXX_STANDARD_LIBRARIES in stage_configure)
	# (Mesa's archives are copied too: link-gles.txt names them in the port's build tree).
	# Everything but Mesa's gallium (whole-archive) goes into ONE --start-group: CMake's find
	# modules name only each package's main archive, so the static closure (libsoup -> nghttp2,
	# psl, brotli; GIO -> libintl, libiconv, libresolv; epoxy -> Mesa's EGL; fontconfig ->
	# freetype; ...) is resolved by the group, not by pkg-config's order.
	mkdir -p "${VN}/mesa"
	local a group=()
	{
		while IFS= read -r a; do
			case "${a}" in
				--whole-archive\ *)
					cp -L "${a#--whole-archive }" "${VN}/mesa/"
					echo "-Wl,--whole-archive"
					echo "${V}/mesa/$(basename "${a#--whole-archive }")"
					echo "-Wl,--no-whole-archive" ;;
				*)
					[ -f "${a}" ] || { echo "build.sh: ${a} (from link-gles.txt) missing" >&2; exit 1; }
					cp -L "${a}" "${VN}/mesa/"
					group+=("${V}/mesa/$(basename "${a}")") ;;
			esac
		done < "${MESA}/${mesa_variant}/link-gles.txt"
		echo "-Wl,--start-group"
		printf '%s\n' "${group[@]}"
		(cd "${VN}/lib" && find . -maxdepth 1 -name '*.a' | sort | sed "s|^\./|${V}/lib/|")
		echo "${V}/lib/gio/modules/libgioopenssl.a"
		echo "-Wl,--end-group"
	} > "${VN}/link-extra.txt"
	mkdir -p "${V}"
	rsync -rc --delete "${VN}/" "${V}/"
	rm -rf "${VN}"
	log "deps: $(find "${V}/lib" -name '*.a' | wc -l) archives, $(ls "${V}/lib/pkgconfig" | wc -l) pkg-config files"
	"${V}/pkg-config" --modversion glib-2.0 gio-unix-2.0 libsoup-3.0 icu-uc harfbuzz-icu wayland-client \
		wayland-protocols xkbcommon epoxy libxml-2.0 libxslt libwebp sqlite3 libcrypto freetype2 fontconfig \
		libpng16 2>&1 | tr '\n' ' ' | sed 's/^/[wpe-build] versions: /'
	echo
}

# --- WebKit configuration ------------------------------------------------------------------------
# README.md "Configuration" explains every value.
WPE_CMAKE_OPTS=(
	-DPORT=WPE
	-DCMAKE_BUILD_TYPE=Release
	-DDEVELOPER_MODE=OFF
	-DENABLE_STATIC_JSC=ON
	# JavaScriptCore as track C: asm LLInt, no JIT/WASM, mimalloc
	-DUSE_SYSTEM_MALLOC=OFF
	-DUSE_MIMALLOC=ON
	-DENABLE_JIT=OFF
	-DENABLE_DFG_JIT=OFF
	-DENABLE_FTL_JIT=OFF
	-DENABLE_C_LOOP=OFF
	-DENABLE_WEBASSEMBLY=OFF
	-DENABLE_SAMPLING_PROFILER=OFF
	-DENABLE_REMOTE_INSPECTOR=OFF
	-DENABLE_API_TESTS=OFF
	-DENABLE_LAYOUT_TESTS=OFF
	-DENABLE_MINIBROWSER=OFF
	-DENABLE_FUZZILLI=OFF
	# WPEPlatform: Wayland window + headless; no DRM platform, no libwpe legacy API
	-DENABLE_WPE_PLATFORM=ON
	-DENABLE_WPE_PLATFORM_WAYLAND=ON
	-DENABLE_WPE_PLATFORM_HEADLESS=ON
	-DENABLE_WPE_PLATFORM_DRM=OFF
	-DENABLE_WPE_LEGACY_API=OFF
	-DENABLE_WPE_QT_API=OFF
	-DUSE_GBM=OFF
	-DUSE_LIBDRM=OFF
	-DENABLE_GPU_PROCESS=OFF
	# media (B8 later)
	-DENABLE_VIDEO=OFF
	-DENABLE_WEB_AUDIO=OFF
	-DENABLE_WEB_RTC=OFF
	-DENABLE_MEDIA_SOURCE=OFF
	-DENABLE_MEDIA_STREAM=OFF
	-DENABLE_MEDIA_RECORDER=OFF
	-DENABLE_MEDIA_SESSION=OFF
	-DENABLE_WEB_CODECS=OFF
	-DENABLE_ENCRYPTED_MEDIA=OFF
	-DENABLE_THUNDER=OFF
	-DUSE_GSTREAMER=OFF
	-DENABLE_SPEECH_SYNTHESIS=OFF
	# graphics: no WebGL/WebXR/Vulkan yet (B7)
	-DENABLE_WEBGL=OFF
	-DENABLE_WEBXR=OFF
	-DUSE_VULKAN=OFF
	-DUSE_SKIA_OPENTYPE_SVG=ON
	# features with a dependency we do not ship
	-DENABLE_XSLT=ON
	-DENABLE_WEB_CRYPTO=OFF
	-DUSE_AVIF=OFF
	-DUSE_JPEGXL=OFF
	-DUSE_LCMS=OFF
	-DUSE_WOFF2=ON
	-DUSE_LIBHYPHEN=OFF
	-DENABLE_SPELLCHECK=OFF
	-DENABLE_GAMEPAD=OFF
	-DUSE_ATK=OFF
	-DUSE_LIBBACKTRACE=OFF
	-DUSE_SYSPROF_CAPTURE=OFF
	-DENABLE_JOURNALD_LOG=OFF
	-DENABLE_BUBBLEWRAP_SANDBOX=OFF
	-DENABLE_WEBDRIVER=OFF
	-DENABLE_DOCUMENTATION=OFF
	-DENABLE_INTROSPECTION=OFF
	-DENABLE_COG=OFF
	-DENABLE_PDFJS=ON
	-DUSE_EXTERNAL_HOLEPUNCH=OFF
)

stage_configure() {
	[ -n "${RUBY}" ] || stage_jsc_tools
	[ -x "${V}/pkg-config" ] || stage_deps
	[ -f "${out}/compat/phoenix-wpe-compat.o" ] || stage_compat
	stage_extract
	local wsrc wb="${out}/webkit-build" tcf="${out}/phoenix-aarch64.cmake" wflags extra
	wsrc="$(webkit_src_dir)"
	wflags="${TFLAGS} -isystem ${out}/compat/include"
	# unifdef runs on the BUILD machine (it strips the other ports' #if blocks from the public
	# API headers): WebKit's bundled copy would be cross-compiled with the target toolchain, and
	# generate-api-header.py then silently installs the headers unprocessed
	mkdir -p "${out}/host-tools"
	gcc -std=gnu99 -O2 -o "${out}/host-tools/unifdef" "${wsrc}/Source/ThirdParty/unifdef/unifdef.c"
	sed -e "s|@HERE@|${jsc}|g" -e "s|@TC@|${TC}|g" -e "s|@SYSROOT@|${S}|g" -e "s|@TFLAGS@|${wflags}|g" \
		-e "s|@ICU@|${V}|g" "${jsc}/cmake/phoenix-aarch64.cmake.in" > "${tcf}"
	echo "set(PKG_CONFIG_EXECUTABLE \"${V}/pkg-config\" CACHE FILEPATH \"\")" >> "${tcf}"
	# -L<deps>/lib: FindWayland hands bare library names (WAYLAND_LIBRARIES) to the link.
	# Mesa's archives (whole-archive gallium first), glib-networking's module, the wayland_phoenix
	# compat library with the --wrap options it and libdrm-phoenix are built for, and the
	# libphoenix shims; libicudata again at the very end (static link order, as track C), and
	# libphoenix's libm BEFORE g++'s implicit -lstdc++: the toolchain's libstdc++.a has its own
	# hypotf (math_stubs_float.o), which otherwise collides with libm's (harfbuzz_icu, labwc).
	# All of it goes at the END of every C++ link (CMAKE_CXX_STANDARD_LIBRARIES): CMake places a
	# target's own libraries BEFORE the link interface of WebCore & co., so a group given as
	# target libraries would be scanned before the archives that need it (libwebp -> sharpyuv).
	extra="$(tr '\n' ' ' < "${V}/link-extra.txt")-Wl,-u,__wrap_close -Wl,-u,__wrap_write ${V}/lib/libwlphx-compat.a -Wl,--wrap=close -Wl,--wrap=write -Wl,--wrap=mmap -Wl,--wrap=ioctl"
	if [ -f "${wb}/build.ninja" ] && ! cmp -s "${tcf}" "${wb}.toolchain"; then
		log "WebKit: toolchain changed, rebuilding from scratch"
		rm -rf "${wb}"
	fi
	log "WebKit: configure"
	mkdir -p "${wb}"
	# pkg_check_modules() caches its results: a changed dependency view needs a fresh cache
	if [ -f "${wb}/CMakeCache.txt" ] && ! cmp -s "${V}/link-extra.txt" "${wb}.deps"; then rm -f "${wb}/CMakeCache.txt"; fi
	PATH="$(dirname "${RUBY}"):${PATH}" cmake -G Ninja -S "${wsrc}" -B "${wb}" \
		-DCMAKE_TOOLCHAIN_FILE="${tcf}" "${WPE_CMAKE_OPTS[@]}" \
		-DICU_ROOT="${V}" \
		-DCMAKE_INSTALL_PREFIX=/usr \
		-DPHOENIX_BROWSER_DIR="${here}/launcher" \
		-DUSE_SYSTEM_UNIFDEF=ON -DUNIFDEF_EXECUTABLE="${out}/host-tools/unifdef" \
		-DCMAKE_CXX_STANDARD_LIBRARIES="${out}/compat/phoenix-jsc-compat.o ${out}/compat/phoenix-wpe-compat.o ${extra} ${V}/lib/libicudata.a ${S}/lib/libm.a" \
		-DCMAKE_EXE_LINKER_FLAGS="-L${V}/lib -Wl,-z,max-page-size=0x1000 -Wl,-z,stack-size=8388608 -Wl,--gc-sections" \
		> "${out}/webkit-configure.log" 2>&1 \
		|| { grep -E 'CMake (Error|Warning)' -A6 "${out}/webkit-configure.log" | head -60 >&2; echo "build.sh: WebKit configure failed, see ${out}/webkit-configure.log" >&2; exit 1; }
	cp "${tcf}" "${wb}.toolchain"
	cp "${V}/link-extra.txt" "${wb}.deps"
	grep -E '^--  (ENABLE|USE)_' "${out}/webkit-configure.log" > "${out}/webkit-features.txt" || true
	log "WebKit: configured; public options ON: $(grep -E ' ON$' "${out}/webkit-features.txt" | awk '{print $2}' | tr '\n' ' ')"
	touch "${out}/configure.stamp"
}

stage_build() {
	[ -n "${RUBY}" ] || stage_jsc_tools
	[ -f "${out}/configure.stamp" ] || stage_configure
	local wb="${out}/webkit-build" t0=${SECONDS}
	log "WebKit: build wpe-browser (-j${jobs})"
	# through scripts/heavy-build.sh: one heavy build on the host at a time (flock), -j capped by
	# MemAvailable (2 GB per WebKit job) and the whole build in a MemoryMax scope
	PATH="$(dirname "${RUBY}"):${PATH}" "${root}/scripts/heavy-build.sh" -j "${jobs}" -- \
		ninja -C "${wb}" -j '{JOBS}' WPEBrowser > "${out}/webkit-build.log" 2>&1 \
		|| { grep -E 'error:|FAILED:' "${out}/webkit-build.log" | head -30 >&2; echo "build.sh: WebKit build failed, see ${out}/webkit-build.log" >&2; exit 1; }
	log "WebKit: built in $((SECONDS - t0)) s"
	cp "${wb}/bin/wpe-browser" "${out}/wpe-browser"
	"${TC}-strip" -o "${out}/wpe-browser-stripped" "${out}/wpe-browser"
	log "wpe-browser: $(stat -c %s "${out}/wpe-browser-stripped") bytes stripped ($(stat -c %s "${out}/wpe-browser") unstripped)"
	# what the static link must (not) contain
	local syms s
	syms="$("${TC}-nm" "${out}/wpe-browser")"
	if grep -q ' malloc_common$' <<< "${syms}"; then
		echo "build.sh: wpe-browser links libphoenix's malloc (stdlib/malloc_dl.o) besides mimalloc" >&2
		exit 1
	fi
	# the roles, the static TLS backend, shm, Mesa's EGL (surfaceless) behind epoxy, WPEPlatform,
	# ICU, HarfBuzz-ICU, OpenSSL (PAL digests), libsoup, the local compat
	for s in _ZN6WebKit14WebProcessMainEiPPc _ZN6WebKit18NetworkProcessMainEiPPc g_io_openssl_load \
			g_tls_backend_get_default memfd_create eglGetProcAddress dri2_initialize_surfaceless \
			epoxy_static_proc_address wpe_display_wayland_new wpe_display_headless_new ubrk_open_78 \
			hb_icu_script_to_script SHA256_Init soup_session_get_feature nextafterf mi_malloc; do
		grep -qE " [TtWD] ${s}\$" <<< "${syms}" || { echo "build.sh: wpe-browser has no ${s}" >&2; exit 1; }
	done
	# mimalloc IS malloc (the override), as in track C
	[ "$(grep -E ' T (malloc|mi_malloc)$' <<< "${syms}" | awk '{print $1}' | sort -u | wc -l)" = 1 ] \
		|| { echo "build.sh: malloc is not mimalloc's" >&2; exit 1; }
	log "wpe-browser: symbol checks passed"
}

stage_all() {
	stage_jsc_tools
	stage_deps
	stage_compat
	stage_extract
	stage_configure
	stage_build
}

case "${stage}" in
	deps) stage_deps ;;
	compat) stage_compat ;;
	extract) stage_jsc_tools; stage_extract ;;
	configure) stage_configure ;;
	build) stage_build ;;
	all) stage_all ;;
	*) echo "build.sh: unknown stage ${stage}" >&2; exit 2 ;;
esac
