# Guard-page + differential harness for libphoenix's string.c, on the host.
#
#   make           build
#   make run       1368 bound checks; expect overreads=0 wrong=0
#   make canfail   REPROVE the harness by rebuilding it against a copy of
#                  string.c with the historical strncmp over-read put back.
#                  It must report SFAULT strncmp. A harness that cannot fail
#                  proves nothing, and this one has a known defect to catch.
#
# No Pi cycle and no cross-build. string.c needs one macro from the target-only
# arch.h, supplied verbatim in shim/.

LIBPH  ?= ../../sources/libphoenix
CFLAGS ?= -std=gnu11 -O1 -Wall
SRC     = $(LIBPH)/string/string.c

all: sdiff

string_phx.o: $(SRC)
	$(CC) -c $(CFLAGS) -Ishim -o string_ph.o $<
	nm string_ph.o | grep ' T ' | awk '{printf "--redefine-sym %s=ph_%s ", $$3, $$3}' > redef.txt
	objcopy $$(cat redef.txt) string_ph.o $@

sdiff: sdiff.c string_phx.o
	$(CC) $(CFLAGS) -o $@ $^

run: sdiff
	./sdiff

# Put the 2026-08 strncmp defect back (bound tested AFTER the dereference) and
# confirm the guard page catches it.
canfail: sdiff.c
	sed "s|for (p = us1, k = 0; (k < n) && (\*p != '\\\\0'); p++, k++) {|for (p = us1, k = 0; (*p != '\\\\0') \&\& (k < n); p++, k++) {|" $(SRC) > .buggy.c
	$(CC) -c $(CFLAGS) -Ishim -o .buggy.o .buggy.c
	nm .buggy.o | grep ' T ' | awk '{printf "--redefine-sym %s=ph_%s ", $$3, $$3}' > .redef_bug.txt
	objcopy $$(cat .redef_bug.txt) .buggy.o .buggyx.o
	$(CC) $(CFLAGS) -o .sdiff_bug sdiff.c .buggyx.o
	@echo "--- expect SFAULT strncmp below; silence here means the harness is broken ---"
	@./.sdiff_bug | grep -m3 'SFAULT strncmp' || (echo "CANFAIL FAILED: harness did not catch the known defect"; exit 1)
	@echo "canfail OK"

clean:
	rm -f sdiff string_ph.o string_phx.o redef.txt .buggy.c .buggy.o .buggyx.o .redef_bug.txt .sdiff_bug

.PHONY: all run canfail clean
