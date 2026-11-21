# Host differential harness for libphoenix's wchar implementation.
#
# Compiles libphoenix's multibyte layer NATIVELY -- wchar/wchar.c, wchar/wcwidth.c,
# wchar/uchar.c and locale/locale.c (setlocale) -- renames every function they
# define to ph_* with objcopy --redefine-sym, and compares each against the host
# glibc on the same inputs. No Pi cycle, no cross-build: it runs in seconds.
#
# The rename list is the UNION of the functions all four objects define, applied
# to all four, so their calls to each other (setlocale -> _wchar_setUtf8,
# wcwidth -> MB_CUR_MAX -> __ctype_get_mb_cur_max, mbrtoc16 -> mbrtowc) stay
# inside libphoenix. `check-refs` fails the build if any object still references
# an unrenamed name from that list -- that would silently call glibc instead --
# or any glibc `*_chk` entry point. The objects are compiled without
# _FORTIFY_SOURCE (on by default on Ubuntu), whose inline wrappers in glibc's
# headers would route e.g. wcrtomb() to glibc's own __wcrtomb_chk().
# Their undefined references to malloc/strtol/getenv still bind to glibc.
#
# The only source change needed is -Dcount=__count: Phoenix's mbstate_t names its
# field `count`, glibc's names it `__count`. No other identifier `count` appears
# in these files.
#
#   make && ./wdiff C          # the C/POSIX locale: single-byte, unchanged
#   make && ./wdiff C.UTF-8    # UTF-8: exhaustive code point / sequence / width runs
#
# Build out of tree with  make O=<dir>  to leave the tracked objects alone.

LIBPH ?= ../../sources/libphoenix
CFLAGS ?= -std=gnu11 -O1 -Wall
O ?= .

SRCS := wchar/wchar.c wchar/wcwidth.c wchar/uchar.c locale/locale.c
RAW := $(addprefix $(O)/,$(notdir $(SRCS:.c=_ph.o)))
OBJS := $(RAW:_ph.o=_phx.o)

all: $(O)/wdiff

PHFLAGS := -U_FORTIFY_SOURCE -Dcount=__count

$(O)/%_ph.o: $(LIBPH)/wchar/%.c
	$(CC) -c $(CFLAGS) $(PHFLAGS) -o $@ $<

$(O)/%_ph.o: $(LIBPH)/locale/%.c
	$(CC) -c $(CFLAGS) $(PHFLAGS) -o $@ $<

$(O)/redef.txt: $(RAW)
	nm $^ | awk '$$2 == "T" {print $$3}' | sort -u | awk '{printf "--redefine-sym %s=ph_%s ", $$1, $$1}' > $@

$(O)/%_phx.o: $(O)/%_ph.o $(O)/redef.txt
	objcopy $$(cat $(O)/redef.txt) $< $@

check-refs: $(OBJS) $(O)/redef.txt
	@bad=$$(nm -u $(OBJS) | awk '{print $$2}' | sort -u | \
		grep -Fxf <(sed 's/--redefine-sym \([^=]*\)=[^ ]* */\1\n/g' $(O)/redef.txt | grep .) || true); \
	bad="$$bad$$(nm -u $(OBJS) | awk '{print $$2}' | grep '_chk$$' | grep -v '^__stack_chk_fail$$' || true)"; \
	if [ -n "$$bad" ]; then echo "UNRENAMED references (would call glibc): $$bad"; exit 1; fi; \
	echo "check-refs: no unrenamed libphoenix references"

$(O)/wdiff: wdiff.c $(OBJS) | check-refs
	$(CC) $(CFLAGS) -o $@ $^

run: $(O)/wdiff
	$(O)/wdiff C
	$(O)/wdiff C.UTF-8

clean:
	rm -f $(O)/wdiff $(O)/*_ph.o $(O)/*_phx.o $(O)/redef.txt

.PHONY: all run clean check-refs
SHELL := /bin/bash
