#!/usr/bin/env bash
#
# Phoenix-RTOS — build the three Quake III QVM bytecode modules FROM SOURCE
# (vm/qagame.qvm, vm/cgame.qvm, vm/ui.qvm) and pack them as a pk3.
#
# WHY this exists
# ---------------
# The free Quake III *demo* pak0.pk3 ships the 1999 QVMs, whose UI reports API
# version 3. Our engine (external/quake3e, code/ui/ui_public.h UI_API_VERSION 6)
# rejects those with "User Interface is version 3, expected 6", so the game dies
# before the menu. A pak with version-6 QVMs makes Quake III run on the free demo
# data — no retail content is involved at any step.
#
# assets/quake3-qvm/pak1-ioq3-vms.pk3 used to be a hand-staged binary with no
# recipe in the repo. This script IS the recipe.
#
# WHERE THE SOURCES COME FROM
# ---------------------------
# external/quake3e is ENGINE-ONLY: code/{game,cgame,ui} contain nothing but the
# four interface headers (ui_public.h, cg_public.h, g_public.h, bg_public.h) —
# there are no *.c module sources to compile, and no QVM compiler either
# (code/tools/{lcc,asm} are absent). So both the VM sources AND the QVM compiler
# come from ioquake3, which is also how the 2026-08-05 hand-staged pak was made.
#
# The compatibility contract is therefore a HEADER CROSS-CHECK, not a shared
# source tree: the pre-flight below asserts that ioquake3's and quake3e's
# ui_public.h agree on UI_API_VERSION and that the value is 6. See the pre-flight
# comment for exactly what is and is not verified.
#
# REPRODUCIBILITY (measured 2026-09-03): this recipe reproduces the hand-staged
# 2026-08-05 pak bit-for-bit except for one 4-byte string — cgame.qvm and ui.qvm
# are byte-identical, and qagame.qvm differs only in the __DATE__ that g_main.c
# bakes into the "gamedate" cvar. The committed asset is therefore explained, not
# merely replaced.
#
# ioquake3 pin: 5883936 "Read and write CD key in lowercase" (2026-07-19).
# ioquake3 publishes NO release tags, so a SHA is the only immutable pin
# available. This is the revision at which the quake3e header cross-check was
# performed by hand (UI_API_VERSION 6, GAME_API_VERSION 8,
# CGAME_IMPORT_API_VERSION 4 all match quake3e), and at which the per-module
# source lists + q3asm link order below were transcribed. At this pin ioquake3
# has migrated to CMake, so the authoritative module lists live in
# cmake/basegame.cmake (add_qvm SOURCES) and the host-tool list in
# cmake/tools/CMakeLists.txt — NOT in a top-level Makefile any more. Bump the pin
# deliberately and re-transcribe both lists if you do.
#
# Licensing: ioquake3 and the resulting QVMs are GPL-2.0. That is fine here —
# this is game data plus a host-tools recipe living in the coordination repo.
# NEVER copy any of it into a sources/phoenix-rtos-* core repo.
#
# Usage:
#   ./tools/quake3-vm/build-quake3-vms.sh              # build into tools/quake3-vm/build/
#   ./tools/quake3-vm/build-quake3-vms.sh --install    # ALSO overwrite the committed asset
#
# Copyright 2026 Phoenix Systems
# Author: Witold Bołt
#
# SPDX-License-Identifier: BSD-3-Clause
set -u

# ioquake3 revision this recipe is pinned to (see header comment for why).
IOQ3_REF="588393618dbc82e7207c21c6ddecca229944a03a"
IOQ3_URL="https://github.com/ioquake/ioq3.git"

INSTALL=0
for arg in "$@"; do
	case "$arg" in
		--install) INSTALL=1 ;;
		-h|--help) sed -n '2,40p' "${BASH_SOURCE[0]:-$0}"; exit 0 ;;
		*) echo "unknown option: $arg" >&2; exit 2 ;;
	esac
done

# Repo root derived from this script's own location (portable across checkouts).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)"

IOQ3="$ROOT/external/ioquake3"
Q3E="$ROOT/external/quake3e"
HERE="$ROOT/tools/quake3-vm"
BUILD="$HERE/build"
TOOLBIN="$BUILD/tools"
ASMDIR="$BUILD/asm"
VMDIR="$BUILD/vm"
PK3="$BUILD/pak1-ioq3-vms.pk3"
ASSET="$ROOT/assets/quake3-qvm/pak1-ioq3-vms.pk3"
LOG="$BUILD/build.log"

