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
#   gpu    : host Mesa meson builds + the three GPU archive builds ->
#            tools/.gpu-libs/{libv3d,libGL,libv3dv}-phoenix.a. These archives MUST
#            exist before the main `build.sh ports` stage runs, because the five
#            game ports (quakespasm, yquake2, quake3, vkquake, supertuxkart) link
#            them by absolute path and b_die without them. (rebuild-rpi4b-fast.sh
#            --with-showcase runs this BEFORE it invokes build.sh.)
#
#            The GAME ENGINE archives that used to be built here
#            (libquakespasm.a via tools/quakespasm-port/build-quakespasm-phoenix.py,
#            libvkquake.a via tools/vkquake-port/build-vkquake-phoenix.py) are GONE:
#            they existed only to feed the _user/rpi4-quake and _user/rpi4-vkquake
#            loader.disk wrappers, which were deleted 2026-09-03 when the games
#            became framework ports installing into the rootfs (decision D10, "one
#            way to build each game"). The ports build their engines from the
#            pinned tarball + the fork-generated patch.
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

# Host Mesa build dirs (reused by the V3D/Mesa port scripts in
# sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/*.py via
# their hardcoded HOSTBUILD paths). Kept in /tmp to match those scripts.
mesa_v3d_build="${MESA_V3D_BUILD:-/tmp/mesa-v3d-build}"
mesa_v3dv_build="${MESA_V3DV_BUILD:-/tmp/mesa-v3dv-build}"
mesa_pyenv="${MESA_PYENV:-/tmp/mesa-pyenv}"

# uv (used to provision the mesa meson pyenv) is installed by bootstrap into
# ~/.local/bin. A login shell picks that up via ~/.profile, but when bootstrap
# and this build run in the SAME shell session (e.g. `bootstrap && rebuild`), the
# PATH was fixed before uv existed, so `command -v uv` misses it. Prepend the
# standard user-local bin dir (bootstrap does the same) so uv is found regardless.
export PATH="$HOME/.local/bin:$PATH"

phase="all"
force=0
# The V3DV Vulkan ICD is now REQUIRED by the default showcase: the `vkquake` port
# is `if: true` in ports.yaml and links libv3dv-phoenix.a, so a ports stage without
# it fails. vkQuake renders on hardware, so there is no reason to keep it opt-in.
# --skip-vulkan still exists for a GL-only GPU-stack build, but such a build cannot
# then run the ports stage with vkquake enabled.
skip_vulkan=0
skip_x11=0

