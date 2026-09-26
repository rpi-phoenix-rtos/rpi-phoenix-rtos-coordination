#!/bin/bash
# Extract and VALIDATE the C1 allocator evidence from a UART log.
#
# The point of this script is the validation. The UART capture mangles characters
# under load -- 4 of 202 hex literals in one 2026-09-23 log contained a character
# that is not even hex, and a digit swapped for another DIGIT leaves no trace at
# all. A `caller=` value read straight out of a log has twice symbolized to a
# plausible-looking function that turned out to have no call instruction before
# it. So malloc prints ~caller as `callerx=`, and nothing here symbolizes an
# address whose complement does not check out.
#
# Usage: scripts/c1-report.sh [label|path]   (default: newest log)
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ART="$REPO/artifacts/rpi4b-uart"
SYMDIR="$REPO/artifacts/c1-symbols"
A2L="$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-addr2line"
OBJDUMP="$REPO/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-objdump"

arg="${1:-}"
if [ -n "$arg" ] && [ -f "$arg" ]; then
	log="$arg"
elif [ -n "$arg" ]; then
	log="$(ls -t "$ART"/*"$arg"*.log 2>/dev/null | head -1)"
else
	log="$(ls -t "$ART"/*.log 2>/dev/null | head -1)"
fi
[ -n "${log:-}" ] && [ -f "$log" ] || { echo "no log found for '${arg}'" >&2; exit 1; }

# ⚠ This picks the NEWEST archived ELF, which is only right when the log came
# from that same build. SuperTuxKart is relinked by every --scope core build and
# `caller=` is an absolute address in a no-ASLR static binary, so symbolizing an
# older log against a newer ELF yields a plausible-looking WRONG function. It did
# exactly that on 2026-09-25, resolving a 09-24 log against f2efc4b0, a binary
# that did not exist when that log was written. There is nothing in a UART log
# that identifies the binary, and the archived ELFs all carry copy-times rather
# than build-times, so this cannot be resolved automatically -- say so instead of
# quietly implying a match.
elf="$(ls -t "$SYMDIR"/*.elf 2>/dev/null | head -1)"
nelf="$(ls -1 "$SYMDIR"/*.elf 2>/dev/null | wc -l)"

echo "log:  $(basename "$log")"
if [ -n "$elf" ]; then
	echo "syms: $(basename "$elf")"
	if [ "$nelf" -gt 1 ]; then
		echo "      ⚠ newest of $nelf archived ELFs, NOT matched to this log. Any symbol below is"
		echo "        only valid if this log came from that exact binary; otherwise it is noise."
		echo "        Archived: $(ls -1 "$SYMDIR"/*.elf | xargs -n1 basename | tr '\n' ' ')"
	fi
else
	echo "syms: <none archived>"
fi
echo

plain() { sed 's/\x1b\[[0-9;]*m//g' "$log"; }

# --- how trustworthy is this log at all? -------------------------------------
tot=$(grep -aoE "0x[0-9a-fA-F]{4,}" "$log" | wc -l)
bad=$(grep -aoE "0x[0-9a-fA-F]*[g-zG-Z][0-9a-fA-F]*" "$log" | wc -l)
echo "== log integrity =="
echo "   hex literals: $tot ; visibly malformed: $bad  (digit->digit corruption is INVISIBLE)"
echo

# --- guard fires --------------------------------------------------------------
echo "== allocator guard fires =="
for pat in "free() of a corrupt chunk header" \
           "small-bin head is not a chunk" \
           "large-bin lookup returned" \
           "chunk handed out twice" \
           "mmap returned a region OVERLAPPING"; do
	n=$(grep -ac "$pat" "$log")
	[ "$n" -gt 0 ] && echo "   $n x  $pat"
done
echo

# --- the corrupted heap headers ----------------------------------------------
echo "== heap-header readings =="
plain | grep -aoE "h(size|lo32|hhi32|hi32|fixed) *= 0x[0-9a-f]*" | sort | uniq -c | head -20
echo