HOSTCC="${HOSTCC:-cc}"
# The lcc sources are 1990s K&R-ish C: modern gcc defaults (C23, no implicit int,
# strict aliasing) reject or miscompile them, hence -std=gnu99 + these -Wno-*.
# The int<->pointer casts are how lcc's own IR is written; they are not bugs.
HOSTCFLAGS="${HOSTCFLAGS:--O2 -std=gnu99 -fno-strict-aliasing -w \
	-Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-unused-result}"

fail() { echo "FAIL: $*" >&2; exit 1; }

###############################################################################
# 1. ioquake3 clone, pinned. Mirrors the EXTERNAL_DEPS idiom in
#    scripts/bootstrap-linux-host.sh: clone, fetch the ref explicitly (a default
#    clone refspec need not bring an arbitrary SHA in), then check it out.
###############################################################################
if [ ! -d "$IOQ3/.git" ]; then
	echo "=== cloning $IOQ3_URL -> external/ioquake3 (pinning $IOQ3_REF) ==="
	git clone --quiet "$IOQ3_URL" "$IOQ3" || fail "git clone $IOQ3_URL failed (network?)"
else
	echo "=== external/ioquake3 present ==="
fi
if [ "$(git -C "$IOQ3" rev-parse HEAD 2>/dev/null)" != "$IOQ3_REF" ]; then
	git -C "$IOQ3" fetch --tags --quiet origin "$IOQ3_REF" 2>/dev/null || true
	git -C "$IOQ3" checkout --quiet "$IOQ3_REF" || fail "cannot check out pinned ref $IOQ3_REF"
fi
echo "=== ioquake3 @ $(git -C "$IOQ3" rev-parse --short HEAD) ==="

CODE="$IOQ3/code"
TOOLS="$CODE/tools"
[ -d "$TOOLS/lcc" ] && [ -d "$TOOLS/asm" ] || fail "ioquake3 code/tools/{lcc,asm} missing at this pin"
[ -d "$Q3E/code/ui" ] || fail "external/quake3e missing (see scripts/bootstrap-linux-host.sh)"

rm -rf "$BUILD"
mkdir -p "$TOOLBIN" "$ASMDIR" "$VMDIR"
: >"$LOG"

###############################################################################
# 2. Host tools: lburg -> dagcheck.c -> q3rcc, plus q3cpp, q3lcc, q3asm.
#    ALL FOUR of q3lcc/q3cpp/q3rcc/q3asm must land in ONE directory: q3lcc
#    locates q3cpp and q3rcc next to its own argv[0] (code/tools/lcc/etc/
#    bytecode.c: UpdatePaths), not via PATH.
#    Source lists transcribed from cmake/tools/CMakeLists.txt at the pin.
###############################################################################
# hostbuild <out-name> <source...>; EXTRA_CFLAGS adds per-target flags.
hostbuild() {
	local out="$1"; shift
	echo "=== HOSTCC $out ===" | tee -a "$LOG"
	# shellcheck disable=SC2086
	$HOSTCC $HOSTCFLAGS ${EXTRA_CFLAGS:-} -o "$TOOLBIN/$out" "$@" >>"$LOG" 2>&1 \
		|| { tail -30 "$LOG"; fail "host build of $out failed (see $LOG)"; }
}

LCC="$TOOLS/lcc"

hostbuild lburg "$LCC/lburg/lburg.c" "$LCC/lburg/gram.c"

echo "=== lburg dagcheck.md -> dagcheck.c ===" | tee -a "$LOG"
"$TOOLBIN/lburg" "$LCC/src/dagcheck.md" "$BUILD/dagcheck.c" >>"$LOG" 2>&1 \
	|| { tail -30 "$LOG"; fail "lburg failed on dagcheck.md"; }

Q3RCC_SRC=(alloc bind bytecode dag decl enode error event expr gen init inits input
	lex list main null output prof profio simp stmt string sym symbolic trace tree types)
rcc_files=()
for f in "${Q3RCC_SRC[@]}"; do rcc_files+=("$LCC/src/$f.c"); done
# q3rcc includes its own headers by bare name, so it needs -I on lcc/src.
EXTRA_CFLAGS="-I$LCC/src" hostbuild q3rcc "${rcc_files[@]}" "$BUILD/dagcheck.c"
EXTRA_CFLAGS=""

Q3CPP_SRC=(cpp lex nlist tokens macro eval include hideset getopt unix)
cpp_files=()
for f in "${Q3CPP_SRC[@]}"; do cpp_files+=("$LCC/cpp/$f.c"); done
hostbuild q3cpp "${cpp_files[@]}"

