#!/usr/bin/env bash
#
# bootstrap-linux-host.sh — set up a fresh Ubuntu x86-64 host for
# Phoenix-RTOS Raspberry Pi 4 development.
#
# Idempotent: re-running is safe. Each step checks whether its target
# already exists before doing work.
#
# What this script does:
#   1. Installs system packages (build toolchain, dnsmasq, serial,
#      video-capture, Python venv tooling, GitHub CLI)
#   2. Clones the project layout into ~/phoenix-rpi/:
#        ~/phoenix-rpi/                          (coord repo)
#        ~/phoenix-rpi/sources/<repo>/           (each sibling)
#      Each sibling gets fork=houp/* + origin=phoenix-rtos/* remotes.
#   3. Stages the Raspberry Pi 4 boot blobs (start4.elf, fixup4.dat,
#      bcm2711-rpi-4-b.dtb, overlays/miniuart-bt.dtbo) under
#      ~/phoenix-rpi/.bootblobs/.
#   4. Creates a Python venv at ~/phoenix-rpi/.venv/ with pyserial.
#   5. Prints the next steps (NIC config, dnsmasq launch, first build).
#
# Author/contact: see https://github.com/houp/phoenix-rpi
# Generated 2026-05-19 during Phoenix-RTOS Pi 4 bring-up session.

set -euo pipefail

# Where the project tree lives. If this script is being run from INSIDE an
# already-cloned coordination repo (…/scripts/bootstrap-linux-host.sh), default
# to that repo root so the bootstrap and rebuild-rpi4b-fast.sh (which derives its
# root from cwd) operate on the SAME tree (#75). Otherwise — the standalone
# "download the script and run it" flow — default to $HOME/phoenix-rpi. Always
# overridable via the PROJECT_DIR env var.
_self_dir="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
_maybe_repo="$(dirname "$_self_dir")"
if [ -f "$_maybe_repo/AGENTS.md" ] && [ -d "$_maybe_repo/scripts" ] && [ -d "$_maybe_repo/manifests" ]; then
	PROJECT_DIR="${PROJECT_DIR:-$_maybe_repo}"
else
	PROJECT_DIR="${PROJECT_DIR:-$HOME/phoenix-rpi}"
fi
SOURCES_DIR="$PROJECT_DIR/sources"
EXTERNAL_DIR="$PROJECT_DIR/external"
BOOTBLOBS_DIR="$PROJECT_DIR/.bootblobs"
VENV_DIR="$PROJECT_DIR/.venv"

# Where the Pi-4-modified sibling forks live, and where the phoenix-rtos
# upstream lives. Both are configurable so the same script serves the
# author, individual contributors, and a future project org:
#
#   PHOENIX_FORK_BASE     base URL for the work forks (default: github.com/<GH_USER>)
#   PHOENIX_UPSTREAM_BASE base URL for the phoenix-rtos upstream repos
#   GH_USER               kept for backward-compat; feeds PHOENIX_FORK_BASE's default
#
# Each sibling is cloned from $PHOENIX_FORK_BASE/<repo>.git with the
# phoenix-rtos upstream wired as a second remote (see clone_repo).
GH_USER="${GH_USER:-houp}"
PHOENIX_FORK_BASE="${PHOENIX_FORK_BASE:-https://github.com/${GH_USER}}"
PHOENIX_UPSTREAM_BASE="${PHOENIX_UPSTREAM_BASE:-https://github.com/phoenix-rtos}"

# --pinned mode: after cloning, check every sibling out to the SHA recorded
# in a release-pin manifest (reproducible-build mode). Set by arg parsing.
PINNED_MODE=0
PIN_MANIFEST="${PIN_MANIFEST:-$PROJECT_DIR/manifests/release-pin.md}"

# Branches each Pi-4-modified sibling repo should track when newly
# cloned. Keep in sync with the latest manifest in manifests/.
declare -A SIBLING_BRANCHES=(
	[phoenix-rtos-kernel]=master
	[phoenix-rtos-devices]=master
	[phoenix-rtos-usb]=master
	[phoenix-rtos-utils]=master
	[libphoenix]=master
	[plo]=master
	[phoenix-rtos-filesystems]=master
	[phoenix-rtos-project]=master
	[phoenix-rtos-build]=master
)