# --- callers, validated before symbolizing ------------------------------------
echo "== caller attribution (validated) =="
callers=$(plain | grep -aoE "caller= 0x[0-9a-f]+" | grep -aoE "0x[0-9a-f]+")
callerxs=$(plain | grep -aoE "callerx= 0x[0-9a-f]+" | grep -aoE "0x[0-9a-f]+")

if [ -z "$callers" ]; then
	echo "   (no caller= lines)"
else
	i=0
	while read -r c; do
		i=$((i + 1))
		x=$(echo "$callerxs" | sed -n "${i}p")
		if [ -z "$x" ]; then
			echo "   caller $c -- NO callerx (pre-checksum build): UNVALIDATED, do not symbolize"
			continue
		fi
		# caller ^ callerx must be all ones
		chk=$(python3 -c "print('%016x' % ((int('$c',16) ^ int('$x',16)) & 0xffffffffffffffff))" 2>/dev/null)
		if [ "$chk" != "ffffffffffffffff" ]; then
			echo "   caller $c / callerx $x -> XOR=$chk MANGLED, discarded"
			continue
		fi
		echo -n "   caller $c VALID -> "
		if [ -n "$elf" ]; then
			"$A2L" -f -C -e "$elf" "$c" 2>/dev/null | head -1
			# The check that caught two false attributions: a return address is
			# preceded by a branch-with-link. If it is not, the symbol is a lie.
			prev=$(printf '0x%x' $(( $(printf '%d' "$c") - 4 )))
			ins=$("$OBJDUMP" -d --start-address="$prev" --stop-address="$c" "$elf" 2>/dev/null | tail -1)
			case "$ins" in
				*bl*|*blr*) echo "        preceded by a call: OK" ;;
				*)          echo "        ⚠ NOT preceded by a call -- not a return address, symbol is meaningless" ;;
			esac
		else
			echo "(no symbols archived)"
		fi
	done <<< "$callers"
fi
echo

# --- mailbox-PA correlation ---------------------------------------------------
# The decisive C1 comparison. The corrupting write is a 32-bit
# 0x80000000/0x80000001 at page+4, and a VideoCore property buffer is an mmap'd
# PAGE whose msg[1] -- exactly page+4 -- is where the firmware writes precisely
# those two response codes. So if a poisoned page that later breaks carries the
# SAME physical page as one of the in-process mailbox request buffers, the
# mechanism is named outright.
#
# rpi4-vcmbox is NOT a candidate: it serialises through one PERSISTENT bounce
# buffer (it logs buf_pa once and never frees it), so its PA is printed here only
# as the reference that should NEVER match.
echo "== mailbox-PA correlation =="
mbox_pa=$(plain | grep -oE 'mbox req buf pa=0x[0-9a-f]+' | grep -oE '0x[0-9a-f]+' | sort -u)
# Both signatures now carry a physical page: p4pa from a poison break, hpa from a
# corrupt-header fire. The header path is much the commoner of the two -- run
# c1pa1 @22:22 gave 139 header fires and zero breaks -- so relying on p4pa alone
# left the decisive comparison waiting on the rarer event.
brk_pa=$(plain | grep -oE '(p4pa|hpa) +=[ ]*0x[0-9a-f]+' | grep -oE '0x[0-9a-f]+' | sed 's/^0x0*/0x/' | sort -u)
vcm_pa=$(plain | grep -oE 'buf_pa=0x[0-9a-f]+' | grep -oE '0x[0-9a-f]+' | sort -u)
if [ -z "$mbox_pa" ] && [ -z "$brk_pa" ]; then
	echo "   (no mailbox PA log and no poison break in this run)"
else
	echo "   in-process mbox request pages: $(echo ${mbox_pa:-none} | tr '\n' ' ')"
	echo "   corrupted page PAs (hpa/p4pa): $(echo ${brk_pa:-none} | tr '\n' ' ')"
	echo "   vcmbox persistent buffer:      ${vcm_pa:-none}  (must NOT match)"
	hit=""
	for b in $brk_pa; do
		bp=$(printf "0x%x" $(( $b & ~0xfff )))
		for m in $mbox_pa; do
			mp=$(printf "0x%x" $(( $m & ~0xfff )))
			[ "$bp" = "$mp" ] && hit="$hit $bp"
		done
	done
	if [ -n "$hit" ]; then
		echo "   *** MATCH on page(s):$hit -- a broken page WAS a mailbox request buffer"
	elif [ -n "$brk_pa" ] && [ -n "$mbox_pa" ]; then
		echo "   no match: these broken pages were never in-process mailbox buffers"
	fi