hostbuild q3lcc "$LCC/etc/lcc.c" "$LCC/etc/bytecode.c"
hostbuild q3asm "$TOOLS/asm/q3asm.c" "$TOOLS/asm/cmdlib.c"

for t in q3lcc q3cpp q3rcc q3asm; do
	[ -x "$TOOLBIN/$t" ] || fail "$t not produced"
done
echo "=== host QVM toolchain OK ($TOOLBIN) ==="

###############################################################################
# 3. Per-module source lists + link order, transcribed VERBATIM from
#    cmake/basegame.cmake at the pin (CGAME_SOURCES / GAME_SOURCES / UI_SOURCES,
#    each followed by GAME_MODULE_SHARED_SOURCES, then the module's *_QVM_SOURCES
#    .asm handed straight to q3asm).
#
#    ORDER IS LOAD-BEARING. q3asm lays the modules out in argument order and the
#    VM entry point is the start of the first object, so cg_main.c / g_main.c /
#    q3_ui/ui_main.c (which define vmMain) MUST come first, and the hand-written
#    *_syscalls.asm MUST come last. Do not sort these lists.
###############################################################################
CGAME_SOURCES="
	cgame/cg_main.c
	game/bg_misc.c game/bg_pmove.c game/bg_slidemove.c game/bg_lib.c
	cgame/cg_consolecmds.c cgame/cg_draw.c cgame/cg_drawtools.c cgame/cg_effects.c
	cgame/cg_ents.c cgame/cg_event.c cgame/cg_info.c cgame/cg_localents.c
	cgame/cg_marks.c cgame/cg_particles.c cgame/cg_players.c cgame/cg_playerstate.c
	cgame/cg_predict.c cgame/cg_scoreboard.c cgame/cg_servercmds.c
	cgame/cg_snapshot.c cgame/cg_view.c cgame/cg_weapons.c
"
GAME_SOURCES="
	game/g_main.c
	game/ai_chat.c game/ai_cmd.c game/ai_dmnet.c game/ai_dmq3.c game/ai_main.c
	game/ai_team.c game/ai_vcmd.c
	game/bg_misc.c game/bg_pmove.c game/bg_slidemove.c game/bg_lib.c
	game/g_active.c game/g_arenas.c game/g_bot.c game/g_client.c game/g_cmds.c
	game/g_combat.c game/g_items.c game/g_mem.c game/g_misc.c game/g_missile.c
	game/g_mover.c game/g_session.c game/g_spawn.c game/g_svcmds.c game/g_target.c
	game/g_team.c game/g_trigger.c game/g_utils.c game/g_weapon.c
"
UI_SOURCES="
	q3_ui/ui_main.c
	game/bg_misc.c game/bg_lib.c
	q3_ui/ui_addbots.c q3_ui/ui_atoms.c q3_ui/ui_cdkey.c q3_ui/ui_cinematics.c
	q3_ui/ui_confirm.c q3_ui/ui_connect.c q3_ui/ui_controls2.c q3_ui/ui_credits.c
	q3_ui/ui_demo2.c q3_ui/ui_display.c q3_ui/ui_gameinfo.c q3_ui/ui_ingame.c
	q3_ui/ui_loadconfig.c q3_ui/ui_menu.c q3_ui/ui_mfield.c q3_ui/ui_mods.c
	q3_ui/ui_network.c q3_ui/ui_options.c q3_ui/ui_playermodel.c q3_ui/ui_players.c
	q3_ui/ui_playersettings.c q3_ui/ui_preferences.c q3_ui/ui_qmenu.c
	q3_ui/ui_removebots.c q3_ui/ui_saveconfig.c q3_ui/ui_serverinfo.c
	q3_ui/ui_servers2.c q3_ui/ui_setup.c q3_ui/ui_sound.c q3_ui/ui_sparena.c
	q3_ui/ui_specifyserver.c q3_ui/ui_splevel.c q3_ui/ui_sppostgame.c
	q3_ui/ui_spskill.c q3_ui/ui_startserver.c q3_ui/ui_team.c q3_ui/ui_teamorders.c
	q3_ui/ui_video.c
"
# GAME_MODULE_SHARED_SOURCES — appended to every module, in this order.
SHARED_SOURCES="qcommon/q_math.c qcommon/q_shared.c"