# Additional sibling repos that phoenix-rtos-project's .gitmodules
# requires but where we have no Pi-4-specific commits — clone from the
# fork base like the others so a fresh empty-dir bootstrap gets the full
# 16-repo set (needed for --pinned/restore). If a fork of one of these
# doesn't exist yet, clone_repo falls back to the upstream remote.
UPSTREAM_ONLY_REPOS=(
	phoenix-rtos-corelibs
	phoenix-rtos-ports
	phoenix-rtos-lwip
	phoenix-rtos-posixsrv
	phoenix-rtos-tests
	phoenix-rtos-hostutils
	phoenix-rtos-doc
)

# BUILD-REQUIRED external dependencies cloned into external/ and pinned to
# a specific SHA/tag. These feed the V3D GPU / GL / Vulkan stack:
#   - external/mesa       -> tools/v3d-driver-port builds libGL/libv3d/libv3dv
#   - external/quakespasm -> tools/quakespasm-port builds libquakespasm.a
#   - external/vkquake    -> tools/vkquake-port builds libvkquake.a
# The rpi4b project's `make -C _user all install` builds rpi4-quake and
# rpi4-vkquake (both link the archives above), so all three are build inputs.
#
# NOT cloned here:
#   - external/linux      research-only; the Pi 4 DTB is fetched ready-made
#                         from raspberrypi/firmware (see stage_pi_firmware),
#                         never compiled. (7 GB; clone out-of-band if needed.)
#   - external/rpi-eeprom Tier-2 lab/netboot only; prepare-pi-eeprom-netboot.sh
#                         self-clones it on demand.
#
# Format: "<subdir>|<git-url>|<pinned-ref>". The pinned refs mirror the
# release-pin manifest; bump both together.
EXTERNAL_DEPS=(
	"mesa|https://gitlab.freedesktop.org/mesa/mesa.git|e8791b4bc1c10af74ddd3af029fbf06cafc11d56"
	"quakespasm|https://github.com/sezero/quakespasm.git|4abb3249fe45c835d3d8540845a18a114e283996"
	"vkquake|https://github.com/Novum/vkQuake.git|f4d923e36f6a2cbb6e796031eb81c88f23db8520"
)

# Raspberry Pi firmware blobs we need from raspberrypi/firmware boot
# tree. We pin to a known-good firmware date; bump deliberately.
PI_FW_REF="${PI_FW_REF:-master}"
PI_FW_REPO="https://github.com/raspberrypi/firmware.git"
PI_FW_FILES=(
	boot/start4.elf
	boot/fixup4.dat
	boot/bcm2711-rpi-4-b.dtb
	boot/overlays/miniuart-bt.dtbo
	boot/overlays/vc4-fkms-v3d.dtbo
)

log()   { printf '[bootstrap] %s\n' "$*"; }
warn()  { printf '[bootstrap] WARN: %s\n' "$*" >&2; }
fatal() { printf '[bootstrap] FATAL: %s\n' "$*" >&2; exit 1; }

require_root_for() {
	if [ "$(id -u)" -ne 0 ]; then
		fatal "this step needs sudo: $*"
	fi
}

##############################################################################
# 1. System packages
##############################################################################

