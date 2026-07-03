#!/usr/bin/env bash
#
# Phoenix-RTOS RPi4 — showcase-app build orchestrator.
#
# Builds the "showcase" layer that sits on top of the base SD image: the V3D
# GPU / GL / Vulkan stack, the Quake engines, the X11 server + apps, and the
# extra userland ports (dillo, mc, nano, ...). It runs the per-piece build
# scripts under tools/* in the correct dependency order, produces the GPU/quake
# static archives into tools/.gpu-libs, and stages every app binary + its data
# files into a rootfs staging tree so the ext2 SD image bundles them.
#
# This replaces the previous web of out-of-band manual `tools/*` invocations
# with one documented, idempotent, fail-loud flow.
#
# ---------------------------------------------------------------------------
# PHASES (two-phase by design — see the timing note below):
#
#   gpu    : host Mesa meson builds + the 5 GPU/quake archive builds ->
#            tools/.gpu-libs/*.a. These archives MUST exist before the main
#            `build.sh project` stage runs, because the rpi4-quake / rpi4-vkquake
#            _user components link them and are staged into the image by that
#            stage. (rebuild-rpi4b-fast.sh --with-showcase runs this BEFORE it
#            invokes build.sh, and passes GPU_LIBS so the Makefiles find them.)
#
#   stage  : port libs (libiconv/libffi/ncurses/glib2/fltk) + X11 lib stack +
#            X11 apps (xterm/xedit/xcalc/wmaker/...) + userland ports
#            (dillo/mc/nano), staging their binaries + data into the rootfs tree
#            $SHOWCASE_STAGE_DIR. This MUST run AFTER `build.sh` has populated
#            _fs/<target>/root (the fs/core/ports/project stages), because those
#            stages repopulate the tree and would clobber anything pre-staged.
#
#   all    : gpu then stage (for a standalone, out-of-band full run against an
#            already-populated rootfs tree).
#
# The two-phase split is why the base `--variant sd` flow calls this script at
# two distinct points rather than as a single pre-step.
# ---------------------------------------------------------------------------
#
# Host dependencies (installed by scripts/bootstrap-linux-host.sh; see the
# "showcase build deps" block there): meson (>= 1.4, via the mesa pyenv),
# ninja-build, python3-mako, libdrm-dev (Vulkan host build), glslang-tools
# (vkquake real SPIR-V; GLQuake does NOT need it), plus the base autotools /
# pkg-config / gperf already present for the port autoconf builds.
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt

set -uo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

target="${RPI4B_TARGET:-aarch64a72-generic-rpi4b}"
buildroot="${RPI4B_BUILDROOT:-${repo_root}/.buildroot}"
gpu_libs="${repo_root}/tools/.gpu-libs"
mesa_dir="${repo_root}/external/mesa"

# Rootfs staging tree the ext2 image consumes. Default matches
# build-rpi4b-rootfs-ext2.sh's RPI4B_ROOTFS_TREE. When wired into --variant sd
# the caller points this at _fs/<target>/root (post-build.sh) so apps land in
# the ext2 image; a standalone run may point it at the NFS export instead.
stage_dir="${SHOWCASE_STAGE_DIR:-${buildroot}/_fs/${target}/root}"

# Host Mesa build dirs (reused by the tools/v3d-driver-port/*.py scripts via
# their hardcoded HOSTBUILD paths). Kept in /tmp to match those scripts.
mesa_v3d_build="${MESA_V3D_BUILD:-/tmp/mesa-v3d-build}"
mesa_v3dv_build="${MESA_V3DV_BUILD:-/tmp/mesa-v3dv-build}"
mesa_pyenv="${MESA_PYENV:-/tmp/mesa-pyenv}"

phase="all"
force=0
skip_vulkan=0
skip_x11=0