# build_qvm <out-name> <-Ddefine> <syscalls.asm relpath> <c-source-list>
build_qvm() {
	local name="$1" define="$2" syscalls="$3" sources="$4"
	local out="$VMDIR/$name.qvm"
	local mod_asm="$ASMDIR/$name"
	local asm_list=() src base

	rm -rf "$mod_asm"; mkdir -p "$mod_asm"
	echo "=== q3lcc $define -> $name ($(echo $sources $SHARED_SOURCES | wc -w) sources) ==="
	for src in $sources $SHARED_SOURCES; do
		[ -f "$CODE/$src" ] || fail "missing VM source $CODE/$src (pin drift?)"
		base="$(basename "$src" .c)"
		# Q3_VM is injected by q3lcc's bytecode backend (etc/bytecode.c), so it
		# must NOT be passed here.
		"$TOOLBIN/q3lcc" "$define" -o "$mod_asm/$base.asm" "$CODE/$src" >>"$LOG" 2>&1 \
			|| { tail -30 "$LOG"; fail "q3lcc failed on $src (see $LOG)"; }
		asm_list+=("$mod_asm/$base.asm")
	done
	[ -f "$CODE/$syscalls" ] || fail "missing $CODE/$syscalls"
	asm_list+=("$CODE/$syscalls")

	echo "=== q3asm -> vm/$name.qvm ==="
	"$TOOLBIN/q3asm" -o "$out" "${asm_list[@]}" >>"$LOG" 2>&1 \
		|| { tail -30 "$LOG"; fail "q3asm failed for $name (see $LOG)"; }
}

build_qvm cgame  -DCGAME  cgame/cg_syscalls.asm "$CGAME_SOURCES"
build_qvm qagame -DQAGAME game/g_syscalls.asm   "$GAME_SOURCES"
build_qvm ui     -DUI     ui/ui_syscalls.asm    "$UI_SOURCES"

###############################################################################
# 4. Pack as a pk3 (a plain zip with vm/ paths).
###############################################################################
rm -f "$PK3"
if command -v zip >/dev/null 2>&1; then
	( cd "$BUILD" && zip -q -X "$PK3" vm/qagame.qvm vm/cgame.qvm vm/ui.qvm ) \
		|| fail "zip failed"
else
	# No zip(1) on this host — python's zipfile writes an equivalent archive.
	( cd "$BUILD" && python3 -m zipfile -c "$PK3" vm/qagame.qvm vm/cgame.qvm vm/ui.qvm ) \
		|| fail "python3 -m zipfile failed"
fi
echo "=== packed $PK3 ==="

###############################################################################
# 5. PRE-FLIGHT
#
# VERIFIED here:
#   * all three .qvm exist and are non-empty;
#   * each carries a QVM magic the engine accepts — VM_MAGIC 0x12721444 or
#     VM_MAGIC_VER2 0x12721445 (external/quake3e code/qcommon/qfiles.h, checked
#     by vm.c VM_LoadQVM), so they are real bytecode images and not stray text;
#   * UI_API_VERSION as the C preprocessor expands it is 6 in ioquake3's
#     ui_public.h (the header these QVMs were actually compiled against) AND 6
#     in quake3e's ui_public.h (the header the engine was compiled against).
#     ui_main.c answers UI_GETAPIVERSION with that macro verbatim, so agreement
#     here is what makes the engine's "expected 6" check pass. Same for
#     GAME_API_VERSION and CGAME_IMPORT_API_VERSION.
#   * the pk3 lists exactly the three expected vm/ entries.
#
# NOT VERIFIED here, deliberately and worth stating plainly:
#   * the version number is read out of the HEADERS, not decompiled out of the
#     bytecode. There is no cheap static way to read a return-constant out of a
#     QVM; a real check means running the VM.
#   * trap-number compatibility beyond the three API version macros. quake3e
#     drops the legacy 100..106 traps (MEMSET/MEMCPY/STRNCPY/SIN/COS/ATAN2/SQRT)
#     and adds engine extensions above them, while keeping FLOOR at 107. The
#     ioquake3 VM sources at this pin reference none of 100..106 (verified by
#     hand at pin time), and entityShared_t's leading field differs only in name
#     (ioq3 "unused" vs quake3e "s"), same type, same layout. Re-check both if
#     the pin is bumped.
#   * NOTHING here proves the freshly built QVMs render on the Pi. That needs an
#     HDMI-verified run (scripts/test-cycle-*.sh + artifacts/hdmi). Until then
#     --install is a deliberate, separate decision.
###############################################################################
echo "=== PRE-FLIGHT ==="
rc=0
for m in qagame cgame ui; do
	f="$VMDIR/$m.qvm"
	[ -s "$f" ] || { echo "[FAIL] vm/$m.qvm missing or empty"; rc=1; continue; }
	magic=$(od -An -tx4 -N4 "$f" | tr -d ' \n')
	case "$magic" in
		12721444|12721445) echo "[OK] vm/$m.qvm  $(stat -c%s "$f") bytes  magic 0x$magic" ;;
		*) echo "[FAIL] vm/$m.qvm bad magic 0x$magic (want 0x12721444/0x12721445)"; rc=1 ;;
	esac