# Single authoritative apt dependency list for a clean Phoenix-RTOS Pi 4
# build. Grouped by purpose so BUILD.md can quote it verbatim. Anything the
# toolchain build (build-phoenix-toolchain-linux.sh) needs is listed HERE so
# a one-shot bootstrap installs everything; that script re-checks its own
# subset for the standalone-toolchain case.
APT_PACKAGES=(
	# --- cross-toolchain build prerequisites (gcc-14.2 + binutils-2.43) ---
	build-essential bison flex texinfo
	libgmp-dev libmpfr-dev libmpc-dev
	wget xz-utils
	# --- host build tools ---
	gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
	cmake pkg-config make
	# autotools: phoenix-rtos-ports lighttpd runs autoreconf/autogen at prepare time
	autoconf automake libtool
	# phoenix-rtos-hostutils links against hidapi (hid.c / usb_imx.c / usb_vybrid.c)
	libhidapi-dev
	# --- SD-image + rootfs assembly (Tier 1: build & flash) ---
	device-tree-compiler
	mtools dosfstools parted e2fsprogs
	# --- source acquisition + Python tooling ---
	git git-lfs curl jq
	python3 python3-pip python3-venv
	# --- Tier 2: author's lab rig (netboot / serial / HDMI capture); ---
	# --- harmless to install, not required for build & flash ---
	dnsmasq iproute2
	tio picocom
	ffmpeg v4l-utils
	gh
	# --- Showcase build deps (Tier 1.5: only needed for --with-showcase, i.e.
	# --- the GPU/GL/Vulkan + Quake + X11 + dillo/mc layer via
	# --- scripts/build-showcase-apps.sh). Harmless for a base-image-only build. ---
	# Mesa host build (GPU/GL/Vulkan cross-compile reuses its compile_commands.json):
	ninja-build python3-mako
	# libdrm dev headers: Mesa's broadcom vulkan TUs #include <xf86drm.h>/<drm.h>.
	libdrm-dev
	# glslangValidator: vkQuake real SPIR-V shaders (GLQuake/GL path does NOT need it).
	glslang-tools
	# gperf: WindowMaker's bundled fontconfig runs gperf codegen at build time.
	gperf
	# NOTE: Ubuntu 24.04's apt `meson` (1.3.x) is too old for external/mesa
	# (needs >= 1.4). build-showcase-apps.sh provisions a local meson>=1.4 in a uv
	# venv (/tmp/mesa-pyenv) automatically, so meson is intentionally NOT in this
	# apt list.
)

install_packages() {
	log "Installing system packages (sudo apt-get)..."
	sudo apt-get update
	sudo apt-get install -y --no-install-recommends "${APT_PACKAGES[@]}"
	# uv is not in apt; install via the official installer script.
	if ! command -v uv >/dev/null 2>&1; then
		log "Installing uv (Python venv tool)..."
		curl -LsSf https://astral.sh/uv/install.sh | sh
		export PATH="$HOME/.local/bin:$PATH"
	fi
}

##############################################################################
# 2. Clone the project layout
##############################################################################

clone_repo() {
	local fork_url="$1"
	local upstream_url="$2"
	local dest="$3"
	local branch="$4"

	if [ -d "$dest/.git" ]; then
		log "  already cloned: $dest"
		# Make sure remotes are configured even on re-run.
		git -C "$dest" remote get-url fork    >/dev/null 2>&1 || git -C "$dest" remote add fork    "$fork_url"
		git -C "$dest" remote get-url origin  >/dev/null 2>&1 || git -C "$dest" remote add origin  "$upstream_url"
		return 0
	fi

	log "  cloning $fork_url → $dest (branch $branch)"
	# Use the fork as origin during clone (we want HEAD on fork's branch),
	# then rewire so origin=upstream, fork=fork to match the macOS layout.
	if git clone --branch "$branch" "$fork_url" "$dest"; then
		:
	elif git clone "$fork_url" "$dest"; then
		warn "fork branch '$branch' not on fork; cloned default branch then setting up"
		(cd "$dest" && git checkout -b "$branch" || true)
	else
		# No fork at all (e.g. an upstream-only sibling with no houp/ fork).
		# Fall back to cloning the upstream directly; still wire a 'fork'
		# remote pointing at the intended fork URL for future pushes.
		warn "fork $fork_url unavailable; cloning upstream $upstream_url instead"
		git clone "$upstream_url" "$dest"
		git -C "$dest" remote rename origin fork
		git -C "$dest" remote set-url fork "$fork_url"
		git -C "$dest" remote add origin "$upstream_url"
		git -C "$dest" fetch origin --no-tags || warn "couldn't fetch from upstream $upstream_url"
		return 0
	fi
	# Rename remotes: clone's "origin" becomes our "fork"; add upstream as "origin"
	git -C "$dest" remote rename origin fork
	git -C "$dest" remote add origin "$upstream_url"
	git -C "$dest" fetch origin --no-tags || warn "couldn't fetch from upstream $upstream_url"
}