usage() {
	cat <<'EOF'
Usage: build-showcase-apps.sh [--phase gpu|stage|all] [options]

Build the Phoenix-RTOS RPi4 showcase-app layer (GPU/GL/Vulkan + Quake, X11
server + apps, dillo/mc/nano) reproducibly, in dependency order.

Phases:
  --phase gpu     host Mesa builds + GPU/quake archives -> tools/.gpu-libs/*.a
                  (run BEFORE build.sh; archives are linked by the _user
                  rpi4-quake/rpi4-vkquake components)
  --phase stage   port libs + X11 libs + X11 apps + ports, staged into
                  $SHOWCASE_STAGE_DIR (run AFTER build.sh populated the rootfs)
  --phase all     gpu then stage (default; for a standalone full run)

Options:
  --force         rebuild archives even if present + fresh (default: skip
                  up-to-date archives for iteration speed)
  --skip-vulkan   skip the V3DV/vkquake path (GL/GLQuake still built)
  --skip-x11      skip the X11 lib stack + X11 apps (ports dillo/mc/nano still
                  attempted; note dillo needs fltk which needs the X11 libs, so
                  --skip-x11 also skips dillo)
  --stage-dir DIR override the rootfs staging tree (default:
                  $RPI4B_BUILDROOT/_fs/<target>/root)
  -h, --help      show this help

Environment:
  RPI4B_BUILDROOT, RPI4B_TARGET, SHOWCASE_STAGE_DIR, MESA_V3D_BUILD,
  MESA_V3DV_BUILD, MESA_PYENV
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--phase) shift; phase="${1:-}";;
		--force) force=1;;
		--skip-vulkan) skip_vulkan=1;;
		--skip-x11) skip_x11=1;;
		--stage-dir) shift; stage_dir="${1:-}";;
		-h|--help) usage; exit 0;;
		*) printf 'error: unknown option: %s\n' "$1" >&2; usage >&2; exit 2;;
	esac
	shift
done

case "$phase" in gpu|stage|all) ;; *) printf 'error: bad --phase %s\n' "$phase" >&2; exit 2;; esac

# --- logging helpers -------------------------------------------------------
c_hdr='\033[1;36m'; c_ok='\033[1;32m'; c_warn='\033[1;33m'; c_err='\033[1;31m'; c_off='\033[0m'
log()  { printf "${c_hdr}==> %s${c_off}\n" "$*"; }
ok()   { printf "${c_ok}[OK] %s${c_off}\n" "$*"; }
warn() { printf "${c_warn}[WARN] %s${c_off}\n" "$*"; }
die()  { printf "${c_err}[FAIL] %s${c_off}\n" "$*" >&2; exit 1; }

need_dir() { [ -d "$1" ] || die "missing required directory: $1 ($2)"; }
need_file() { [ -f "$1" ] || die "missing required file: $1 ($2)"; }

# archive_fresh <archive> — 0 (fresh) if the archive exists AND is newer than
# every input under $mesa_dir/{include,src} + the port-script tree. Cheap
# approximation: newer than the newest tools/*-port script. Used only to
# skip-if-fresh for iteration; --force bypasses.
archive_fresh() {
	local a="$1"
	[ -f "$a" ] || return 1
	[ "$force" = 0 ] || return 1
	local newest
	newest="$(find "${repo_root}/tools/v3d-driver-port" "${repo_root}/tools/quakespasm-port" \
		"${repo_root}/tools/vkquake-port" -type f -newer "$a" 2>/dev/null | head -1)"
	[ -z "$newest" ]
}

toolchain_bin="${repo_root}/.toolchain/aarch64-phoenix/bin"
need_dir "$toolchain_bin" "aarch64-phoenix toolchain — build it with scripts/build-phoenix-toolchain-linux.sh"

##############################################################################
# PHASE gpu — host Mesa builds + GPU/quake archives
##############################################################################

find_meson() {
	# Prefer the mesa pyenv's meson (>= 1.4, which apt's 1.3 on 24.04 isn't).
	if [ -x "${mesa_pyenv}/bin/meson" ]; then echo "${mesa_pyenv}/bin"; return 0; fi
	# Fall back to a PATH meson only if it's new enough.
	if command -v meson >/dev/null 2>&1; then
		local v; v="$(meson --version 2>/dev/null)"
		case "$v" in 1.[4-9]*|1.[1-9][0-9]*|[2-9]*) echo ""; return 0;; esac
	fi
	return 1
}

ensure_mesa_pyenv() {
	# Create /tmp/mesa-pyenv with meson>=1.4 + ninja + mako if no usable meson.
	if find_meson >/dev/null 2>&1; then return 0; fi
	log "Setting up Mesa build pyenv (meson>=1.4 + ninja + mako) at ${mesa_pyenv}"
	command -v uv >/dev/null 2>&1 || die "uv not found — run scripts/bootstrap-linux-host.sh"
	uv venv "${mesa_pyenv}" || die "uv venv failed"
	uv pip install --python "${mesa_pyenv}/bin/python" "meson>=1.4" ninja mako pyyaml packaging \
		|| die "uv pip install (meson/ninja/mako) failed"
}

mesa_meson_env_path() {
	local extra; extra="$(find_meson)"
	if [ -n "$extra" ]; then echo "${extra}:${PATH}"; else echo "${PATH}"; fi
}

# libdrm's <drm.h> lives under /usr/include/libdrm (via libdrm-dev), but Mesa's
# broadcom/perfcntrs TUs `#include <xf86drm.h>` -> `#include <drm.h>` without the
# libdrm pkg-config cflags on that meson target, so drm.h isn't found on a stock
# Ubuntu include path. Expose it via C_INCLUDE_PATH for the meson/ninja steps.
# (Harmless if the dir is absent.)
mesa_c_include_path() {
	local d
	for d in /usr/include/libdrm /usr/include/drm; do
		[ -f "$d/drm.h" ] && { echo "$d"; return 0; }
	done
	echo ""
}

setup_mesa_host_build() {
	# $1 = build dir, $2... = extra meson args (e.g. -Dvulkan-drivers=broadcom)
	local bdir="$1"; shift
	if [ -f "${bdir}/compile_commands.json" ] && [ "$force" = 0 ]; then
		ok "Mesa host build present: ${bdir}"
		return 0
	fi
	ensure_mesa_pyenv
	local mpath; mpath="$(mesa_meson_env_path)"
	local cinc; cinc="$(mesa_c_include_path)"
	log "meson setup ${bdir}"
	rm -rf "${bdir}"
	# -Dvulkan-drivers= (empty) is REQUIRED even for the GL build: the default
	# ['auto'] enables nouveau_vk, which pulls in a Rust build dependency (rustc)
	# we don't have or want. The v3dv call passes -Dvulkan-drivers=broadcom via
	# "$@", which (as the last occurrence) overrides this empty default.
	# -Dspirv-tools=disabled (SB-2): mesa's spirv-tools feature option defaults to
	# auto-detect. On a clean host with SPIRV-Tools present it enables
	# -DHAVE_SPIRV_TOOLS, making spirv_to_nir.c reference spirv_print_asm (defined
	# in vtn_debug.c, which is NOT in the v3dv aux-source closure) -> libv3dv link
	# fails "undefined reference to spirv_print_asm" -> rpi4-vkquake can't link.
	# We don't use SPIR-V validation/disasm, so force it off (harmless for GL too).
	( cd "${mesa_dir}" && PATH="${mpath}" C_INCLUDE_PATH="${cinc}${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}" \
		meson setup "${bdir}" \
		-Dgallium-drivers=v3d -Dvulkan-drivers= -Dplatforms= -Dglx=disabled \
		-Dspirv-tools=disabled \
		-Degl=disabled -Dgbm=disabled -Dvideo-codecs= -Dbuildtype=release "$@" ) \
		|| die "meson setup ${bdir} failed (missing host dep? see meson-logs/meson-log.txt)"
}

ninja_mesa_soft() {
	# $1 = build dir, $2... = ninja targets. Best-effort: a FULL host ninja of
	# the Mesa v3d tree always fails on x86 because v3d_resource.c has aarch64
	# `dc civac` cache asm that can't be assembled on the build host. The
	# cross-compile scripts (build-*-phoenix.py) don't need a full host build —
	# only compile_commands.json (from `meson setup`) plus a handful of GENERATED
	# sources, which they ninja themselves (ensure_generated_sources/gen_headers).
	# So we ninja only the targets we must materialize here and tolerate the
	# expected asm failure; the caller verifies the specific outputs it needs.
	local bdir="$1"; shift
	local mpath; mpath="$(mesa_meson_env_path)"
	local cinc; cinc="$(mesa_c_include_path)"
	log "ninja (best-effort) ${bdir} ${*}"
	( cd "${bdir}" && PATH="${mpath}" C_INCLUDE_PATH="${cinc}${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}" \
		ninja "$@" ) || warn "ninja ${bdir} returned non-zero (expected: x86 can't assemble v3d aarch64 cache asm; generated sources still materialized)"
}

phase_gpu() {
	log "PHASE gpu — GPU/GL/Vulkan + Quake archives -> ${gpu_libs}"
	need_dir "$mesa_dir" "external/mesa — clone it (git clone the mesa fork into external/mesa)"
	mkdir -p "$gpu_libs"

	# Export the libdrm include dir for the whole phase: the build-*.py scripts
	# invoke `ninja` on generated headers in the host trees, which hit the same
	# <drm.h> lookup. Also put the mesa pyenv's ninja/meson on PATH for them.
	local cinc mpath
	cinc="$(mesa_c_include_path)"
	[ -n "$cinc" ] && export C_INCLUDE_PATH="${cinc}${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
	mpath="$(mesa_meson_env_path)"; export PATH="${mpath}"

	# --- host Mesa GL build: `meson setup` only. This emits compile_commands.json
	#     (the per-file flag source) and the codegen scripts; the cross-compile
	#     build-*-phoenix.py scripts ninja the specific generated sources they
	#     need themselves. A full host ninja is neither required nor possible
	#     (x86 can't assemble v3d_resource.c's aarch64 cache asm). ---
	setup_mesa_host_build "${mesa_v3d_build}"
	need_file "${mesa_v3d_build}/compile_commands.json" "meson setup did not emit compile_commands.json"

	# Materialize ALL mesa generated sources up-front. On a COLD host mesa build the
	# per-script enumerated gen-header lists (build-*.py) are always incomplete — a
	# missing generated header silently drops objs (state_tracker st_*, v3d_screen, ...)
	# so archives link with undefined symbols. Instead of chasing each header, ninja
	# every custom-command OUTPUT (pure codegen — no object compiles, so it's fast and
	# side-steps the x86-can't-assemble-aarch64-asm failure). Best-effort; the enumerated
	# lists remain as a harmless fallback (already built here).
	log "materializing all mesa generated sources in ${mesa_v3d_build} (codegen-only ninja)"
	# Build each generated output INDIVIDUALLY. A single `ninja -k 0 <all>` aborts at the
	# manifest/graph level if any one target is unresolvable on the cold host (-k 0 only
	# tolerates *build* errors, not graph-load errors), leaving the rest (e.g. nir_opcodes.h)
	# ungenerated. Per-target ninja with `|| true` isolates each: every codegen output that
	# can build, does — which is all of them (each verified to build standalone).
	( cd "${mesa_v3d_build}" \
	  && mapfile -t _gen < <(ninja -t targets rule CUSTOM_COMMAND 2>/dev/null | cut -d: -f1) \
	  && { n=0; for _t in "${_gen[@]}"; do ninja "$_t" >/dev/null 2>&1 && n=$((n+1)) || true; done; \
	       log "materialized ${n}/${#_gen[@]} generated sources"; } ) \
	  || warn "gen-all step returned non-zero (continuing)"

	# --- GPU driver + GL archives (order: v3d driver -> GL frontend) ---
	local py="python3"
	if [ ! -f "${gpu_libs}/libv3d-phoenix.a" ] || [ "$force" = 1 ] || ! archive_fresh "${gpu_libs}/libv3d-phoenix.a"; then
		log "build-v3d-phoenix.py (Mesa v3d gallium driver)"
		"$py" "${repo_root}/tools/v3d-driver-port/build-v3d-phoenix.py" || die "build-v3d-phoenix.py failed"
	else ok "libv3d-phoenix.a fresh"; fi
	need_file "${gpu_libs}/libv3d-phoenix.a" "build-v3d-phoenix.py did not produce its archive"

	if [ ! -f "${gpu_libs}/libGL-phoenix.a" ] || [ "$force" = 1 ] || ! archive_fresh "${gpu_libs}/libGL-phoenix.a"; then
		log "build-gl-phoenix.py (Mesa OpenGL frontend)"
		"$py" "${repo_root}/tools/v3d-driver-port/build-gl-phoenix.py" || die "build-gl-phoenix.py failed"
	else ok "libGL-phoenix.a fresh"; fi
	need_file "${gpu_libs}/libGL-phoenix.a" "build-gl-phoenix.py did not produce its archive"

	# --- GLQuake engine archive (needs libGL + libv3d; NO glslang) ---
	if [ ! -f "${gpu_libs}/libquakespasm.a" ] || [ "$force" = 1 ] || ! archive_fresh "${gpu_libs}/libquakespasm.a"; then
		log "build-quakespasm-phoenix.py (GLQuake engine)"
		need_dir "${repo_root}/external/quakespasm" "external/quakespasm — clone the quakespasm fork"
		"$py" "${repo_root}/tools/quakespasm-port/build-quakespasm-phoenix.py" || die "build-quakespasm-phoenix.py failed"
	else ok "libquakespasm.a fresh"; fi
	need_file "${gpu_libs}/libquakespasm.a" "build-quakespasm-phoenix.py did not produce its archive"

	if [ "$skip_vulkan" = 1 ]; then
		warn "--skip-vulkan: not building V3DV / vkquake archives"
		ok "PHASE gpu complete (GL only)"; return 0
	fi

	# --- Vulkan V3DV + vkquake (BEST-EFFORT) --------------------------------
	# vkQuake is a work-in-progress capstone ("renders the first frame"); GLQuake
	# above is the proven bar. So the whole Vulkan path is soft: a failure here is
	# recorded and reported, never fatal, so the GL+quake spine (and the caller's
	# --with-showcase image build) always proceeds. (separate host build with
	# -Dvulkan-drivers=broadcom.)
	local gpu_soft=()
	if ! setup_mesa_host_build "${mesa_v3dv_build}" -Dvulkan-drivers=broadcom; then
		warn "v3dv meson setup failed — skipping Vulkan path"; gpu_soft+=("v3dv meson setup")
	elif [ ! -f "${mesa_v3dv_build}/compile_commands.json" ]; then
		warn "v3dv compile_commands.json missing — skipping Vulkan path"; gpu_soft+=("v3dv compile_commands")
	else
		# Materialize the generated Vulkan entrypoint sources (v3dv_entrypoints.c
		# etc.) that build-v3dv-phoenix.py compiles. Building the broadcom vulkan
		# .so target generates them; the link itself fails on the x86 v3d asm,
		# which is fine — the generated .c files are emitted first. Best-effort.
		ninja_mesa_soft "${mesa_v3dv_build}" src/broadcom/vulkan/libvulkan_broadcom.so
		if ! find "${mesa_v3dv_build}" -name 'v3dv_entrypoints.c' | grep -q .; then
			warn "v3dv_entrypoints.c not generated by the best-effort ninja"
		fi

		if [ ! -f "${gpu_libs}/libv3dv-phoenix.a" ] || [ "$force" = 1 ] || ! archive_fresh "${gpu_libs}/libv3dv-phoenix.a"; then
			log "build-v3dv-phoenix.py (Mesa V3DV Vulkan ICD)"
			"$py" "${repo_root}/tools/v3d-driver-port/build-v3dv-phoenix.py" \
				|| { warn "build-v3dv-phoenix.py failed"; gpu_soft+=("build-v3dv-phoenix.py"); }
		else ok "libv3dv-phoenix.a fresh"; fi

		if [ -f "${gpu_libs}/libv3dv-phoenix.a" ]; then
			# vkquake needs REAL SPIR-V shaders -> glslang. Warn loudly if absent.
			if ! command -v glslangValidator >/dev/null 2>&1 && ! command -v glslang >/dev/null 2>&1; then
				warn "glslang not on PATH — vkquake shaders will be PLACEHOLDER (non-rendering). Install glslang-tools for real SPIR-V."
			fi
			if [ ! -d "${repo_root}/external/vkquake" ]; then
				warn "external/vkquake absent — skipping vkquake archive"; gpu_soft+=("vkquake (no external/vkquake)")
			elif [ ! -f "${gpu_libs}/libvkquake.a" ] || [ "$force" = 1 ] || ! archive_fresh "${gpu_libs}/libvkquake.a"; then
				log "build-vkquake-phoenix.py --link (vkQuake engine)"
				if "$py" "${repo_root}/tools/vkquake-port/build-vkquake-phoenix.py" --link; then
					[ -f "${gpu_libs}/libvkquake.a" ] || { warn "libvkquake.a not produced"; gpu_soft+=("libvkquake.a missing"); }
				else
					# build-vkquake-phoenix.py writes the archive BEFORE its final
					# standalone link-drive verification, so a nonzero return can
					# leave a linkable-looking but incomplete libvkquake.a on disk.
					# The rpi4-vkquake _user Makefile guard treats archive-present
					# as buildable, so a broken archive would make `build.sh project`
					# (make -C _user all) HARD-FAIL and abort the whole image build.
					# GATE: remove the archive on link failure so the _user guard
					# skips rpi4-vkquake, GLQuake still ships, and the base build is
					# unaffected. (Known cause on a clean Ubuntu host: the mesa host
					# build has -DHAVE_SPIRV_TOOLS, so spirv_to_nir.c references
					# spirv_print_asm, which the v3dv aux closure doesn't resolve ->
					# undefined symbol at the rpi4-vkquake link. See KNOWN-ISSUES.)
					warn "build-vkquake-phoenix.py link failed — removing incomplete libvkquake.a so rpi4-vkquake is skipped (GLQuake unaffected)"
					rm -f "${gpu_libs}/libvkquake.a"
					gpu_soft+=("vkquake link (spirv_print_asm undefined) — archive removed, rpi4-vkquake skipped")
				fi
			else ok "libvkquake.a fresh"; fi
		else
			warn "no libv3dv-phoenix.a — skipping vkquake"; gpu_soft+=("vkquake (no v3dv archive)")
		fi
	fi

	if [ "${#gpu_soft[@]}" -gt 0 ]; then
		warn "PHASE gpu: GL+GLQuake spine OK; Vulkan path had ${#gpu_soft[@]} soft failure(s) (non-fatal):"
		printf '  - %s\n' "${gpu_soft[@]}" >&2
	fi
	ok "PHASE gpu complete — archives in ${gpu_libs}:"
	ls -la "${gpu_libs}"/*.a 2>/dev/null || true
}

##############################################################################
# PHASE stage — port libs + X11 libs + apps, staged into $stage_dir
##############################################################################

run_step() {
	# run_step <label> <script> [args...] — run a tools build script with the
	# showcase staging dir exported; fail loud on non-zero (best-effort steps
	# use run_step_soft instead).
	local label="$1"; shift
	log "$label"
	SHOWCASE_STAGE_DIR="${stage_dir}" "$@" || die "$label failed ($*)"
}

# Soft steps: the X11 apps / ports are the explicit "get as far as feasible,
# report breakages precisely" tier. A failure here is recorded and reported at
# the end rather than aborting the whole stage phase.
soft_failures=()
run_step_soft() {
	local label="$1"; shift
	log "$label"
	if SHOWCASE_STAGE_DIR="${stage_dir}" "$@"; then
		ok "$label"
	else
		warn "$label FAILED (rc=$?) — recording, continuing"
		soft_failures+=("$label")
	fi
}

phase_stage() {
	log "PHASE stage — port libs + X11 + apps -> ${stage_dir}"
	[ -d "${stage_dir}/bin" ] || die "staging tree ${stage_dir} has no bin/ — run this AFTER build.sh has populated the rootfs (fs/core stages)"

	# Pre-create the data-file destinations the app scripts write into. Several
	# scripts gate their data staging on `[ -d .../usr/share ]`; create it so the
	# themes / app-defaults / terminfo / mc skins get staged into the rootfs tree.
	mkdir -p "${stage_dir}/usr/share" "${stage_dir}/etc" "${stage_dir}/usr/lib"

	local X11="${repo_root}/tools/x11-port"
	local PORTS="${repo_root}/tools/ports"

	# --- port libraries (dependency order; hard-fail: apps need these) ---
	# libiconv/libffi/ncurses have no cross-deps. glib2 and fltk both consume
	# zlib/png/jpeg from the X11 prefix (/tmp/x11-phoenix), so the X11 lib stack
	# MUST be built before them. (glib2's zlib copy is `|| true`, so a missing
	# X11 prefix fails glib2 silently downstream — order matters.)
	run_step "port lib: libiconv" "${PORTS}/build-libiconv.sh"
	run_step "port lib: libffi"   "${PORTS}/build-libffi.sh"
	run_step "port lib: ncurses"  "${PORTS}/build-ncurses.sh"

	if [ "$skip_x11" = 0 ]; then
		# X11 lib stack first (provides zlib/png/jpeg + the X client libs).
		run_step "X11 lib stack" "${X11}/build-x11-phoenix.sh"
		run_step "port lib: glib2" "${PORTS}/build-glib2.sh"
		run_step "port lib: fltk"  "${PORTS}/build-fltk.sh"
	else
		warn "--skip-x11: skipping X11 libs + fltk (dillo/X apps will be skipped)"
		# glib2 needs zlib from the X11 prefix; without X11 it may fail — attempt
		# it soft so nano/mc that only need ncurses still stage.
		run_step_soft "port lib: glib2 (no X11 prefix — may fail)" "${PORTS}/build-glib2.sh"
	fi

	# --- userland ports (soft) ---
	run_step_soft "port app: nano" "${PORTS}/build-nano.sh"
	run_step_soft "port app: mc"   "${PORTS}/build-mc.sh"
	if [ "$skip_x11" = 0 ]; then
		run_step_soft "port app: dillo" "${PORTS}/build-dillo.sh"
	fi

	# --- X11 server + apps (soft) ---
	if [ "$skip_x11" = 0 ]; then
		run_step_soft "X11: Xphoenix server" "${X11}/build-xfbdev.sh"
		run_step_soft "X11: xlaunch/startx"  "${X11}/build-xlaunch.sh"
		run_step_soft "X11: xterm"           "${X11}/build-xterm.sh"
		run_step_soft "X11: xedit"           "${X11}/build-xedit.sh"
		run_step_soft "X11: xcalc"           "${X11}/build-xcalc.sh"
		run_step_soft "X11: xclock"          "${X11}/build-xclock.sh"
		run_step_soft "X11: xlogo"           "${X11}/build-xlogo.sh"
		run_step_soft "X11: xbill"           "${X11}/build-xbill.sh"
		run_step_soft "X11: WindowMaker"     "${X11}/build-wmaker.sh"
	fi

	if [ "${#soft_failures[@]}" -gt 0 ]; then
		warn "PHASE stage finished with ${#soft_failures[@]} soft failure(s):"
		printf '  - %s\n' "${soft_failures[@]}" >&2
	fi
	ok "PHASE stage complete — staged into ${stage_dir}"
	log "showcase binaries now in ${stage_dir}/bin:"
	ls "${stage_dir}/bin" 2>/dev/null | grep -Ei 'xterm|xedit|xcalc|wmaker|dillo|^mc$|nano|Xphoenix|xbill|startx|xclock|xlogo' || true
}

##############################################################################
case "$phase" in
	gpu)   phase_gpu ;;
	stage) phase_stage ;;
	all)   phase_gpu; phase_stage ;;
esac