fi
echo

# --- WHERE in the page did the write land? ------------------------------------
# The experiment that tests the FIXED-OFFSET premise. The genet / xHCI / ADMA2
# suspects were excluded on the grounds that "their writes vary in offset" --
# a property nothing could observe while only ONE word per page (+4) was poisoned,
# which made "the corruption always lands at page+4" unfalsifiable.
#
# ⚠ THE ARM MUST BE READ BEFORE THE RESULT. "Only +4 broke" is the expected
# outcome BOTH when the writer is offset-specific AND when +4 was the only offset
# armed -- opposite conclusions from identical output. The allocator prints a
# one-shot banner under -DC1_P4_WIDE for exactly this reason, so the log is
# self-describing and a reader months later needs no access to the binary.
echo "== probe-offset distribution (tests the fixed-offset premise) =="
# ⚠ NOT `plain | grep -q`. This script sets `pipefail`, and `grep -q` exits on the
# FIRST match, which SIGPIPEs sed; pipefail then propagates sed's 141 and the
# `if` takes the ELSE branch even though the string is present. It is a RACE --
# standalone the sed often finishes first and it looks fine -- so it fooled a
# direct test before `bash -x` showed the branch. Left unfixed it would have made
# every WIDE run read as NARROW, silently: exactly the misreading this guard
# exists to prevent. `grep -c` consumes all input, so no SIGPIPE.
if [ "$(plain | grep -c 'p4 probe arm = WIDE')" -gt 0 ]; then
	echo "   arm: WIDE (4 offsets armed -- the question is answerable in this run)"
	_off=$(plain | grep -oE 'p4off  = 0x[0-9a-f]+' | grep -oE '0x[0-9a-f]+' | sed 's/^0x0*/0x/' | sort | uniq -c | sort -rn)
	if [ -z "$_off" ]; then
		echo "   no poison break in this run -- says nothing about offsets"
	else
		echo "$_off" | sed 's/^/   broke at /'
		_n=$(echo "$_off" | grep -c .)
		if [ "$_n" -le 1 ]; then
			echo "   ⇒ only ONE offset broke: consistent with an OFFSET-SPECIFIC writer,"
			echo "     which is what the genet/xHCI/ADMA2 exclusions assume."
		else
			echo "   ⇒ *** $_n DIFFERENT offsets broke: the writer does NOT hit a fixed offset."
			echo "     The fixed-offset framing is wrong, and every exclusion built on it"
			echo "     (genet, xHCI, SD ADMA2) REOPENS."
		fi
	fi
else
	echo "   arm: single probe (+0x4 only) -- this build cannot answer the question."
	echo "   ⚠ Do NOT read 'only +4 broke' as evidence of an offset-specific writer here:"
	echo "     +4 is the only place this build looked. Rebuild with"
	echo "     LIBC_DIAG=\"-DC1_P4_WIDE\" to make it answerable."
fi
echo

