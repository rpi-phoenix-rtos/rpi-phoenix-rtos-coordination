# dl-hosttest: runs libphoenix's dl.c and the libc/dlopen Unity tests on the build
# host, under qemu-aarch64 (user mode). See README.md.
#
#   make check                  new dl.c must pass every test
#   make check-old OLD_REF=...  dl.c at OLD_REF must FAIL (the tests catch the old code)
#   make check-bundle BUNDLE=<libWPEInjectedBundle.so> [EXTENSION=<phx-probe-extension.so>]
#                               load WPE's injected bundle and call WKBundleInitialize (which
#                               then loads the extension)
#
# Copyright 2026 Phoenix Systems
# SPDX-License-Identifier: BSD-3-Clause

ROOT       ?= $(abspath ../..)
CROSS      ?= $(ROOT)/.toolchain/aarch64-phoenix/bin/aarch64-phoenix-
SYSROOT    ?= $(ROOT)/.buildroot/_build/aarch64a72-generic-rpi4b/sysroot
LIBPHOENIX ?= $(ROOT)/sources/libphoenix
TESTS      ?= $(ROOT)/sources/phoenix-rtos-tests
QEMU       ?= $(firstword $(wildcard /opt/qemu-11/bin/qemu-aarch64) qemu-aarch64)
OLD_REF    ?= master
OUT        ?= out

CC    := $(CROSS)gcc
STRIP := $(CROSS)strip

DLOPEN := $(TESTS)/libc/dlopen
UNITY  := $(TESTS)/unity

# the target's code generation (phoenix-rtos-build target/aarch64.mk + Makefile.common)
TFLAGS := --sysroot=$(SYSROOT) -mcpu=cortex-a72 -mtune=cortex-a72 -mstrict-align -mno-outline-atomics \
	-O2 -ffunction-sections -fdata-sections -std=gnu17 -Wall -Wstrict-prototypes -Wundef -Werror -fno-common
CFLAGS := $(TFLAGS) -fomit-frame-pointer -I$(UNITY) -DDLTEST_DIR=\"$(abspath $(OUT))\"

# the program: static, and the export table exactly as libc/dlopen/Makefile links it
LDFLAGS := -nostdlib -static -Wl,-z,max-page-size=0x1000 -Wl,--gc-sections \
	-Wl,--no-dynamic-linker -Wl,--dynamic-list=$(DLOPEN)/exports.list
LIBS := $(SYSROOT)/lib/libphoenix.a -lgcc

# the plugins, exactly as libc/dlopen/Makefile builds them
PLUGIN_FLAGS := $(TFLAGS) -fomit-frame-pointer -fPIC -shared -nostartfiles -nostdlib -Wl,-z,max-page-size=0x1000 -Wl,--gc-sections
PLUGINS := $(OUT)/test-libc-dlopen-import.so $(OUT)/test-libc-dlopen-weak.so \
	$(OUT)/test-libc-dlopen-gnuhash.so $(OUT)/test-libc-dlopen-tls.so

COMMON_OBJS := $(OUT)/shim.o $(OUT)/dlopen.o $(OUT)/main.o $(OUT)/unity.o $(OUT)/unity_fixture.o

.PHONY: all check check-old check-bundle clean
all: $(OUT)/dltest $(OUT)/dltest-old $(PLUGINS)

$(OUT):
	mkdir -p $@

$(OUT)/shim.o: shim.c | $(OUT)
	$(CC) $(CFLAGS) -fno-builtin -c $< -o $@
$(OUT)/dlopen.o: $(DLOPEN)/dlopen.c $(DLOPEN)/dltest.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/main.o: $(DLOPEN)/main.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/unity.o: $(UNITY)/unity.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/unity_fixture.o: $(UNITY)/unity_fixture.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/dl.o: $(LIBPHOENIX)/dl/dl.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/dl-old.c: FORCE | $(OUT)
	git -C $(LIBPHOENIX) show $(OLD_REF):dl/dl.c > $@
$(OUT)/dl-old.o: $(OUT)/dl-old.c
	$(CC) $(CFLAGS) -c $< -o $@

# installed stripped, as on the target: the program file has no .symtab
$(OUT)/dltest: $(COMMON_OBJS) $(OUT)/dl.o
	$(CC) $(TFLAGS) $(LDFLAGS) $^ $(LIBS) -o $@.unstripped
	$(STRIP) -o $@ $@.unstripped
$(OUT)/dltest-old: $(COMMON_OBJS) $(OUT)/dl-old.o
	$(CC) $(TFLAGS) $(LDFLAGS) $^ $(LIBS) -o $@.unstripped
	$(STRIP) -o $@ $@.unstripped

$(OUT)/test-libc-dlopen-%.so: $(DLOPEN)/plugins/%.c $(DLOPEN)/dltest.h | $(OUT)
	$(CC) $(PLUGIN_FLAGS) -Wl,--hash-style=$(if $(filter gnuhash,$*),gnu,sysv) $< -o $@

check: $(OUT)/dltest $(PLUGINS)
	$(QEMU) $(abspath $(OUT)/dltest) -v

# exits 0 only when the old dl.c FAILS the tests
check-old: $(OUT)/dltest-old $(PLUGINS)
	@if $(QEMU) $(abspath $(OUT)/dltest-old) -v; then echo "dl-hosttest: dl.c at $(OLD_REF) PASSED -- the tests do not catch it"; exit 1; \
	else echo "dl-hosttest: dl.c at $(OLD_REF) fails, as expected"; fi

# the injected bundle against a mock of its three imports, the export list as wpe-browser's
$(OUT)/bundle-check.o: bundle-check.c | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/bundle-check: $(OUT)/shim.o $(OUT)/bundle-check.o $(OUT)/dl.o bundle-check.exports
	$(CC) $(TFLAGS) -nostdlib -static -Wl,-z,max-page-size=0x1000 -Wl,--gc-sections \
		-Wl,--no-dynamic-linker -Wl,--dynamic-list=bundle-check.exports \
		$(OUT)/shim.o $(OUT)/bundle-check.o $(OUT)/dl.o $(LIBS) -o $@.unstripped
	$(STRIP) -o $@ $@.unstripped

check-bundle: $(OUT)/bundle-check
	@test -n "$(BUNDLE)" || { echo "dl-hosttest: set BUNDLE=<libWPEInjectedBundle.so>"; exit 2; }
	$(QEMU) $(abspath $(OUT)/bundle-check) $(abspath $(BUNDLE)) $(if $(EXTENSION),$(abspath $(EXTENSION)))

clean:
	rm -rf $(OUT)

FORCE:
