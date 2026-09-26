# Differential harness for libphoenix's stdio FILE layer, on the host.
#
#   make run   drives open_memstream()/fmemopen() (and a plain fopen() file)
#              through identical operation sequences on libphoenix's
#              stdio/file.c + stdio/memstream.c and on glibc, and compares
#              every observable result: return values, errno on failure,
#              ftell(), bytes read, and the buffer/size the caller sees.
#
#   LIBPH=/path/to/libphoenix make run   checks another tree (e.g. a worktree).
#
# The real file.c is compiled against libphoenix's own <stdio.h> (its FILE
# layout) with shim/ standing in for the Phoenix-only headers; its public
# symbols are then renamed ph_* with objcopy so glibc's stay usable for the
# comparison. Nothing under sources/ is modified.

LIBPH  ?= ../../sources/libphoenix
CFLAGS ?= -std=gnu11 -O1 -g
# MEM=0: a tree without stdio/memstream.c (runs the `file` scenario only, whose
# digest is then comparable with a tree that has it)
MEM    ?= 1

ifeq ($(MEM),0)
STDOBJS = file_raw.o
DRVFLAGS = -DNO_MEM
else
STDOBJS = file_raw.o memstream_raw.o
DRVFLAGS =
endif
PHOBJS = $(STDOBJS:_raw.o=_ph.o)

PHFLAGS = -D_GNU_SOURCE -Ishim \
	-DPH_STDIO_H='"$(LIBPH)/include/stdio.h"' \
	-DPH_SYS_LIST_H='"$(LIBPH)/include/sys/list.h"' \
	-DPH_SYS_MINMAX_H='"$(LIBPH)/include/sys/minmax.h"'

all: stdiff

file_raw.o: $(LIBPH)/stdio/file.c
	$(CC) -c $(CFLAGS) $(PHFLAGS) -w -o $@ $<

memstream_raw.o: $(LIBPH)/stdio/memstream.c
	$(CC) -c $(CFLAGS) $(PHFLAGS) -w -o $@ $<

list.o: $(LIBPH)/sys/list.c
	$(CC) -c $(CFLAGS) $(PHFLAGS) -w -o $@ $<

# Rename every global symbol the two stdio objects DEFINE (T/D/B/R), in both
# objects, so their calls to each other stay bound together.
redef.txt: $(STDOBJS)
	nm --defined-only -g $(STDOBJS) | awk 'NF==3 {print $$3 " ph_" $$3}' | sort -u > $@

file_ph.o: file_raw.o redef.txt
	objcopy --redefine-syms=redef.txt $< $@

memstream_ph.o: memstream_raw.o redef.txt
	objcopy --redefine-syms=redef.txt $< $@

stdiff: stdiff.c $(PHOBJS) list.o support.o
	$(CC) $(CFLAGS) $(DRVFLAGS) -Wall -o $@ $^ -lpthread

support.o: support.c
	$(CC) -c $(CFLAGS) -Wall -o $@ $<

run: stdiff
	./stdiff

clean:
	rm -f stdiff unity-test unity-runner.c *.o redef.txt redef-unity.txt

# `make unity`: the phoenix-rtos-tests Unity groups stdio_memstream and
# stdio_fmemopen, compiled against libphoenix's <stdio.h> with -D__phoenix__ (so
# the Phoenix-only assertions run too) and linked to the renamed libphoenix
# stdio -- the target test, executed on the host.
TESTS ?= ../../sources/phoenix-rtos-tests
UNITY_GROUPS = stdio_memstream stdio_fmemopen

fmt_%_raw.o: $(LIBPH)/stdio/%.c
	$(CC) -c $(CFLAGS) $(PHFLAGS) -w -o $@ $<

unity_t_raw.o: $(TESTS)/libc/stdio/stdio_memstream.c
	$(CC) -c $(CFLAGS) $(PHFLAGS) -D__phoenix__ -I$(TESTS)/unity -o $@ $<

UNITY_PH = file_raw.o memstream_raw.o fmt_fprintf_raw.o fmt_format_raw.o fmt_bignum_raw.o

redef-unity.txt: $(UNITY_PH)
	nm --defined-only -g $(UNITY_PH) | awk 'NF==3 {print $$3 " ph_" $$3}' | sort -u > $@

%_uph.o: %_raw.o redef-unity.txt
	objcopy --redefine-syms=redef-unity.txt $< $@

unity-runner.c:
	printf '#include <stdlib.h>\n#include "unity_fixture.h"\nextern void ph__file_init(void);\nstatic void runner(void) {' > $@
	for g in $(UNITY_GROUPS); do printf ' RUN_TEST_GROUP(%s);' $$g >> $@; done
	printf ' }\nint main(int argc, char *argv[]) { ph__file_init(); return (UnityMain(argc, (const char **)argv, runner) == 0) ? EXIT_SUCCESS : EXIT_FAILURE; }\n' >> $@

unity-test: unity-runner.c $(UNITY_PH:_raw.o=_uph.o) unity_t_uph.o list.o support.o
	$(CC) $(CFLAGS) -I$(TESTS)/unity -o $@ unity-runner.c $(UNITY_PH:_raw.o=_uph.o) unity_t_uph.o list.o support.o \
		$(TESTS)/unity/unity.c $(TESTS)/unity/unity_fixture.c -lpthread

unity: unity-test
	./unity-test -v

.PHONY: all run clean unity