# --- BO attribution of the corrupted FRAME ------------------------------------
# The measurement that separates the two readings of the KEEP_CLOSED_BO result:
# under "recycling is merely necessary" the victim frame is ordinary pool churn
# and was never a BO; under "recycling is the TRIGGER" it was one.
#
# Both detectors report it, because a fire arrives through either and wiring it
# to one costs a whole fire (run c1pfn1: two poison breaks, zero header reports,
# no attribution at all):
#   header path : hbopa / hbotot / hbonpg / hboord
#   poison path : p4bopa / p4botl / p4bonp / p4bord
#
# ⚠ "0 matches out of N closes" is a REAL answer, not a missing instrument --
# it says the driver closed N BOs and this frame was not among them. The case
# that means nothing is tot==0, which says the lookup never ran.
echo "== BO attribution of the corrupted frame =="
_bo_line() {
	local label="$1" nre="$2" tre="$3" ore="$4"
	local n t o
	n=$(plain | grep -oE "$nre" | grep -oE '0x[0-9a-f]+' | tail -1)
	t=$(plain | grep -oE "$tre" | grep -oE '0x[0-9a-f]+' | tail -1)
	o=$(plain | grep -oE "$ore" | grep -oE '0x[0-9a-f]+' | tail -1)
	if [ -z "$t" ]; then
		printf '   %-12s (not reported -- no fire on this path, or build predates it)\n' "$label"
		return
	fi
	if [ "$((t))" -eq 0 ]; then
		printf '   %-12s ⚠ closes recorded = 0 -- the lookup never ran; says NOTHING\n' "$label"
	elif [ -n "$n" ] && [ "$((n))" -gt 0 ]; then
		printf '   %-12s *** WAS A CLOSED BO -- %s match(es) of %s closes, closed %s ordinal(s) ago\n' \
			"$label" "$((n))" "$((t))" "$(( $((t)) - $((${o:-0})) ))"
	else
		printf '   %-12s not a closed BO -- 0 matches out of %s recorded closes (a real answer)\n' \
			"$label" "$((t))"
	fi
}
_bo_line "header:" 'hbopa = 0x[0-9a-f]+' 'hbotot= 0x[0-9a-f]+' 'hboord= 0x[0-9a-f]+'
_bo_line "poison:" 'p4bopa= 0x[0-9a-f]+' 'p4botl= 0x[0-9a-f]+' 'p4bord= 0x[0-9a-f]+'
echo

# --- live-heap PA coincidence (does NOT need C1 to fire) ----------------------
# The correlation above is FIRE-GATED: hpa/p4pa exist only once a guard trips. On
# five clean trials of series #3 that meant 24 mailbox PAs per run and not one
# heap PA to compare them with -- the decisive test could not even be attempted.
#
# That is a bad place to be, because the archive says instruments SUPPRESS C1
# (five in a row drove the rate to 0), so a design that needs the event to fire
# is fighting its own instrumentation. `capa` (C1_HEAP_TRACE_ALL) logs the
# physical page of EVERY heap creation, which reframes the question into one a
# CLEAN run can answer: does a mailbox request buffer's physical page ever
# coincide with a live heap's?
#
# Read it carefully, in both directions:
#   a MATCH means the firmware owns a page the allocator also owns -- the
#     mechanism, demonstrated without needing corruption to occur;
#   NO match over many heaps is evidence AGAINST the hypothesis, but only in
#     proportion to how many heaps were sampled -- capa is capped at 64 per
#     process, so quote the sample size with the verdict, never "no match" alone.
echo "== live-heap PA coincidence (capa vs mailbox, no fire needed) =="
capa=$(plain | grep -oE 'capa +=[ ]*0x[0-9a-f]+' | grep -oE '0x[0-9a-f]+' | sed 's/^0x0*/0x/' | sort -u)
if [ -z "$capa" ]; then
	echo "   (no all-trace in this run -- arm with C1_HEAP_TRACE_ALL=1 on the command itself)"
elif [ -z "$mbox_pa" ]; then
	echo "   heap pages sampled: $(echo "$capa" | grep -c .)  but NO mailbox PA logged -- nothing to compare"