usage() {
	cat <<'EOF'
Usage: build-showcase-apps.sh [--phase gpu|stage|all] [options]

Build the Phoenix-RTOS RPi4 showcase-app layer (GPU/GL/Vulkan + Quake, X11
server + apps, dillo/mc/nano) reproducibly, in dependency order.

Phases:
  --phase gpu     host Mesa builds + GPU archives -> tools/.gpu-libs/*.a
                  (run BEFORE build.sh; the archives are linked by the five
                  game ports in ports.yaml)
  --phase stage   port libs + X11 libs + X11 apps + ports, staged into
                  $SHOWCASE_STAGE_DIR (run AFTER build.sh populated the rootfs)
  --phase all     gpu then stage (default; for a standalone full run)

Options:
  --force         rebuild archives even if present + fresh (default: skip
                  up-to-date archives for iteration speed)
  --with-vkquake  no-op, kept for compatibility: the V3DV Vulkan stack is ON by
                  default now (the vkquake port needs libv3dv-phoenix.a)
  --skip-vulkan   skip the V3DV path (GL only). Incompatible with the vkquake
                  port being enabled in ports.yaml — the ports stage will fail
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
		--with-vkquake) skip_vulkan=0;;
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

# archive_fresh <archive> [extra_src_dir...] — 0 (fresh) if the archive exists AND
# is newer than every input under the port-script tree AND every caller-supplied
# extra source dir (the external/ trees the build recipe actually compiles). The
# port-script tree alone is NOT enough: editing external/quakespasm/Quake/*.c or
# external/mesa/src/**/*.c leaves the port scripts untouched, so a tools-only check
# silently ships a STALE archive (cost a build cycle 2026-07-18). Callers MUST pass
# the external source dirs their archive is built from. --force bypasses.
#
# Two hardening rules added 2026-09-03, both preventing a SILENT stale archive:
#
#  1. Every freshness input directory must EXIST. The old body ended in
#     `2>/dev/null`, so a renamed or migrated input dir (e.g. the tools/->ports
#     migration moving tools/quakespasm-port away) turned into "find printed
#     nothing" == "archive is fresh" == never rebuild. Same failure shape as the
#     _user Makefile guard whose empty target list made GNU make silently skip the
#     prerequisite (fixed in phoenix-rtos-project ca91eb9). Missing input => die.
#
#  2. The freshly built sysroot's libphoenix.a is a freshness input. These archives
#     are cross-compiled against libphoenix headers; after a libphoenix/kernel
#     rebuild an mtime check over Mesa/port sources alone still says "fresh", so an
#     ABI-skewed libGL/libv3d/libv3dv would ship. (Absent sysroot = not yet built =
#     not an input; the check just skips it.)
archive_fresh() {
	local a="$1"; shift
	[ -f "$a" ] || return 1
	[ "$force" = 0 ] || return 1

	local inputs=(
		"${repo_root}/sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa"
		"${repo_root}/tools/v3d-driver-port"
		"${repo_root}/tools/quakespasm-port"
		"${repo_root}/tools/vkquake-port"
		"$@"
	)
	local d
	for d in "${inputs[@]}"; do
		[ -e "$d" ] || die "archive_fresh: freshness input missing: $d
       A missing input silently reads as 'nothing is newer' => the stale archive
       $(basename "$a") would be reused forever. Fix the path list in this
       function (or restore the directory) rather than letting it pass."
	done

	local libphoenix="${buildroot}/_build/${target}/sysroot/lib/libphoenix.a"
	[ -f "$libphoenix" ] && inputs+=("$libphoenix")

	local newest
	newest="$(find "${inputs[@]}" -type f -newer "$a" -print -quit)"
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
	# Skip the codegen loop when the key generated sources already exist (from a prior build):
	# the 88-target serial ninja loop is slow AND has been an intermittent point where the whole
	# build gets externally killed; skipping it when unneeded shrinks that window. The enumerated
	# aux/core lists still materialize any straggler on demand.
	#
	# ROBUSTNESS (2026-08-22): the sentinel set below MUST include headers the Mesa *.c compiles
	# actually #include, not just the nir/format tables. A partial /tmp/mesa-v3d-build (the 4 table
	# sentinels present but the glapi dispatch headers missing — e.g. after a partial /tmp cleanup)
	# would otherwise falsely skip the codegen, then every src/mesa/main/*.c fails "dispatch.h: No
	# such file or directory" -> libGL incomplete -> libquakespasm link fails with ~222 undefined
	# _mesa_* symbols (observed). So also require the generated glapi headers (path-agnostic find).
	if [ -s "${mesa_v3d_build}/src/compiler/nir/nir_opcodes.c" ] \
	   && [ -s "${mesa_v3d_build}/src/compiler/builtin_types.c" ] \
	   && [ -s "${mesa_v3d_build}/src/util/format/u_format_table.c" ] \
	   && [ -s "${mesa_v3d_build}/src/compiler/nir/nir_intrinsics.c" ] \
	   && [ -n "$(find "${mesa_v3d_build}" -name dispatch.h -print -quit 2>/dev/null)" ] \
	   && [ -n "$(find "${mesa_v3d_build}" -name api_exec_decl.h -print -quit 2>/dev/null)" ]; then
		log "generated mesa sources already present in ${mesa_v3d_build} — skipping codegen loop"
	else
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
	fi

	# --- GPU driver + GL archives (order: v3d driver -> GL frontend) ---
	local py="python3"
	# NB: libv3d is built from the Mesa tree AND the Phoenix winsys under
	# sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/
	# (v3d_phoenix_winsys.c — where the #67 GPU-coherency fix lives). archive_fresh() already
	# hard-codes that mesa/ dir (+ tools/v3d-driver-port, quakespasm-/vkquake-port) as freshness inputs, so a
	# winsys-only change (Mesa untouched) DOES trigger a rebuild — a stale pre-fix libv3d.a cannot
	# silently ship. The mesa src/include args below are the additional Mesa-tree inputs.
	if [ ! -f "${gpu_libs}/libv3d-phoenix.a" ] || [ "$force" = 1 ] || ! archive_fresh "${gpu_libs}/libv3d-phoenix.a" "${repo_root}/external/mesa/src" "${repo_root}/external/mesa/include"; then
		log "build-v3d-phoenix.py (Mesa v3d gallium driver)"
		"$py" "${repo_root}/sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/build-v3d-phoenix.py" || die "build-v3d-phoenix.py failed"
	else ok "libv3d-phoenix.a fresh"; fi
	need_file "${gpu_libs}/libv3d-phoenix.a" "build-v3d-phoenix.py did not produce its archive"

	if [ ! -f "${gpu_libs}/libGL-phoenix.a" ] || [ "$force" = 1 ] || ! archive_fresh "${gpu_libs}/libGL-phoenix.a" "${repo_root}/external/mesa/src" "${repo_root}/external/mesa/include"; then
		log "build-gl-phoenix.py (Mesa OpenGL frontend)"
		"$py" "${repo_root}/sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/build-gl-phoenix.py" || die "build-gl-phoenix.py failed"
	else ok "libGL-phoenix.a fresh"; fi
	need_file "${gpu_libs}/libGL-phoenix.a" "build-gl-phoenix.py did not produce its archive"

	# NOTE: no game-engine archive is built here any more. The GLQuake
	# (libquakespasm.a) and vkQuake (libvkquake.a) archive steps were removed
	# 2026-09-03 together with the _user/rpi4-quake / _user/rpi4-vkquake wrappers
	# that consumed them; the engines are now built by the framework ports
	# (sources/phoenix-rtos-ports/{quakespasm,vkquake}) from the pinned tarball plus
	# the patch generated from our fork. This phase's job is only the GPU stack the
	# ports link against.

	if [ "$skip_vulkan" = 1 ]; then
		warn "--skip-vulkan: no libv3dv-phoenix.a will be produced. The vkquake port (ports.yaml if:true) links it and will b_die in the ports stage."
		ok "PHASE gpu complete (GL only)"; return 0
	fi

	# --- Vulkan V3DV ICD (SOFT) ---------------------------------------------
	# Separate host Mesa build with -Dvulkan-drivers=broadcom. The steps stay SOFT
	# (recorded and reported, never fatal) so a Vulkan hiccup does not abort the GL
	# spine mid-phase and the caller still gets a diagnosable run. Note that the
	# consequence has changed: libv3dv-phoenix.a is now a HARD requirement of the
	# vkquake port, so a soft failure here surfaces as a b_die in the ports stage
	# naming the missing archive.
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

		if [ ! -f "${gpu_libs}/libv3dv-phoenix.a" ] || [ "$force" = 1 ] || ! archive_fresh "${gpu_libs}/libv3dv-phoenix.a" "${repo_root}/external/mesa/src" "${repo_root}/external/mesa/include"; then
			log "build-v3dv-phoenix.py (Mesa V3DV Vulkan ICD)"
			"$py" "${repo_root}/sources/phoenix-rtos-devices/gpu/rpi4-v3d/mesa/build-v3dv-phoenix.py" \
				|| { warn "build-v3dv-phoenix.py failed"; gpu_soft+=("build-v3dv-phoenix.py"); }
		else ok "libv3dv-phoenix.a fresh"; fi

		# The vkquake ENGINE archive is no longer built here (see the note in the
		# GL section above): the vkquake framework port builds and links the engine
		# itself. All this phase owes it is libv3dv-phoenix.a, checked below.
		if [ ! -f "${gpu_libs}/libv3dv-phoenix.a" ]; then
			warn "no libv3dv-phoenix.a — the vkquake port will b_die in the ports stage"
			gpu_soft+=("libv3dv-phoenix.a missing (vkquake port cannot link)")
		fi

		# NOTE: glslang is NOT a build dependency of the vkquake PORT. The port's
		# glue/vkquake_shaders.c carries the compiled SPIR-V as committed byte arrays
		# (1.1 MB; verified to start with the 0x07230203 SPIR-V magic), regenerated
		# out-of-band by tools/vkquake-port/gen-vkquake-shaders.py when the Shaders/
		# tree changes. The old "glslang missing -> placeholder shaders" warning
		# belonged to the deleted ad-hoc build-vkquake-phoenix.py and is gone with it.
	fi

	if [ "${#gpu_soft[@]}" -gt 0 ]; then
		warn "PHASE gpu: GL spine OK; Vulkan path had ${#gpu_soft[@]} soft failure(s) (non-fatal):"
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

	# --- game launchers (hard-fail: without them three of the five games have no
	# usable entry point from psh, which cannot set env vars or chain commands) ---
	# The engines themselves come from the ports stage; this only builds the tiny
	# static glue binaries (ram-stage-play, quake2, quake3, stk). Their game DATA is
	# staged into the rootfs overlay by scripts/stage-game-data.sh.
	run_step "rootfs helper binaries (launchers, ram-stage-play, pty-run)" "${repo_root}/scripts/build-rootfs-helpers.sh" \
		--stage-dir "${stage_dir}"

	# --- port libraries ---
	# GONE (2026-09-03): libiconv, libffi, ncurses and glib2 no longer have ad-hoc
	# steps. Every consumer is now a framework port that pulls them transitively
	# via depends=, so these four existed only to duplicate what the ports
	# framework already builds — with different versions in two places:
	#
	#   libiconv  tools/ = a 1,974-byte hand-written ASCII/UTF-8 identity STUB,
	#             installed into the SHARED sysroot; framework = real GNU 1.18
	#   libffi    tools/ = 3.3                    framework = 3.4.6
	#   zlib      (via glib2) tools/ = 1.3.1      framework = 1.2.11
	#
	# gl-x11-window was the last thing still linking the stub, and it linked it by
	# -L search order alone (coord 41d0fef18). Consumers verified absent before
	# deleting: nano/mc/dillo/python are framework ports; no X11 script references
	# ncurses/glib2/libffi; build-pango.sh only mentions the glib prefix in a
	# comment and is not invoked at all.
	if [ "$skip_x11" = 0 ]; then
		# X11 lib stack stays: it provides zlib/png/jpeg + the X client libs to the
		# small ad-hoc X apps (xedit/xcalc/...) out of /tmp/x11-phoenix.
		run_step "X11 lib stack" "${X11}/build-x11-phoenix.sh"
	else
		warn "--skip-x11: skipping the X11 lib stack (the ad-hoc X apps will be skipped)"
	fi

	# --- userland ports (soft) ---
	# nano is a FRAMEWORK PORT now (ports.yaml if:true) — no step here. Adding one
	# back would stage a second, different binary, and this phase runs AFTER the
	# ports stage into the same rootfs, so the copy written here would silently win.
	# mc is a FRAMEWORK PORT now (ports.yaml if:true) — no step here. See the nano
	# note above: this phase runs AFTER the ports stage into the same rootfs, so a
	# step here would silently overwrite the framework-built binary.
	# dillo removed (#7 2026-08-22): now a framework port (ports.yaml if:true) — the
	# ports stage builds + stages /bin/dillo into the rootfs.

	# --- X11 server + apps (soft) ---
	# #7 2026-08-22: the Xphoenix server, xterm and WindowMaker are now framework ports
	# (ports.yaml if:true); the ports stage builds + stages them into the rootfs
	# (Xphoenix -> /usr/bin, xterm/wmaker -> /bin), so their ad-hoc steps are removed
	# here. The small Xaw demo apps xedit/xcalc/xclock/xlogo (framework port xorg_apps)
	# and xbill (framework port xbill) are ALSO framework ports now (ports.yaml
	# if:true) — the ports stage builds + stages them into the rootfs /bin + their
	# app-defaults/assets — so their ad-hoc build-{xedit,xcalc,xclock,xlogo,xbill}.sh
	# steps are removed here too. Only the xlaunch/startx supervisor (a tiny in-repo
	# C launcher, not an upstream tarball) stays ad-hoc. NOTE: framework Xphoenix
	# lands at /usr/bin/Xphoenix (b_install) not /bin — launch with that path.
	if [ "$skip_x11" = 0 ]; then
		run_step_soft "X11: xlaunch/startx"  "${X11}/build-xlaunch.sh"

		# Concurrent-GPU (#13) daemon-client desktop apps: the glamor X server and the
		# GPU window relinked as v3d-server CLIENTS (route V3D work through the rpi4-v3d
		# daemon instead of the in-process winsys), so a shipped image can run the
		# accelerated CONCURRENT-GPU desktop — rpi4-v3d (daemon, already a device
		# component) + Xphoenix-glamor-daemon + gl-x11-window-daemon via pl_phoenix_xlaunch
		# `gpudesk`/`deskapps`. HW-proven end-to-end: docs/inprogress/2026-08-22-concurrent-
		# gpu-v3d-server-feasibility.md. Soft steps (GPU-lib dependent); staged into /bin.
		# Requires the GPU phase (Mesa/libGL + libv3d-phoenix.a) to have run first.
		# The glamor daemon links libglamor.a, which only exists if the xorg-server
		# core was CONFIGURED with --enable-glamor. Nothing here did that, so on a
		# clean tree the daemon died with "missing libglamor.a — configure core with
		# --enable-glamor first". It passed on an incremental tree purely because an
		# old ad-hoc --enable-glamor reconfigure had left the archive behind — the
		# exact stale-artifact dependency build-xserver-core.sh's --glamor flag was
		# added to retire. Build it explicitly; the script's own glamor marker makes
		# this a no-op once the core is already configured that way.
		run_step_soft "X11: xorg-server core (glamor)" "${X11}/build-xserver-core.sh" --glamor
		run_step_soft "X11: Xphoenix-glamor-daemon (concurrent-GPU X)" "${X11}/build-xfbdev.sh" --glamor-daemon
		xgd="$(find "${X11}/src" -name Xphoenix-glamor-daemon -type f 2>/dev/null | head -1)"
		if [ -n "$xgd" ]; then cp -v "$xgd" "${stage_dir}/bin/Xphoenix-glamor-daemon"; else warn "Xphoenix-glamor-daemon not built — skipped staging"; fi
		run_step_soft "X11: gl-x11-window-daemon (GPU window client)" "${X11}/build-gl-x11-window.sh" --daemon
		if [ -f "${gpu_libs}/gl-x11-window-daemon" ]; then cp -v "${gpu_libs}/gl-x11-window-daemon" "${stage_dir}/bin/gl-x11-window-daemon"; else warn "gl-x11-window-daemon not built — skipped staging"; fi
	fi

	if [ "${#soft_failures[@]}" -gt 0 ]; then
		warn "PHASE stage finished with ${#soft_failures[@]} soft failure(s):"
		printf '  - %s\n' "${soft_failures[@]}" >&2
	fi
	ok "PHASE stage complete — staged into ${stage_dir}"
	log "showcase binaries now in ${stage_dir}/bin:"
	ls "${stage_dir}/bin" 2>/dev/null | grep -Ei 'xterm|xedit|xcalc|wmaker|dillo|^mc$|nano|Xphoenix|xbill|startx|xclock|xlogo|^stk$|ram-stage-play' || true
}

##############################################################################
case "$phase" in
	gpu)   phase_gpu ;;
	stage) phase_stage ;;
	all)   phase_gpu; phase_stage ;;
esac
