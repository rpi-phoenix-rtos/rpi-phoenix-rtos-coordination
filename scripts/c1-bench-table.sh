#!/bin/bash
# Per-trial C1 table across a set of UART logs sharing a label prefix.
#
# Exists because grading a C1 bench by hand is easy to get wrong in the two
# ways this project has already been burnt by:
#   - a 0-fire run that never ran the workload grades identically to a clean
#     one, so FRAMES is printed next to FIRES and a trial with 0 frames is
#     marked VOID rather than counted;
#   - "armed" is the C1_HEAP_TRACE heap-creation counter, which is OFF by
#     default since 2026-09-25, so 0 there is expected and is NOT evidence the
#     detector was absent. The detectors (the chunk poisons) are always on.
#
# Usage: scripts/c1-bench-table.sh <label-prefix>      e.g. c1base
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
art="$repo/artifacts/rpi4b-uart"
pref="${1:-}"
if [ -z "$pref" ]; then
	echo "usage: $(basename "$0") <label-prefix>" >&2
	exit 2
fi

shopt -s nullglob
logs=("$art"/*"$pref"*.log)
if [ ${#logs[@]} -eq 0 ]; then
	echo "no logs matching '*${pref}*.log' under $art" >&2
	exit 1
fi

# ⚠ The label is matched as a SUBSTRING, so a short one silently pulls in
# unrelated runs: "c1s" also matches c1stk, c1sweep, c1solo, c1soak and
# c1smoke, which on 2026-09-25 turned a 1-trial series into a 19-trial
# aggregate mixing three days of archived runs. Warn when the matched logs
# span more than a day -- a single bench never does.
_days=$(printf '%s\n' "${logs[@]}" | sed 's/.*rpi4b-uart-\([0-9]\{8\}\)-.*/\1/' | sort -u | wc -l)
if [ "$_days" -gt 1 ]; then
	printf '⚠ label %s matches logs from %s different DAYS -- probably over-matching.\n' "$pref" "$_days"
	printf '  dates: %s\n\n' "$(printf '%s\n' "${logs[@]}" | sed 's/.*rpi4b-uart-\([0-9]\{8\}\)-.*/\1/' | sort -u | tr '\n' ' ')"
fi
printf '%-36s %5s %5s %6s %5s %6s %8s %5s %5s %5s %s\n' LOG SIG VICT GUARD CORR ARMED FRAMES KFLT UFLT TDOWN VERDICT
tot_f=0; tot_fire=0; valid=0; void=0
for log in $(printf '%s\n' "${logs[@]}" | sort); do
	# ⚠ Count EVERY allocator guard, not just the corrupt-header one. The
	# `why   =` line belongs to one guard; the page-poison guard prints
	# "POISON BROKEN" and no `why`. Counting only `why` called
	# c1armA-3 clean when it had 4 poison breaks, and produced a bogus
	# "C1 has gone quiet" reading across the whole archive on 2026-09-25.
	# Two independent columns, because neither alone is trustworthy:
	#
	#  SIG   the C1 signature itself -- 0x80000000/0x80000001 sitting in the HIGH
	#        half of a 64-bit word, however it is reported (hhi32, p4got, a
	#        corrupted hsize/heap). This is the selector to believe: it does not
	#        care which guard printed it.
	#  GUARD any allocator guard message at all. Kept because a guard can fire
	#        WITHOUT the signature (that is a different defect, e.g. c1keep-4's
	#        "small-bin head is not a chunk"), and because it catches new guards.
	#
	# Guard-NAME counting was tried and is fragile: malloc_dl.c can emit 20+
	# distinct literals, and picking a subset produced two wrong conclusions on
	# 2026-09-25 -- first `why   =` alone (which misses the page-poison guard
	# entirely), then a five-message list that still omitted "free-bin link is not
	# a plausible chunk", which has fired in 17 archived logs.
	fires=$(grep -acE 'hhi32 = 0x0*8000000[01]|p4got  = 0x0*8000000[01]|= 0x8000000[01][0-9a-f]{8}' "$log")
	# Exclude the C1-hunt TRACE's own lines. They are malloc: prefixed, and with
	# the trace armed they dwarf everything else -- one run showed GUARD=923 whose
	# entire content was 179 trace reports and zero actual guards. An instrument
	# must not inflate the metric it is being read alongside.
	#
	# ⚠ ALSO exclude UART-CORRUPTED lines, or this column measures the serial link
	# rather than the allocator. Measured 2026-09-25: ~5 of every ~375 malloc trace
	# lines (~1.3%) arrive with a bit-error, and a single corrupted character
	# defeats the exclusion regex above -- "C1-hunt: created" arriving as "Cn-hunt:
	# created" or "C1 hunt: created" is no longer excluded and counts as a guard.
	# That is the whole content of GUARD=5/9/6 on the three clean c1hpa trials:
	# zero real guards. Corruption is unambiguous where a fixed-width hex field is
	# involved -- one line read `0x00F00000091U6000`, and `U` is not a hex digit --
	# so treat a `= 0x` line whose payload is not exactly 16 hex digits as corrupt,
	# count it separately in CORR, and keep it out of GUARD.
	#
	# CORR is reported rather than silently dropped because link quality is itself
	# evidence: at a non-zero bit-error rate a corrupted digit could in principle
	# fabricate or erase a C1 signature, so a run with an unusual CORR deserves a
	# second look before its SIG is believed.
	# ⚠ Corruption lands mostly in the LABEL, not the hex payload: `c6base`,
	# `c1sizes`, `c1req 8=` all carry a perfectly well-formed 16-digit value. A
	# filter aimed at the hex field therefore changes nothing -- which is exactly
	# what the first attempt at this did, leaving GUARD at 5/9/6.
	#
	# So count guard EVENTS structurally instead of counting lines. Every guard
	# announces itself with a PROSE line and then prints its fields as `label =
	# 0x...`; dropping every line containing `0x` leaves one line per event and no
	# field lines at all, with no list of guard literals to get wrong. The only
	# prose this build emits routinely is the trace's "created a victim-size heap",
	# and all three corrupted variants seen (`Cn-hunt`, `C1 hunt`, `he)p`) still
	# contain "created a victim-size", so a loose match on that substring excludes
	# them too.
	#
	# Result on the three clean c1hpa trials: GUARD 5/9/6 -> 0/0/0. There were
	# never any guards; the column was reporting serial-link bit-errors.
	# Chasing each corruption SHAPE with another regex is a losing game -- the
	# residue after the first two attempts was still all noise (`cr=ated`,
	# `victim-siIe`, `acvictim-size`, and hex whose `0x` itself was mangled into
	# `0U0`/`hx0`/`0s0`, which slips past a `0x` filter). So classify POSITIVELY
	# instead: match the distinctive words real guards use, every one of which is
	# absent from the trace line. The union below is derived mechanically from the
	# source, so it can be regenerated rather than guessed:
	#
	#   grep -aoE 'debug\("malloc: [^"]*"' sources/libphoenix/stdlib/malloc_dl.c
	#
	# A single-character corruption can still break one of these matches and lose
	# a guard -- but that line then fails every classifier and lands in CORR, so
	# it shows up as unexplained rather than vanishing. That is the property that
	# makes this safe where the earlier guard-NAME list was not: nothing is
	# silently dropped, and a NEW guard message not in the union also lands in
	# CORR. Eyeball CORR whenever it is non-zero.
	_gre='gone bad|NOT MAPPED|POISON BROKEN|handed out twice|double free|plausible chunk'
	_gre="$_gre"'|WRITTEN TO|corrupt chunk header|plausible heap size|not releasing it'
	_gre="$_gre"'|ABANDONED|large-bin|small-bin|OVERLAPPING|munmap of a released'
	_gre="$_gre"'|LIVE HEAP BASE|wild pointer|not transplanting|leaking the'
	_mtot=$(grep -ac 'malloc: ' "$log")
	_mfield=$(grep -acE 'malloc:.*= 0x[0-9a-f]{16}([ \r]*)$' "$log")
	_mtrace=$(grep -acE 'malloc: C1-hunt: created a victim-size heap([ \r]*)$' "$log")
	guards=$(grep -acE "$_gre" "$log")
	# CORR: everything that is neither a well-formed field, nor an exact trace
	# line, nor a guard event -- i.e. the corrupted residue. Reported rather than
	# silently dropped because link quality is itself evidence: at a non-zero
	# bit-error rate a corrupted digit could in principle fabricate or erase a C1
	# signature, so a run with an unusual CORR deserves a second look before its
	# SIG is believed.
	corr=$(( _mtot - _mfield - _mtrace - guards ))
	if [ "$corr" -lt 0 ]; then corr=0; fi
	# VICT: DISTINCT corrupted heaps. SIG counts report LINES, and one corrupt
	# heap is re-reported on every later free from it -- run c1pa1 @22:22 showed
	# SIG=552 from just FOUR distinct heaps. Quoting SIG as an event count
	# overstates the evidence by two orders of magnitude.
	vict=$(grep -aoE 'heap  = 0x[0-9a-f]{16}' "$log" | sort -u | wc -l)
	armed=$(grep -ac 'C1-hunt: created' "$log")
	frames=$(grep -ao 'total [0-9]*)' "$log" | tail -1 | tr -dc 0-9)
	frames=${frames:-0}
	# ⚠ EVERY fault dump reaches the UART TWICE, so a raw line count is 2x the
	# truth. process_dumpException() (kernel proc/process.c:259-261) emits the
	# SAME buffer down two paths: hal_consolePrint() to the kernel console AND
	# posix_write(2, ...) to the faulting process's stderr -- which on this bench
	# is also the UART. Verified 2026-09-25 as a fixed 2:1 ratio of "Exception #"
	# lines to fault events across four independent logs (6:3, 6:3, 2:1, 2:1),
	# and visible directly in c1hpa01, where another process's output interleaves
	# BETWEEN the two copies. So halve, and split by exception level: an EL1
	# entry is a kernel fault and must be 0, an EL0 entry killed a user process
	# and may be the INSTRUMENT's own doing rather than the workload's.
	#
	# ⚠ The doubling is EL0-ONLY, and getting this wrong is worse than not
	# splitting at all. An EL1 dump is a KERNEL fault: there is no live user
	# stderr to write to, so only the console copy appears -- measured on
	# c1pw-on-216762 (2026-09-24), which carries exactly ONE "(EL1)" line. Had
	# KFLT been halved too, that log would have graded KFLT=0 by integer
	# division and a kernel fault would have vanished from the table. Halve the
	# EL0 count; take the EL1 count as-is.
	kflt=$(grep -ac 'Exception #[0-9]*:.*(EL1)' "$log")
	uflt=$(( $(grep -ac 'Exception #[0-9]*:.*(EL0)' "$log") / 2 ))
	if [ "$frames" -eq 0 ]; then
		verdict=VOID; void=$((void+1))
	else
		verdict=ok; valid=$((valid+1)); tot_f=$((tot_f+frames)); tot_fire=$((tot_fire+fires))
	fi
	# TDOWN: did the app run to completion and tear down? Measured 2026-09-25:
	# the two runs that reached STK's profile summary produced 79 of the 108
	# archived fires (78 in one), because the exit free-storm re-walks every heap
	# and re-detects corruption that happened earlier. It is a detection
	# AMPLIFIER, not a precondition -- 6 of the 8 firing runs never got there and
	# still fired 1-12 times. A bench of runs that all show no is not void, but it
	# is weaker than one that completes.
	if grep -aq 'Number of frames:' "$log"; then tdown=yes; else tdown=no; fi
	printf '%-36s %5s %5s %6s %5s %6s %8s %5s %5s %5s %s\n' "$(basename "$log" .log | cut -c11-)" \
		"$fires" "$vict" "$guards" "$corr" "$armed" "$frames" "$kflt" "$uflt" "$tdown" "$verdict"
	# A UFLT is only interesting once you know WHOSE fault it was, and this bench
	# has already mistaken its own instrument's crash for a property of the run
	# (2026-09-25, c1hpa01: a malloc_dl guard dereferencing an unmapped live[]
	# entry killed /bin/ntpclient, and the raw count read as "the workload
	# faulted twice"). Name the victim and the PC so the reader can addr2line it
	# rather than guess; the PC is build-specific, so resolve it against the
	# prog/ ELF of the build that produced THIS log.
	if [ "$uflt" -gt 0 ]; then
		while read -r _p; do
			printf '    ^ EL0 %s  victim(s): %s\n' "$_p" \
				"$(grep -aoE 'process "[^"]*"' "$log" | sort -u | tr '\n' ' ')"
		done < <(grep -aoE 'pc=[0-9a-f]{16} esr=[0-9a-f]{16} far=[0-9a-f]{16}' "$log" | sort -u)
	fi
done

echo "---"
printf 'valid trials: %s   void (0 frames, NOT counted): %s\n' "$valid" "$void"
printf 'C1-signature hits across valid trials: %s   frames: %s\n' "$tot_fire" "$tot_f"