else
	# ⚠ A raw PA match is NOT evidence on its own. Phoenix recycles physical
	# frames across mmap/munmap constantly, so a heap that was RELEASED and whose
	# frame the v3d driver later took for a mailbox buffer produces capa==mbox_pa
	# while the two never coexisted. That is benign reuse, and printing it as a
	# discovery would be the worst kind of false positive -- the report's
	# strongest-worded line, earned by nothing.
	#
	# So the match has to be temporal. `carel` records each page at RELEASE time
	# (emitted while still mapped, so va2pa can answer). A coincidence counts only
	# if the heap page was logged BEFORE the mailbox line and was NOT released in
	# between; otherwise it is reuse and is reported as such.
	chit=""; creuse=""
	mbox_ln=$(plain | grep -n 'mbox req buf pa' | head -1 | cut -d: -f1)
	for c in $capa; do
		cp=$(printf "0x%x" $(( c & ~0xfff )))
		matched=0
		for m in $mbox_pa; do
			mp=$(printf "0x%x" $(( m & ~0xfff )))
			[ "$cp" = "$mp" ] && matched=1
		done
		[ "$matched" = "0" ] && continue
		# Was this page released before the first mailbox line?
		# (carel = a malloc HEAP release; see below for the mailbox side.)
		rel_ln=$(plain | grep -n "carel  = .*$(printf '%x' $(( c & ~0xfff )))" | head -1 | cut -d: -f1)
		if [ -n "$rel_ln" ] && [ -n "$mbox_ln" ] && [ "$rel_ln" -lt "$mbox_ln" ]; then
			creuse="$creuse $cp"
		else
			chit="$chit $cp"
		fi
	done
	echo "   heap pages sampled:   $(echo "$capa" | grep -c .)  (per-PAGE; budget 512 lines/process)"
	echo "   pages released again: $(plain | grep -c 'carel  =')"
	echo "   mailbox pages:        $(echo $mbox_pa | tr '\n' ' ')"
	if [ -n "$chit" ]; then
		echo "   *** COINCIDENCE on page(s):$chit -- heap still LIVE when the mailbox took the page"
		# Which side of the coincidence is dangerous depends on whether the
		# FIRMWARE was finished with the page. The driver logs a RELEASED line
		# only on the success path, i.e. only once its response had surfaced --
		# so a mailbox page with a matching RELEASED line is one VideoCore is
		# provably done with, and a match on it is harmless recycling.
		#
		# A mailbox page with NO release line is the whole point: that is the
		# shape of the one route left untested after release-by-munmap was
		# refuted -- a process exiting with a property call in flight, where the
		# kernel reclaims the page with no munmap and nothing to count it.
		for h in $chit; do
			hs=$(printf '%x' $(( h )))
			# Same pipefail/grep -q race as the arm check above -- and here the
			# false branch is the DANGEROUS one: a spurious miss reports "firmware
			# may still own it", turning benign reuse into an apparent smoking gun.
			if [ "$(plain | grep -c "RELEASED pa=0x0*$hs")" -gt 0 ]; then
				echo "       $h: firmware HAD finished (RELEASED logged) -- benign recycling"
			else
				echo "       $h: ⚠ NO release logged for this mailbox page -- firmware may still own it"
			fi
		done
	fi
	if [ -n "$creuse" ]; then
		echo "   benign PA reuse on:$creuse -- heap was released BEFORE the mailbox line, so they never coexisted"
	fi
	if [ -z "$chit" ] && [ -z "$creuse" ]; then
		echo "   no coincidence in this sample -- weakens the mailbox route IN PROPORTION to the sample above"
	fi
fi
echo

# --- hunt instruments ---------------------------------------------------------
echo "== instruments =="
# ⚠ 0 here does NOT mean STK created no victim heap. Since 2026-09-25 this trace
# is env-gated (C1_HEAP_TRACE=1) and OFF by default, because it was four blocking
# debug() writes per creation -- ~200 ms of UART inside heap creation, in every
# process -- and it was added after the last fire. On a default run 0 is expected;
# `export C1_HEAP_TRACE=1` before the app to arm it as a positive control.
c1_created=$(grep -ac 'C1-hunt: created' "$log")
if [ "$c1_created" = "0" ]; then
	echo "   victim-size heap creations: 0  (trace OFF by default -- export C1_HEAP_TRACE=1 to arm)"
else
	echo "   victim-size heap creations: $c1_created  (trace ran; pre-2026-09-25 builds counted 0xd000 ONLY, later ones 0xd000+0x2000)"
fi
echo "   implausible gc header: $(grep -ac 'implausible gc header' "$log")"
echo "   EL0/EL1 exceptions:    $(grep -ac '(EL0)' "$log") / $(grep -ac '(EL1)' "$log")"
