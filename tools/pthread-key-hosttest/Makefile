# Host harness for libphoenix's pthread keys (thread-specific data).
#
# Builds libphoenix's REAL pthread/pthread.c for the host and runs the Unity
# group pthread_tsd (phoenix-rtos-tests libc/pthread_tsd) against it, with the
# Phoenix system calls it makes modelled on host threads (shim.c). Run it under
# ThreadSanitizer to check the lock-free lookups for data races:
#
#   make run                       build + run against sources/libphoenix
#   make run SAN=thread            ... under ThreadSanitizer
#   make run LIBPH=/path/to/wt     ... against another libphoenix checkout
#   make run ARGS='-v -n getspecific_cost'
#                                  Unity arguments (one case, verbose)
#
# pthread.c is compiled against libphoenix's own headers with -nostdinc. They
# select the architecture from the compiler's macros, and the x86 ones are the
# 32-bit ia32 port, so the build poses as aarch64 (LP64, like the host). The
# object's own symbols are renamed ph_*, and every call it makes outside the
# host C library is renamed shim_* -- implemented in shim.c, or aborting via
# the generated stubs.c so an unmodelled path cannot pass silently.

ROOT   ?= ../..
LIBPH  ?= $(ROOT)/sources/libphoenix
KERNEL ?= $(ROOT)/sources/phoenix-rtos-kernel
TESTS  ?= $(ROOT)/sources/phoenix-rtos-tests
SRC    ?= $(TESTS)/libc/pthread_tsd/pthread_tsd.c
UNITY  ?= $(TESTS)/unity
# clang or gcc; Ubuntu ships the ThreadSanitizer runtime with gcc only
HCC    ?= gcc
SAN    ?=
OPT    ?= -O2

SANFLAGS := $(if $(SAN),-fsanitize=$(SAN))
OUT := build$(if $(SAN),-$(SAN))

# Symbols pthread.c may take from the host C library as they are
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
KEEP := malloc calloc realloc free memcpy memset strlen strcpy abort __errno_location fprintf stderr

PHFLAGS := -std=gnu11 $(OPT) -g -Wall -nostdinc -U__x86_64__ -D__aarch64__ \
	-isystem $(shell $(HCC) -print-file-name=include) -I$(LIBPH)/include -I$(OUT)/inc \
	-fno-builtin -fno-stack-protector $(SANFLAGS)
HOSTFLAGS := -std=gnu11 $(OPT) -g -Wall $(SANFLAGS)

all: $(OUT)/tsd-host

$(OUT)/inc/phoenix:
	mkdir -p $(OUT)/inc
	ln -sfn $(abspath $(KERNEL))/include $@

$(OUT)/ph.o: $(LIBPH)/pthread/pthread.c $(LIBPH)/sys/list.c | $(OUT)/inc/phoenix
	$(HCC) $(PHFLAGS) -c -o $(OUT)/pthread.o $(LIBPH)/pthread/pthread.c
	$(HCC) $(PHFLAGS) -c -o $(OUT)/list.o $(LIBPH)/sys/list.c
	ld -r -o $(OUT)/ph-raw.o $(OUT)/pthread.o $(OUT)/list.o
	nm -g --defined-only $(OUT)/ph-raw.o | awk '{print $$3 " ph_" $$3}' > $(OUT)/redef.txt
	nm -u $(OUT)/ph-raw.o | awk '{print $$2}' | grep -v -x -E '$(subst $(SPACE),|,$(KEEP))|__tsan_.*' \
		| awk '{print $$1 " shim_" $$1}' >> $(OUT)/redef.txt
	objcopy --redefine-syms=$(OUT)/redef.txt $(OUT)/ph-raw.o $@

$(OUT)/shim.o: shim.c
	mkdir -p $(OUT)
	$(HCC) $(HOSTFLAGS) -c -o $@ $<

# An aborting stub for every shim_* call that shim.c does not model
$(OUT)/stubs.c: $(OUT)/ph.o $(OUT)/shim.o
	nm -u $(OUT)/ph.o | awk '$$2 ~ /^shim_/ {print $$2}' | LC_ALL=C sort > $(OUT)/needed.txt
	nm --defined-only $(OUT)/shim.o | awk '$$3 ~ /^shim_/ {print $$3}' | LC_ALL=C sort > $(OUT)/have.txt
	LC_ALL=C comm -23 $(OUT)/needed.txt $(OUT)/have.txt | awk ' \
		BEGIN { print "#include <stdio.h>"; print "#include <stdlib.h>" } \
		{ n = substr($$1, 6); printf "void %s(void)\n{\n\tfprintf(stderr, \"shim: %s() not modelled\\n\");\n\tabort();\n}\n", $$1, n }' > $@

$(OUT)/tsd-host: $(OUT)/ph.o $(OUT)/shim.o $(OUT)/stubs.c main.c rename.h $(SRC)
	$(HCC) $(HOSTFLAGS) -Wno-unused-function -I$(UNITY) -include rename.h -c -o $(OUT)/test.o $(SRC)
	$(HCC) $(HOSTFLAGS) -o $@ main.c $(OUT)/test.o $(OUT)/ph.o $(OUT)/shim.o $(OUT)/stubs.c \
		$(UNITY)/unity.c $(UNITY)/unity_fixture.c -lpthread

run: $(OUT)/tsd-host
	TSAN_OPTIONS="suppressions=$(CURDIR)/tsan.supp $(TSAN_OPTIONS)" ./$(OUT)/tsd-host $(ARGS)

clean:
	rm -rf build build-*

.PHONY: all run clean
