#!/usr/bin/env bash
#
# Browser track C / milestone B3: JavaScriptCore (WTF + JSC, WebKit's JSCOnly port) cross-built
# STATIC for aarch64-phoenix, producing the `jsc` shell. See README.md next to this script and
# docs/browser/PLAN.md (B3 is the go/no-go gate for the WebKit browser).
#
# Developed outside the ports framework (PLAN decision 3): a standalone scratch build against the
# tree sysroot. Writes only into <out> and <dl>; never into the repo, .buildroot or sources/.
#
#   <dl>/                     pinned tarballs (sha256 below), shared across runs
#   <out>/host/               host tools built here (ruby, for WebKit's offlineasm)
#   <out>/icu-host/           ICU built for the build machine (its tools build the data)
#   <out>/icu/                ICU cross-built for Phoenix (static, data linked in)
#   <out>/src/webkit/         the WebKit tarball, extracted + patches/webkit/*.patch applied
#   <out>/webkit-build/       the CMake/Ninja tree
#   <out>/jsc                 the shell, unstripped (addr2line); <out>/jsc-stripped (stage this)
#   with --jit (browser B9): <out>/src/webkit-jit (+ patches/webkit-jit/*.patch), <out>/webkit-build-jit,
#                             <out>/jsc-jit, <out>/jsc-jit-stripped (and <out>/jsc-host-jit)
#   <out>/mallocrate[-mimalloc] malloc-rate micro-benchmark, libphoenix vs mimalloc (bench/)
#
# Host tools: cmake >= 3.20, ninja, perl, python3, gperf, gcc/g++ (for the host ICU and ruby).
# Ruby is built from a pinned tarball when the host has none (WebKit needs it, Ubuntu here has none).
#
# Usage: tools/browser/jsc/build.sh --out <dir> [--dl <dir>] [-j N] [--clean]
#            [--stage fetch|ruby|icu|webkit|host-jsc|all] [--host-jsc] [--icu-prefix <dir>] [--jit]
#   --host-jsc    also build an x86-64 Linux jsc with the same JSC options (reference numbers)
#   --jit         Baseline JIT + DFG + FTL (and the YARR JIT) instead of the LLInt-only B3 build:
#                 applies patches/webkit-jit/ on top and builds into separate *-jit trees/outputs
#   --icu-prefix  link an existing Phoenix ICU 78.3 install (<dir>/include, <dir>/lib/libicu*.a:
#                 track A1's `icu` port, whose data is filtered to 11 MB) instead of building the
#                 private one (full 33 MB data) in <out>/icu. Never point it at the tree's
#                 _build/<target> prefix: its include/ holds every port's headers.
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../../.." && pwd)"
out=""
dl=""
jobs=12
clean=0
stage=all
host_jsc=0
icu_prefix=""
jit=0
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
		--host-jsc) host_jsc=1 ;;
		--jit) jit=1 ;;
		--icu-prefix) shift; icu_prefix="${1:?--icu-prefix needs a directory}" ;;
		--icu-prefix=*) icu_prefix="${1#--icu-prefix=}" ;;
		*) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