clone_layout() {
	log "Cloning project layout to $PROJECT_DIR/"
	mkdir -p "$SOURCES_DIR"

	# 2a. Coord repo
	if [ ! -d "$PROJECT_DIR/.git" ]; then
		# Only happens if the user ran this script from a fresh empty
		# directory. Normally they curl the script and run it inside the
		# coord clone.
		log "Cloning coord repo into $PROJECT_DIR/"
		git clone "$PHOENIX_FORK_BASE/phoenix-rpi.git" "$PROJECT_DIR"
	else
		log "Coord repo already at $PROJECT_DIR/"
	fi

	# 2b. Pi-4-modified sibling repos (fork branch tracked per SIBLING_BRANCHES)
	for repo in "${!SIBLING_BRANCHES[@]}"; do
		branch="${SIBLING_BRANCHES[$repo]}"
		clone_repo \
			"$PHOENIX_FORK_BASE/$repo.git" \
			"$PHOENIX_UPSTREAM_BASE/$repo.git" \
			"$SOURCES_DIR/$repo" \
			"$branch"
	done

	# 2c. Remaining siblings phoenix-rtos-project requires (no Pi-4 commits).
	# Clone from the fork base like the rest so a fresh empty-dir bootstrap
	# produces the full 16-repo set; clone_repo falls back to upstream if a
	# fork of one of these doesn't exist. They track their default branch.
	for repo in "${UPSTREAM_ONLY_REPOS[@]}"; do
		clone_repo \
			"$PHOENIX_FORK_BASE/$repo.git" \
			"$PHOENIX_UPSTREAM_BASE/$repo.git" \
			"$SOURCES_DIR/$repo" \
			master
	done
}

##############################################################################
# 2d. Build-required external dependencies (V3D GPU / GL / Vulkan stack)
##############################################################################

clone_external_deps() {
	log "Cloning build-required external deps → $EXTERNAL_DIR/"
	mkdir -p "$EXTERNAL_DIR"
	local spec subdir url ref dest
	for spec in "${EXTERNAL_DEPS[@]}"; do
		IFS='|' read -r subdir url ref <<< "$spec"
		dest="$EXTERNAL_DIR/$subdir"
		if [ -d "$dest/.git" ]; then
			# Already present: leave dev clones alone in floating mode. In
			# --pinned mode, re-pin to the recorded ref so a drifted clone is
			# brought back to the reproducible SHA (never a fresh clone).
			if [ "$PINNED_MODE" -eq 1 ]; then
				log "  external/$subdir present; --pinned → checking out $ref"
				git -C "$dest" fetch --tags origin "$ref" 2>/dev/null || true
				git -C "$dest" checkout --quiet "$ref" || \
					warn "couldn't check out pinned ref $ref for external/$subdir"
			else
				log "  already present: external/$subdir (leaving as-is)"
			fi
			continue
		fi
		log "  cloning $url → external/$subdir (pinning $ref)"
		git clone "$url" "$dest"
		# Pin to the recorded ref. Fetch it explicitly first in case the
		# default clone depth/refspec didn't bring it in.
		git -C "$dest" fetch --tags origin "$ref" 2>/dev/null || true
		git -C "$dest" checkout --quiet "$ref" || \
			warn "couldn't check out pinned ref $ref for external/$subdir"
	done
}

##############################################################################
# 3. Pi firmware blobs
##############################################################################

stage_pi_firmware() {
	log "Staging Raspberry Pi firmware blobs ($PI_FW_REF) → $BOOTBLOBS_DIR/"
	mkdir -p "$BOOTBLOBS_DIR/overlays"
	local fw_tmp="$BOOTBLOBS_DIR/.firmware-checkout"
	if [ ! -d "$fw_tmp/.git" ]; then
		log "  doing sparse-checkout of $PI_FW_REPO ..."
		git clone --depth 1 --filter=blob:none --no-checkout \
			--branch "$PI_FW_REF" \
			"$PI_FW_REPO" "$fw_tmp"
		git -C "$fw_tmp" sparse-checkout init --cone
		git -C "$fw_tmp" sparse-checkout set boot
		git -C "$fw_tmp" checkout
	else
		log "  firmware checkout already present; pulling latest"
		git -C "$fw_tmp" pull --ff-only || true
	fi
	for f in "${PI_FW_FILES[@]}"; do
		local rel="${f#boot/}"
		local dest="$BOOTBLOBS_DIR/$rel"
		mkdir -p "$(dirname "$dest")"
		cp "$fw_tmp/$f" "$dest"
		log "  copied $rel"
	done
}