done

# Expand each API macro with the real preprocessor rather than grepping, so a
# refactor into a computed value can't slip past this check.
api_version() { # <header-dir> <header> <macro>
	# A marker token keeps the expansion separable from the rest of the header,
	# which -E also emits.
	printf '#include "%s"\nQ3APIPROBE %s\n' "$2" "$3" \
		| $HOSTCC -E -P -I"$1" -xc - 2>/dev/null \
		| awk '/Q3APIPROBE/ { print $2 }' | tr -d '[:space:]'
}
for spec in "ui/ui_public.h UI_API_VERSION 6" \
            "game/g_public.h GAME_API_VERSION 8" \
            "cgame/cg_public.h CGAME_IMPORT_API_VERSION 4"; do
	set -- $spec
	hdr="$1"; macro="$2"; want="$3"
	vm_v=$(api_version "$CODE" "$hdr" "$macro")
	eng_v=$(api_version "$Q3E/code" "$hdr" "$macro")
	if [ "$vm_v" = "$want" ] && [ "$eng_v" = "$want" ]; then
		echo "[OK] $macro = $want in both ioquake3 (VM) and quake3e (engine) headers"
	else
		echo "[FAIL] $macro: ioquake3=$vm_v quake3e=$eng_v want=$want"
		rc=1
	fi
done

entries=$(python3 -c 'import sys,zipfile;print(" ".join(sorted(zipfile.ZipFile(sys.argv[1]).namelist())))' "$PK3")
if [ "$entries" = "vm/cgame.qvm vm/qagame.qvm vm/ui.qvm" ]; then
	echo "[OK] pk3 contains exactly vm/{qagame,cgame,ui}.qvm"
else
	echo "[FAIL] pk3 entries unexpected: $entries"; rc=1
fi

# Informational: fresh vs the committed asset. A difference here is NOT a
# failure — the committed pak is the HW-proven artifact and --install is opt-in.
#
# Measured 2026-09-03 at this pin: cgame.qvm and ui.qvm come out BYTE-IDENTICAL
# to the 2026-08-05 hand-staged ones, and qagame.qvm differs in exactly 4 bytes
# — the __DATE__ string that g_main.c bakes into the "gamedate" cvar
# ("Aug  5 2026" vs today). So this recipe reproduces the committed asset
# bit-for-bit modulo the build date, which is the strongest reproducibility
# evidence available without a Pi run. If a future run shows differences beyond
# that one string, the pin or the source lists have drifted — investigate before
# installing.
if [ -f "$ASSET" ]; then
	echo "--- fresh vs committed assets/quake3-qvm/pak1-ioq3-vms.pk3 ---"
	cmp_dir="$BUILD/committed"; rm -rf "$cmp_dir"; mkdir -p "$cmp_dir"
	( cd "$cmp_dir" && unzip -qo "$ASSET" ) || echo "  (could not unpack committed asset)"
	for m in qagame cgame ui; do
		a=$(stat -c%s "$VMDIR/$m.qvm" 2>/dev/null || echo -)
		b=$(stat -c%s "$cmp_dir/vm/$m.qvm" 2>/dev/null || echo -)
		if cmp -s "$VMDIR/$m.qvm" "$cmp_dir/vm/$m.qvm"; then
			verdict=identical
		else
			verdict="differs in $(cmp -l "$VMDIR/$m.qvm" "$cmp_dir/vm/$m.qvm" 2>/dev/null | wc -l) byte(s)"
		fi
		printf '  vm/%-12s fresh %8s  committed %8s  %s\n' "$m.qvm" "$a" "$b" "$verdict"
	done
fi

[ "$rc" -eq 0 ] || fail "pre-flight failed"

###############################################################################
# 6. Install (opt-in only).
###############################################################################
if [ "$INSTALL" -eq 1 ]; then
	cp "$PK3" "$ASSET" || fail "install to $ASSET failed"
	echo "=== INSTALLED -> $ASSET ==="
	echo "    Re-run scripts/stage-game-data.sh and HDMI-verify Quake III before committing this."
else
	echo "=== NOT installed (default). Committed asset untouched. ==="
	echo "    Result: $PK3"
	echo "    To adopt it: re-run with --install, then stage + HDMI-verify on the Pi."
fi
echo "=== quake3 QVMs OK ==="