[ -n "${out}" ] || { echo "build.sh: --out <dir> is required (the build is ~6 GB; keep it out of the repo)" >&2; exit 2; }
case "${out}" in /*) ;; *) out="${PWD}/${out}" ;; esac
[ -n "${dl}" ] || dl="${out}/dl"
case "${dl}" in /*) ;; *) dl="${PWD}/${dl}" ;; esac
case "${out}/" in "${root}/"*) echo "build.sh: --out must be outside the repository" >&2; exit 2 ;; esac
ICU="${icu_prefix:-${out}/icu}"

if [ "${clean}" = 1 ]; then
	rm -rf "${out:?}/src" "${out}/webkit-build" "${out}/webkit-host-build" "${out}/webkit-build-jit" \
		"${out}/webkit-host-build-jit" "${out}/icu" "${out}/icu-host" "${out}/icu-build" "${out}/icu-host-build" \
		"${out}"/*.stamp
	echo "cleaned ${out} (kept ${dl} and ${out}/host)"
	exit 0
fi

B="${root}/.buildroot/_build/aarch64a72-generic-rpi4b"
S="${B}/sysroot"
TC="${root}/.toolchain/aarch64-phoenix/bin/aarch64-phoenix"

# name|file|url|sha256
PKGS=(
	"webkit|wpewebkit-2.54.0.tar.xz|https://wpewebkit.org/releases/wpewebkit-2.54.0.tar.xz|efa9bcc3cb891c2d88f50eec710d9ccee71cbdf1040420361eb98c17355eb452"
	"icu|icu4c-78.3-sources.tgz|https://github.com/unicode-org/icu/releases/download/release-78.3/icu4c-78.3-sources.tgz|3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0"
	"ruby|ruby-3.4.7.tar.gz|https://cache.ruby-lang.org/pub/ruby/3.4/ruby-3.4.7.tar.gz|23815a6d095696f7919090fdc3e2f9459b2c83d57224b2e446ce1f5f7333ef36"
	"libyaml|yaml-0.2.5.tar.gz|https://github.com/yaml/libyaml/releases/download/0.2.5/yaml-0.2.5.tar.gz|c642ae9b75fee120b2d96c712538bd2cf283228d2337df2cf2988e3c02678ef4"
)

for p in "${S}/lib/libphoenix.a" "${TC}-gcc" "${TC}-g++" "${TC}-gcc-ar" "${TC}-strip"; do
	[ -e "${p}" ] || { echo "build.sh: missing ${p}" >&2; exit 1; }
done
for t in cmake ninja perl python3 gperf gcc g++ curl sha256sum git; do
	command -v "${t}" > /dev/null || { echo "build.sh: host tool ${t} not found" >&2; exit 1; }
done

mkdir -p "${out}" "${dl}" "${out}/src"
log() { echo "[jsc-build] $*"; }

# --- sources ---------------------------------------------------------------------------------
pkg_field() {  # name field(1=file 2=url 3=sha)
	local rec
	for rec in "${PKGS[@]}"; do
		if [ "${rec%%|*}" = "$1" ]; then
			IFS='|' read -r _ f u s <<< "${rec}"
			case "$2" in 1) echo "${f}" ;; 2) echo "${u}" ;; 3) echo "${s}" ;; esac
			return
		fi
	done
	echo "build.sh: no package $1" >&2
	exit 1
}
fetch() {
	local file url sum
	file="$(pkg_field "$1" 1)"; url="$(pkg_field "$1" 2)"; sum="$(pkg_field "$1" 3)"
	if [ ! -f "${dl}/${file}" ]; then
		log "fetch ${file}"
		curl -sSfL -o "${dl}/${file}.part" "${url}"
		mv "${dl}/${file}.part" "${dl}/${file}"
	fi
	echo "${sum}  ${dl}/${file}" | sha256sum -c --quiet - || { echo "build.sh: ${file}: sha256 mismatch" >&2; exit 1; }
}
# extract_patched <dir name> <package> <patchdir>... -> ${out}/src/<dir name>: its own git repo,
# one commit per patch, the patch directories applied in the order given
extract_patched() {
	local name="$1" pkg="$2" file sum dir stamp p pd
	shift 2
	file="$(pkg_field "${pkg}" 1)"; sum="$(pkg_field "${pkg}" 3)"
	dir="${out}/src/${name}"
	stamp="$( { echo "${sum}"; for pd in "$@"; do cat "${pd}"/*.patch 2>/dev/null || true; done; } | sha256sum | cut -c1-16)"
	if [ "$(cat "${dir}.stamp" 2>/dev/null || true)" = "${stamp}" ]; then
		return
	fi
	log "extract ${file}"
	rm -rf "${dir}" "${dir}.tmp"
	mkdir -p "${dir}.tmp"
	tar -xf "${dl}/${file}" -C "${dir}.tmp" --strip-components=1
	mv "${dir}.tmp" "${dir}"
	# Its own git repository: `git apply` inside a directory of ANOTHER repository silently
	# skips every path (weston-drm M6 section 5.1).
	git -C "${dir}" init -q
	git -C "${dir}" add -A
	git -C "${dir}" -c user.name=build -c user.email=build@invalid commit -q -m "${file}"
	for pd in "$@"; do
		for p in "${pd}"/*.patch; do
			[ -e "${p}" ] || continue
			log "apply $(basename "${pd}")/$(basename "${p}")"
			git -C "${dir}" apply --whitespace=nowarn "${p}"
			git -C "${dir}" add -A
			git -C "${dir}" -c user.name=build -c user.email=build@invalid commit -q -m "$(basename "${pd}")/$(basename "${p}")"
		done
	done
	echo "${stamp}" > "${dir}.stamp"
	rm -f "${out}/${name}"*.stamp
}

stage_fetch() {
	local n
	for n in webkit icu ruby libyaml; do fetch "${n}"; done
}

# --- host ruby (WebKit's offlineasm, generators) ----------------------------------------------
RUBY="$(command -v ruby || true)"
stage_ruby() {
	if [ -n "${RUBY}" ] && [ "${RUBY}" != "${out}/host/bin/ruby" ]; then
		log "host ruby: ${RUBY}"
		return
	fi
	if [ ! -x "${out}/host/bin/ruby" ]; then
		fetch ruby
		fetch libyaml
		# libyaml for ruby's psych (WebKit's generators read YAML); the host has no headers.
		log "build host libyaml"
		rm -rf "${out}/host-src/libyaml"
		mkdir -p "${out}/host-src/libyaml"
		tar -xzf "${dl}/$(pkg_field libyaml 1)" -C "${out}/host-src/libyaml" --strip-components=1
		(cd "${out}/host-src/libyaml" && ./configure --prefix="${out}/host" --disable-shared --enable-static \
			CFLAGS="-O2 -fPIC" > "${out}/libyaml-build.log" 2>&1 && make -j"${jobs}" >> "${out}/libyaml-build.log" 2>&1 \
			&& make install >> "${out}/libyaml-build.log" 2>&1) \
			|| { echo "build.sh: host libyaml build failed, see ${out}/libyaml-build.log" >&2; exit 1; }
		log "build host ruby"
		rm -rf "${out}/host-src/ruby"
		mkdir -p "${out}/host-src/ruby"
		tar -xzf "${dl}/$(pkg_field ruby 1)" -C "${out}/host-src/ruby" --strip-components=1
		(cd "${out}/host-src/ruby" && ./configure --prefix="${out}/host" --disable-install-doc \
			--disable-install-rdoc --without-gmp --with-libyaml-dir="${out}/host" > "${out}/ruby-build.log" 2>&1 \
			&& make -j"${jobs}" >> "${out}/ruby-build.log" 2>&1 && make install >> "${out}/ruby-build.log" 2>&1) \
			|| { echo "build.sh: host ruby build failed, see ${out}/ruby-build.log" >&2; exit 1; }
	fi
	RUBY="${out}/host/bin/ruby"
	log "host ruby: ${RUBY} ($("${RUBY}" --version))"
}

# --- target flags --------------------------------------------------------------------------------
# -mno-outline-atomics as the tree (target/aarch64.mk): no runtime LSE detection on Phoenix.
# NOT -mstrict-align: EL0 alignment checking is off (kernel hal/aarch64/_init.S, SCTLR.A=0) and the
# LLInt's offlineasm code does unaligned bytecode loads anyway; the tree uses it for device memory.
TFLAGS="-mcpu=cortex-a72 -mtune=cortex-a72 -mno-outline-atomics --sysroot=${S}/ -B${S}/lib/ -ffunction-sections -fdata-sections"
COMPAT="${here}/compat"

# --- ICU (A1 picks the same 78.3; this private copy goes away when the ICU port lands) ----------
stage_icu() {
	fetch icu
	extract_patched icu icu "${here}/patches/icu"
	local isrc="${out}/src/icu/source"
	if [ ! -f "${out}/icu-host.stamp" ]; then
		log "ICU: host build"
		rm -rf "${out}/icu-host-build" "${out}/icu-host"
		mkdir -p "${out}/icu-host-build"
		(cd "${out}/icu-host-build" && CC=gcc CXX=g++ CFLAGS=-O2 CXXFLAGS=-O2 "${isrc}/configure" \
			--prefix="${out}/icu-host" --disable-shared --enable-static --disable-tests --disable-samples \
			> "${out}/icu-host-build.log" 2>&1 && make -j"${jobs}" >> "${out}/icu-host-build.log" 2>&1 \
			&& make install >> "${out}/icu-host-build.log" 2>&1) \
			|| { echo "build.sh: host ICU failed, see ${out}/icu-host-build.log" >&2; exit 1; }
		touch "${out}/icu-host.stamp"
	fi
	if [ ! -f "${out}/icu.stamp" ]; then
		log "ICU: cross build"
		rm -rf "${out}/icu-build" "${out}/icu"
		mkdir -p "${out}/icu-build"
		# icu_cv_host_frag: configure maps an unknown OS to mh-unknown (a stub that fails);
		# mh-linux is the generic GNU-toolchain fragment and is all a static build needs.
		(cd "${out}/icu-build" && "${isrc}/configure" \
			--host=aarch64-phoenix --build=x86_64-pc-linux-gnu --prefix="${out}/icu" \
			--with-cross-build="${out}/icu-host-build" icu_cv_host_frag=mh-linux \
			--disable-shared --enable-static --with-data-packaging=static --disable-dyload \
			--disable-tests --disable-samples --disable-extras --disable-layoutex --disable-icuio --disable-tools \
			CC="${TC}-gcc" CXX="${TC}-g++" AR="${TC}-gcc-ar" RANLIB="${TC}-gcc-ranlib" \
			CFLAGS="-O2 ${TFLAGS}" CXXFLAGS="-O2 ${TFLAGS}" CPPFLAGS="-DU_HAVE_TZSET=1" \
			> "${out}/icu-build.log" 2>&1 && make -j"${jobs}" >> "${out}/icu-build.log" 2>&1 \
			&& make install >> "${out}/icu-build.log" 2>&1) \
			|| { echo "build.sh: Phoenix ICU failed, see ${out}/icu-build.log" >&2; exit 1; }
		touch "${out}/icu.stamp"
	fi
	log "ICU: $(ls "${out}/icu/lib"/*.a | xargs -n1 basename | tr '\n' ' ')"
}

# --- WebKit (JSCOnly port) ------------------------------------------------------------------------
# JSC configuration (README.md explains each choice):
#   ENABLE_JIT=OFF on ARM64 keeps the asm LLInt (PlatformEnable.h: ENABLE_C_LOOP is 0 whenever
#   CPU(ARM64)); no executable memory is needed. WASM, sampling profiler, remote inspector OFF.
#   USE_MIMALLOC=ON (WebKit's vendored mimalloc) instead of libpas (needs madvise + PROT_NONE
#   reserve-then-commit) and instead of USE_SYSTEM_MALLOC (libphoenix malloc, a mutex syscall
#   per call when threaded); on Phoenix the same mimalloc also overrides malloc/new.
#   --jit (B9): the Baseline JIT, DFG and FTL (B3/Air, no LLVM) and the YARR JIT; the executable
#   pool is one RWX mapping made at JSC initialization (README "JIT"). WebAssembly is compiled in
#   because WebKit 2.54's B3 and FTL do not build without it, but stays off at run time on Phoenix
#   (patches/webkit-jit, Options.cpp; --useWasm=true turns it on).
if [ "${jit}" = 1 ]; then
	JIT_CMAKE_OPTS=(-DENABLE_JIT=ON -DENABLE_DFG_JIT=ON -DENABLE_FTL_JIT=ON -DENABLE_WEBASSEMBLY=ON)
	sfx=-jit
else
	JIT_CMAKE_OPTS=(-DENABLE_JIT=OFF -DENABLE_DFG_JIT=OFF -DENABLE_FTL_JIT=OFF -DENABLE_WEBASSEMBLY=OFF)
	sfx=""
fi
JSC_CMAKE_OPTS=(
	-DPORT=JSCOnly
	-DENABLE_STATIC_JSC=ON
	-DUSE_SYSTEM_MALLOC=OFF
	-DUSE_MIMALLOC=ON
	"${JIT_CMAKE_OPTS[@]}"
	-DENABLE_C_LOOP=OFF
	-DENABLE_SAMPLING_PROFILER=OFF
	-DENABLE_REMOTE_INSPECTOR=OFF
	-DENABLE_API_TESTS=OFF
	-DENABLE_FUZZILLI=OFF
	-DUSE_LIBBACKTRACE=OFF
	-DDEVELOPER_MODE=OFF
	-DCMAKE_BUILD_TYPE=Release
)
webkit_src_dir() {
	if [ -n "${WEBKIT_SRC:-}" ]; then   # development: an already-patched tree
		echo "${WEBKIT_SRC}"
	else
		echo "${out}/src/webkit${sfx}"
	fi
}
webkit_extract() {
	[ -z "${WEBKIT_SRC:-}" ] || return 0
	if [ "${jit}" = 1 ]; then
		extract_patched webkit-jit webkit "${here}/patches/webkit" "${here}/patches/webkit-jit"
	else
		extract_patched webkit webkit "${here}/patches/webkit"
	fi
}
stage_webkit() {
	stage_ruby
	fetch webkit
	webkit_extract
	local wsrc wb="${out}/webkit-build${sfx}" tcf="${out}/phoenix-aarch64.cmake" wflags
	wsrc="$(webkit_src_dir)"
	if [ -z "${icu_prefix}" ]; then
		[ -f "${ICU}/lib/libicuuc.a" ] || stage_icu
	fi
	for p in libicuuc.a libicui18n.a libicudata.a; do
		[ -f "${ICU}/lib/${p}" ] || { echo "build.sh: ${ICU}/lib/${p} missing" >&2; exit 1; }
	done
	log "ICU: ${ICU}"

	# Compat shims (compat/, README "Local shims"): only those the sysroot's libphoenix still
	# lacks, so this builds both before and after the libphoenix B1 work lands. The headers go
	# into <out>/compat/include, which is searched (-isystem) before the sysroot's.
	log "compat objects"
	local I="${S}/usr/include" ci="${out}/compat/include" cdefs="" syms
	rm -rf "${out}/compat"
	mkdir -p "${ci}/sys"
	syms="$("${TC}-nm" -g --defined-only "${S}/lib/libphoenix.a" 2>/dev/null || true)"
	has_sym() { grep -qE " [TW] $1\$" <<< "${syms}"; }
	need() {  # need <macro> <condition...>: record the shim decision
		local m="$1"; shift
		if "$@"; then cdefs="${cdefs} -D${m}=1"; log "  shim ${m#PHX_COMPAT_}"; else cdefs="${cdefs} -D${m}=0"; fi
	}
	need PHX_COMPAT_PTHREAD_GETATTR_NP eval '! has_sym pthread_getattr_np'
	need PHX_COMPAT_SEM eval '! has_sym sem_init'
	need PHX_COMPAT_MADVISE eval '! has_sym madvise'
	need PHX_COMPAT_MSYNC eval '! has_sym msync'
	[ -f "${I}/semaphore.h" ] || cp "${COMPAT}/include/semaphore.h" "${ci}/"
	[ -f "${I}/uchar.h" ] || cp "${COMPAT}/include/uchar.h" "${ci}/"
	# <fenv.h> must work from C++ (WTF's SIMDe), not only from C: the toolchain's libstdc++ was
	# configured while libphoenix had no <fenv.h> (_GLIBCXX_HAVE_FENV_H unset), so its <fenv.h>
	# wrapper includes nothing even once libphoenix has a real one. Keep the shim until a C++
	# compile sees FE_TONEAREST and fesetround (a toolchain rebuild against the b20 sysroot).
	if ! printf '#include <cfenv>\nint phx_fenv_probe(void) { return std::fesetround(FE_TONEAREST); }\n' |
			"${TC}-g++" ${TFLAGS} -x c++ -fsyntax-only - 2> /dev/null; then
		cp "${COMPAT}/include/fenv.h" "${ci}/"
	fi
	grep -q 'define LC_MESSAGES' "${I}/locale.h" || cp "${COMPAT}/include/locale.h" "${ci}/"
	grep -qE 'define UINT8_MAX +\(0xffU\)' "${I}/stdint.h" && cp "${COMPAT}/include/stdint.h" "${ci}/"
	cp "${COMPAT}/include/sys/mman.h" "${ci}/sys/"   # self-guarding: MAP_FILE, msync, madvise
	log "  compat headers: $(cd "${ci}" && find . -name '*.h' | sort | tr '\n' ' ')"
	"${TC}-gcc" -O2 ${TFLAGS} -Wall -Wextra -Werror ${cdefs} -isystem "${ci}" -c "${COMPAT}/phoenix-jsc-compat.c" \
		-o "${out}/compat/phoenix-jsc-compat.o"

	wflags="${TFLAGS} -isystem ${ci}"
	sed -e "s|@HERE@|${here}|g" -e "s|@TC@|${TC}|g" -e "s|@SYSROOT@|${S}|g" -e "s|@TFLAGS@|${wflags}|g" \
		-e "s|@ICU@|${ICU}|g" "${here}/cmake/phoenix-aarch64.cmake.in" > "${tcf}"

	# libicudata.a again at the end: CMake's FindICU lists ICU::data before ICU::uc, and a static
	# link resolves left to right (icudt78_dat is referenced from libicuuc's udata.o).
	# A changed toolchain file (flags, compat set, ICU) needs a fresh tree: CMake caches the flags.
	if [ -f "${wb}/build.ninja" ] && ! cmp -s "${tcf}" "${wb}.toolchain"; then
		log "WebKit: toolchain changed, rebuilding from scratch"
		rm -rf "${wb}"
	fi
	if [ ! -f "${wb}/build.ninja" ]; then
		log "WebKit: configure"
		mkdir -p "${wb}"
		# 4 KiB segment alignment as the tree (target/aarch64.mk); 8 MiB main-thread stack
		# (PT_GNU_STACK p_memsz, read by process_load64; the aarch64 default is 1 MiB, SIZE_USTACK).
		PATH="$(dirname "${RUBY}"):${PATH}" cmake -G Ninja -S "${wsrc}" -B "${wb}" \
			-DCMAKE_TOOLCHAIN_FILE="${tcf}" "${JSC_CMAKE_OPTS[@]}" \
			-DICU_ROOT="${ICU}" \
			-DCMAKE_CXX_STANDARD_LIBRARIES="${out}/compat/phoenix-jsc-compat.o ${ICU}/lib/libicudata.a" \
			-DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,max-page-size=0x1000 -Wl,-z,stack-size=8388608" \
			> "${out}/webkit${sfx}-configure.log" 2>&1 \
			|| { echo "build.sh: WebKit configure failed, see ${out}/webkit${sfx}-configure.log" >&2; exit 1; }
		cp "${tcf}" "${wb}.toolchain"
	fi
	log "WebKit: build jsc (-j${jobs})"
	local t0=${SECONDS}
	PATH="$(dirname "${RUBY}"):${PATH}" ninja -C "${wb}" -j"${jobs}" jsc > "${out}/webkit${sfx}-build.log" 2>&1 \
		|| { echo "build.sh: WebKit build failed, see ${out}/webkit${sfx}-build.log" >&2; exit 1; }
	log "WebKit: built in $((SECONDS - t0)) s"
	cp "${wb}/bin/jsc" "${out}/jsc${sfx}"
	"${TC}-strip" -o "${out}/jsc${sfx}-stripped" "${out}/jsc${sfx}"
	log "jsc${sfx}: $(stat -c %s "${out}/jsc${sfx}-stripped") bytes stripped ($(stat -c %s "${out}/jsc${sfx}") unstripped)"

	# The malloc-rate micro-benchmark, against libphoenix's malloc and against the very mimalloc
	# object linked into jsc (with the same override and _malloc_init hook).
	local mimalloc_obj="${wb}/Source/bmalloc/mimalloc/mimalloc/CMakeFiles/mimalloc-obj.dir/src/static.c.o"
	"${TC}-gcc" -O2 ${TFLAGS} -Wall -Wextra -Werror -Wl,-z,max-page-size=0x1000 \
		"${here}/bench/mallocrate.c" -o "${out}/mallocrate"
	"${TC}-gcc" -O2 ${TFLAGS} -Wall -Wextra -Werror -Wl,-z,max-page-size=0x1000 -DMALLOCRATE_IMPL='"mimalloc"' \
		"${here}/bench/mallocrate.c" "${mimalloc_obj}" "${out}/compat/phoenix-jsc-compat.o" -o "${out}/mallocrate-mimalloc"
	# Neither binary may carry libphoenix's allocator next to mimalloc (malloc_dl.o's state).
	local b
	for b in "jsc${sfx}" mallocrate-mimalloc; do
		if "${TC}-nm" "${out}/${b}" | grep -q ' malloc_common$'; then
			echo "build.sh: ${b} links libphoenix's malloc (stdlib/malloc_dl.o) besides mimalloc" >&2
			exit 1
		fi
	done
	"${TC}-strip" "${out}/mallocrate" "${out}/mallocrate-mimalloc"
	log "mallocrate, mallocrate-mimalloc: built"
}

# --- host reference jsc (x86-64 Linux, same JSC options, the host's ICU) ----------------------------
stage_host_jsc() {
	stage_ruby
	fetch webkit
	webkit_extract
	local wsrc hb="${out}/webkit-host-build${sfx}"
	wsrc="$(webkit_src_dir)"
	if [ ! -f "${hb}/build.ninja" ]; then
		log "host jsc: configure"
		mkdir -p "${hb}"
		PATH="$(dirname "${RUBY}"):${PATH}" CC=gcc CXX=g++ cmake -G Ninja -S "${wsrc}" -B "${hb}" \
			"${JSC_CMAKE_OPTS[@]}" > "${out}/webkit-host${sfx}-configure.log" 2>&1 \
			|| { echo "build.sh: host jsc configure failed, see ${out}/webkit-host${sfx}-configure.log" >&2; exit 1; }
	fi
	log "host jsc: build (-j${jobs})"
	PATH="$(dirname "${RUBY}"):${PATH}" ninja -C "${hb}" -j"${jobs}" jsc > "${out}/webkit-host${sfx}-build.log" 2>&1 \
		|| { echo "build.sh: host jsc build failed, see ${out}/webkit-host${sfx}-build.log" >&2; exit 1; }
	cp "${hb}/bin/jsc" "${out}/jsc-host${sfx}"
	log "host jsc: ${out}/jsc-host${sfx}"
}

stage_all() {
	stage_fetch
	stage_ruby
	[ -n "${icu_prefix}" ] || stage_icu
	stage_webkit
	[ "${host_jsc}" = 0 ] || stage_host_jsc
}

case "${stage}" in
	fetch) stage_fetch ;;
	ruby) stage_ruby ;;
	icu) stage_icu ;;
	webkit) stage_webkit ;;
	host-jsc) stage_host_jsc ;;
	all) stage_all ;;
	*) echo "build.sh: unknown stage ${stage}" >&2; exit 2 ;;
esac