##############################################################################
# 4. Python venv for psh-interact.py
##############################################################################

setup_python_venv() {
	# pyserial -> psh-interact.py; the phoenix-rtos-build requirements
	# (resolvelib/jinja2/PyYAML/rich/...) -> the port_manager used by
	# build-ports.sh when building userspace ports (busybox etc.). The
	# rpi4b fast rebuild prepends $VENV_DIR/bin to PATH for the build, so
	# the build's bare `python3` resolves these here rather than from the
	# PEP668-managed system Python.
	local reqs="$PROJECT_DIR/sources/phoenix-rtos-build/requirements.txt"
	if [ -d "$VENV_DIR" ] \
		&& "$VENV_DIR/bin/python" -c 'import serial' 2>/dev/null \
		&& "$VENV_DIR/bin/python" -c 'import resolvelib' 2>/dev/null; then
		log "Python venv already at $VENV_DIR (pyserial + port_manager deps present)"
		return 0
	fi
	log "Creating Python venv at $VENV_DIR with pyserial + port_manager deps"
	export PATH="$HOME/.local/bin:$PATH"
	if ! command -v uv >/dev/null 2>&1; then
		warn "uv not on PATH yet — re-source ~/.local/bin/env or restart shell"
		return 1
	fi
	uv venv "$VENV_DIR" >/dev/null
	# uv venvs don't ship with pip; use `uv pip install` instead.
	uv pip install --python "$VENV_DIR/bin/python" pyserial
	if [ -f "$reqs" ]; then
		uv pip install --python "$VENV_DIR/bin/python" -r "$reqs"
	else
		warn "port_manager requirements not found at $reqs — ports build may fail"
	fi
}

##############################################################################
# 5. Print next steps
##############################################################################

print_next_steps() {
	cat <<EOF

[bootstrap] DONE. Next steps:

  1. Confirm the dedicated NIC is up at 10.42.0.1/24. Example with
     NetworkManager:

        nmcli connection add type ethernet ifname eth1 con-name pi-netboot \\
            ipv4.method manual ipv4.addresses 10.42.0.1/24 ipv6.method ignore
        nmcli connection up pi-netboot

     (Replace 'eth1' with whatever your USB-Ethernet adapter shows in 'ip link'.)

  2. Wire up the Pi:
        - GPIO 14/15 → USB-UART (TX/RX) at 115200 8N1
        - HDMI0      → HDMI-USB grabber
        - ETH        → dedicated NIC (eth1)
        - USB-A      → optional USB keyboard

  3. Power-cycle scripts: edit scripts/pi_power_on.sh and pi_power_off.sh to
     control whatever smart plug or switch you have. The macOS version uses
     python-kasa; the equivalent works fine on Linux.

  4. First build (full-clean to be safe):

        cd $PROJECT_DIR
        RPI4B_DTB_ALLOW_WARNINGS=1 ./scripts/rebuild-rpi4b-fast.sh --scope full-clean

  5. Start the netboot server (foreground, for the first test):

        sudo dnsmasq --no-daemon \\
            --interface=eth1 --bind-interfaces \\
            --dhcp-range=10.42.0.10,10.42.0.20,255.255.255.0,12h \\
            --enable-tftp --tftp-root=$PROJECT_DIR/.netboot/rpi4b-bootfs

     (Replace eth1 with your dedicated interface. The build copies output
     into .netboot/rpi4b-bootfs along with the firmware blobs from .bootblobs/.)

  6. In another terminal, open the UART:

        tio -b 115200 /dev/ttyUSB0

  7. Power-cycle the Pi. Expect 'fbcon: ok' on HDMI ~50 s after power-on,
     then the '(psh)% ' prompt about 1 s later.

  8. Verification runbook (USB keyboard etc.):

        $PROJECT_DIR/docs/knowledge/interactive-verification-runbook.md

  9. Read these in order:
        - AGENTS.md          (project rules)
        - docs/inprogress/status.md     (current focus)
        - docs/knowledge/build-cache-and-cold-boot-races.md   (build-cache lesson)
        - docs/knowledge/console-architecture-and-fbcon.md    (Path A vs Path B)
        - tracking/current-step.md  (the active implementation step)
        - manifests/2026-05-19-td12-stable-plus-pm-sigint.md  (current baseline)

  Bootstrapped repo locations on this machine:

        $PROJECT_DIR             — coord repo
        $SOURCES_DIR/            — sibling Phoenix-RTOS repos
        $EXTERNAL_DIR/           — build-required external deps (mesa, quakespasm, vkquake)
        $BOOTBLOBS_DIR/          — Raspberry Pi firmware blobs
        $VENV_DIR/               — Python venv (pyserial for psh-interact.py)

  GitHub remotes per sibling:
        origin   = $PHOENIX_UPSTREAM_BASE/<repo>  (upstream)
        fork     = $PHOENIX_FORK_BASE/<repo>      (work + pushes)

EOF
}

