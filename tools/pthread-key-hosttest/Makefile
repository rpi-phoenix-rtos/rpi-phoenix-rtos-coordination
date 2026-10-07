# Host harness for libphoenix's pthreads: keys (thread-specific data), thread
# lifetime and the clocks of condition variables.
#
# Builds libphoenix's REAL pthread/pthread.c for the host and runs Unity suites
# of phoenix-rtos-tests against it, with the Phoenix system calls it makes
# modelled on host threads (shim.c):
#
#   tsd       libc/pthread_tsd       keys, lock-free lookups
#   lifetime  libc/pthread_lifetime  joins, detached exits, cancels
#   cond      libc/pthread/pthread_cond_clock.c
#                                    timed waits on either clock, wall-clock
#                                    steps; run twice, the second time with
#                                    the shim's futexWait() waking up
#                                    spuriously (SHIM_SPURIOUS)
#
# Run it under ThreadSanitizer (data races, lock misuse) and AddressSanitizer
# (use after free). Independently of either, the shim aborts on a release of
# a lock the caller does not hold, and each run fails if a thread stack that
# pthread.c mapped is left mapped at the end (see main.c).
#
#   make run                       build + run every suite against sources/libphoenix
#   make run SAN=thread            ... under ThreadSanitizer
#   make run SAN=address           ... under AddressSanitizer
#   make run SUITES=lifetime       ... one suite
#   make run LIBPH=/path/to/wt     ... against another libphoenix checkout
#   make run SUITES=tsd ARGS='-v -n getspecific_cost'
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
SUITES ?= tsd lifetime cond
SRC_tsd      ?= $(TESTS)/libc/pthread_tsd/pthread_tsd.c
SRC_lifetime ?= $(TESTS)/libc/pthread_lifetime/pthread_lifetime.c
SRC_cond     ?= $(TESTS)/libc/pthread/pthread_cond_clock.c
# The header that renames the suite's calls, and host files linked in
RENAME_cond  := rename-cond.h
EXTRA_cond   := cond-main.c
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

all: $(SUITES:%=$(OUT)/%-host)

# Which libphoenix the objects came from: switching LIBPH must rebuild them even
# when the other checkout's pthread.c is the OLDER file
$(OUT)/libph.path: FORCE
	mkdir -p $(OUT)
	echo '$(abspath $(LIBPH))' | cmp -s - $@ || echo '$(abspath $(LIBPH))' > $@

$(OUT)/inc/phoenix:
	mkdir -p $(OUT)/inc
	ln -sfn $(abspath $(KERNEL))/include $@

$(OUT)/ph.o: $(LIBPH)/pthread/pthread.c $(LIBPH)/sys/list.c $(LIBPH)/sys/ulock.c $(OUT)/libph.path | $(OUT)/inc/phoenix
	$(HCC) $(PHFLAGS) -c -o $(OUT)/pthread.o $(LIBPH)/pthread/pthread.c
	$(HCC) $(PHFLAGS) -c -o $(OUT)/list.o $(LIBPH)/sys/list.c
	# ulock.c's spin hint is aarch64 assembly, which the host's assembler rejects
	sed 's/"yield"/"pause"/' $(LIBPH)/sys/ulock.c > $(OUT)/ulock.c
	$(HCC) $(PHFLAGS) -iquote $(LIBPH)/sys -c -o $(OUT)/ulock.o $(OUT)/ulock.c
	ld -r -o $(OUT)/ph-raw.o $(OUT)/pthread.o $(OUT)/list.o $(OUT)/ulock.o
	nm -g --defined-only $(OUT)/ph-raw.o | awk '{print $$3 " ph_" $$3}' > $(OUT)/redef.txt
	nm -u $(OUT)/ph-raw.o | awk '{print $$2}' | grep -v -x -E '$(subst $(SPACE),|,$(KEEP))|__tsan_.*|__asan_.*' \
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

define suite
$$(OUT)/$(1)-host: $$(OUT)/ph.o $$(OUT)/shim.o $$(OUT)/stubs.c main.c rename.h $$(RENAME_$(1)) $$(EXTRA_$(1)) $$(SRC_$(1))
	$$(HCC) $$(HOSTFLAGS) -Wno-unused-function -I$$(UNITY) -include $$(or $$(RENAME_$(1)),rename.h) -c -o $$(OUT)/$(1).o $$(SRC_$(1))
	$$(HCC) $$(HOSTFLAGS) -I$$(UNITY) -o $$@ main.c $$(EXTRA_$(1)) $$(OUT)/$(1).o $$(OUT)/ph.o $$(OUT)/shim.o $$(OUT)/stubs.c \
		$$(UNITY)/unity.c $$(UNITY)/unity_fixture.c -lpthread
endef
$(foreach s,tsd lifetime cond,$(eval $(call suite,$(s))))

run: $(SUITES:%=$(OUT)/%-host)
	@rc=0; for s in $(SUITES); do \
		echo "=== $$s"; \
		TSAN_OPTIONS="suppressions=$(CURDIR)/tsan.supp $(TSAN_OPTIONS)" ./$(OUT)/$$s-host $(ARGS) || rc=1; \
		if [ $$s = cond ]; then \
			echo "=== $$s (SHIM_SPURIOUS)"; \
			SHIM_SPURIOUS=1 TSAN_OPTIONS="suppressions=$(CURDIR)/tsan.supp $(TSAN_OPTIONS)" ./$(OUT)/$$s-host $(ARGS) || rc=1; \
		fi; \
	done; exit $$rc

clean:
	rm -rf build build-*

.PHONY: all run clean FORCE
