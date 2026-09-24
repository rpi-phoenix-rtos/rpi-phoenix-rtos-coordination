# Host differential harness for libphoenix's wchar implementation.
#
# Compiles libphoenix/wchar/wchar.c NATIVELY, renames its exported symbols to
# ph_* with objcopy --redefine-sym (so its internal calls to malloc/strtol still
# bind to glibc), and compares every function against the host glibc on the same
# inputs. No Pi cycle, no cross-build: it runs in about a second.
#
# The only source change needed is -Dcount=__count: Phoenix's mbstate_t names its
# field `count`, glibc's names it `__count`, and wchar.c touches it exactly once
# (mbsinit). No other identifier `count` appears in the file.
#
#   make && ./wdiff C          # compare against glibc's C locale  <- the contract
#   make && ./wdiff C.UTF-8    # compare against UTF-8 (expect many by-design diffs)

LIBPH ?= ../../sources/libphoenix
CFLAGS ?= -std=gnu11 -O1 -Wall

all: wdiff

wchar_phx.o: $(LIBPH)/wchar/wchar.c
	$(CC) -c $(CFLAGS) -Dcount=__count -o wchar_ph.o $<
	nm wchar_ph.o | grep ' T ' | awk '{printf "--redefine-sym %s=ph_%s ", $$3, $$3}' > redef.txt
	objcopy $$(cat redef.txt) wchar_ph.o $@

wdiff: wdiff.c wchar_phx.o
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f wdiff wchar_ph.o wchar_phx.o redef.txt

.PHONY: all clean