##############################################################################
# 6. Reproducible-build pin (--pinned): check every sibling out to a manifest SHA
##############################################################################

apply_pinned_manifest() {
	local restore="$PROJECT_DIR/scripts/restore-integration-state.sh"
	if [ ! -f "$PIN_MANIFEST" ]; then
		warn "--pinned: manifest not found: $PIN_MANIFEST (skipping SHA pin)"
		return 0
	fi
	if [ ! -x "$restore" ]; then
		warn "--pinned: restore-integration-state.sh not found/executable; skipping"
		return 0
	fi
	log "--pinned: restoring siblings to SHAs in $PIN_MANIFEST"
	# restore-integration-state.sh consumes the integration-state-v1 block
	# (the 16 siblings). --force so a freshly-cloned tree on a fork branch is
	# repointed to the pinned SHA without a dirty-tree refusal.
	#
	# HAZARD: --force discards uncommitted changes in a sibling. This is safe
	# on a fresh clone (nothing dirty) but destructive on a dev box with local
	# edits. Commit/stash before running --pinned on a working tree.
	"$restore" "$PIN_MANIFEST" --force

	# External deps were already re-pinned by clone_external_deps (which runs
	# before this in --pinned mode) using the EXTERNAL_DEPS refs.
	log "--pinned: external deps re-pinned by clone_external_deps (EXTERNAL_DEPS refs)"
}

##############################################################################
# main
##############################################################################

main() {
	# Arg parsing.
	while [ $# -gt 0 ]; do
		case "$1" in
			--pinned) PINNED_MODE=1; shift ;;
			--pin-manifest) PIN_MANIFEST="${2:?--pin-manifest requires a path}"; shift 2 ;;
			-h|--help)
				cat <<USAGE
usage: $0 [--pinned] [--pin-manifest <path>]

  --pinned              after cloning, check every sibling out to the SHA
                        recorded in the release-pin manifest (reproducible build)
  --pin-manifest PATH   manifest to use for --pinned (default: $PIN_MANIFEST)

Environment overrides:
  PROJECT_DIR           install root (default: \$HOME/phoenix-rpi)
  PHOENIX_FORK_BASE     work-fork base URL (default: https://github.com/\$GH_USER)
  PHOENIX_UPSTREAM_BASE upstream base URL (default: https://github.com/phoenix-rtos)
  GH_USER               feeds PHOENIX_FORK_BASE's default (default: houp)
USAGE
				exit 0 ;;
			*) fatal "unknown option: $1 (try --help)" ;;
		esac
	done

	log "Starting Phoenix-RTOS Pi 4 dev-host bootstrap"
	log "  PROJECT_DIR          = $PROJECT_DIR"
	log "  PHOENIX_FORK_BASE     = $PHOENIX_FORK_BASE"
	log "  PHOENIX_UPSTREAM_BASE = $PHOENIX_UPSTREAM_BASE"
	log "  PINNED_MODE           = $PINNED_MODE"

	install_packages
	clone_layout
	clone_external_deps
	stage_pi_firmware
	setup_python_venv
	if [ "$PINNED_MODE" -eq 1 ]; then
		apply_pinned_manifest
	fi
	print_next_steps
}

main "$@"
