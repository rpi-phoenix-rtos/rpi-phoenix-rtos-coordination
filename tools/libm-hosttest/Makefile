# Host accuracy harness for libphoenix's libm.
#
# Compiles BOTH libm implementations shipped in libphoenix on the host:
#   phx  = libm/phoenix/*.c + libm/phoenix/msun/*.c  (the DEFAULT, LIBM_USE_LIBMCS=n;
#          complex.c excluded), with the hardware fast paths of <arch.h>
#          (sqrt/floor/ceil/trunc/round/fabs as single instructions)
#   phs  = the same sources WITHOUT those fast paths: the software fallbacks
#          that targets with no such instructions run (section 2 only)
#   mcs  = libm/libmcs/libm/{common,mathd,mathf}  (LIBM_USE_LIBMCS=y)
# links each into one relocatable object, renames every exported symbol to
# phx_* / phs_* / mcs_* (objcopy --redefine-syms renames the internal call
# sites too, so phx_pow calls phx_log, never glibc's), and measures the ULP
# error of every function against MPFR at 256 bits (correctly rounded).
# The build uses -mfma -ffp-contract=fast, i.e. gcc fuses a*b+c into an FMA
# exactly as it does by default on aarch64; libm/phoenix/msun/ gets the
# -ffp-contract=off the libphoenix build gives it (MSUN_CFLAGS).
# glibc's own double functions are measured the same way as a baseline column.
#
#   make run LIBPH=/path/to/libphoenix-worktree   # measure another tree
#   make run N=200000                             # more random samples/range
#   make run MSUN_CFLAGS=                         # msun/ WITH FMA contraction
#   make gen-refs                                 # tests' reference header
#
# Exit status is non-zero when any phx function exceeds its ULP budget (see
# harness.c) or disagrees with glibc on an Annex F special case.

LIBPH ?= ../../sources/libphoenix
N ?= 20000
BUILD ?= build

# Flags of the real libphoenix build (phoenix-rtos-build/Makefile.common), minus
# the warnings: libmcs is third-party code and warns on the host compiler.
COMMON_CFLAGS := -O2 -std=gnu17 -fno-strict-aliasing -ffunction-sections -fdata-sections -fno-common
# libphoenix libm/Makefile builds libm/phoenix/msun/ with these
MSUN_CFLAGS ?= -ffp-contract=off

# libmcs sources (the mcs column). Since the 2026-10 upstream sync libm/libmcs is
# a git submodule: `git -C sources/libphoenix submodule update --init libm/libmcs`,
# or MCSROOT=<a tree that has it>.
MCSROOT ?= $(LIBPH)/libm/libmcs
MCS_INC := -I$(MCSROOT)/libm/include -I$(MCSROOT)/libm/common

# The headers libm/phoenix compiles against: libm/include plus the
# libm_feature_config.h the libphoenix build generates (2026-10 sync and later),
# or, before that, the vendored libmcs tree's with its internal_config.h forced in.
ifneq ($(wildcard $(LIBPH)/libm/include/math.h),)
  PHX_FCFG := $(BUILD)/fcfg/libm_feature_config.h
  PHX_INC := -I$(LIBPH)/libm/include -I$(BUILD)/fcfg
  PHX_CFG :=
  # libphoenix builds the libmcs sources against the same headers
  # (LIBM_USE_LIBMCS=y: -Ilibm/include -I$(BUILD_DIR)/include), not libmcs's
  # own libm/include, whose libm_internal_config.h needs a generated
  # libm_feature_config.h too.
  MCS_INC := $(PHX_INC) -I$(MCSROOT)/libm/common
else
  PHX_FCFG :=
  PHX_INC := $(MCS_INC)
  PHX_CFG := -include $(LIBPH)/libm/libmcs/libm/include/internal_config.h
endif
PHX_SRCS := $(filter-out %/complex.c,$(wildcard $(LIBPH)/libm/phoenix/*.c)) $(wildcard $(LIBPH)/libm/phoenix/msun/*.c)
PHX_HDRS := $(wildcard $(LIBPH)/libm/phoenix/*.h $(LIBPH)/libm/phoenix/msun/*.h)
MCS_SRCS := $(filter-out %/fenv.c %/cmplx.c,$(wildcard $(MCSROOT)/libm/common/*.c)) \
	$(wildcard $(MCSROOT)/libm/mathd/*.c) $(wildcard $(MCSROOT)/libm/mathd/internal/*.c) \
	$(wildcard $(MCSROOT)/libm/mathf/*.c) $(wildcard $(MCSROOT)/libm/mathf/internal/*.c)

# Symbols that must never be left UNDEFINED in a renamed libm object: if one
# is, that libm silently called glibc's implementation and the numbers lie.
MATHSYMS := '^(a?(sin|cos|tan)h?|atan2|exp(2|10|m1)?|log(2|10|1p)?|pow|sqrt|cbrt|hypot|erfc?|[lt]gamma|fmod|remainder|modf|frexp|ldexp|scalbn|floor|ceil|trunc|round|rint|fabs|copysign)f?$$'

ifeq ($(PHX_SRCS),)
  $(error no libm/phoenix sources under LIBPH=$(LIBPH))
endif

# $(call build_phx,CC,CFLAGS,ARCH_INCLUDE_DIR,OBJDIR)
# Object names keep the subdirectory (msun_e_exp.o) so none collide.
define build_phx
	@rm -rf $(4) && mkdir -p $(4)
	@for f in $(PHX_SRCS); do \
		extra=; case $$f in */msun/*) extra="$(MSUN_CFLAGS)";; esac; \
		o=$(4)/$$(echo $$f | sed 's|.*/libm/phoenix/||; s|/|_|g; s|\.c$$|.o|'); \
		$(1) -c $(2) $$extra -I$(3) $(PHX_INC) $(PHX_CFG) -o $$o $$f || exit 1; done
endef

# $(call build_mcs,CC,CFLAGS,OBJDIR)
define build_mcs
	@rm -rf $(3) && mkdir -p $(3)
	@for f in $(MCS_SRCS); do \
		o=$(3)/$$(echo $$f | sed 's|.*/libm/||; s|/|_|g; s|\.c$$|.o|'); \
		$(1) -c $(2) -w $(MCS_INC) -o $$o $$f || exit 1; done
endef

all: $(BUILD)/x86/harness

# As the libphoenix build generates it with its defaults (LIBM_WANT_COMPLEX=y, LIBM_USE_LIBMCS=n)
$(BUILD)/fcfg/libm_feature_config.h: $(LIBPH)/libm/include/libm_feature_config.h.in
	@mkdir -p $(@D)
	sed -e 's|#undef LIBMCS_WANT_COMPLEX|#define LIBMCS_WANT_COMPLEX 1|' $< > $@

# --- x86-64 / MPFR ---------------------------------------------------------
X86_CC := gcc
X86_CFLAGS := $(COMMON_CFLAGS) -mfma -ffp-contract=fast
X86_TOOLS := ld nm objcopy

$(BUILD)/x86/phx.o: $(PHX_SRCS) $(PHX_HDRS) $(PHX_FCFG) inc/arch.h Makefile
	$(call build_phx,$(X86_CC),$(X86_CFLAGS),inc,$(BUILD)/x86/phx)
	./rename.sh phx $(BUILD)/x86/phx $@ $(X86_TOOLS) $(MATHSYMS)

$(BUILD)/x86/phs.o: $(PHX_SRCS) $(PHX_HDRS) $(PHX_FCFG) inc-sw/arch.h Makefile
	$(call build_phx,$(X86_CC),$(X86_CFLAGS),inc-sw,$(BUILD)/x86/phs)
	./rename.sh phs $(BUILD)/x86/phs $@ $(X86_TOOLS) $(MATHSYMS)

$(BUILD)/x86/mcs.o: $(MCS_SRCS) $(PHX_FCFG) Makefile
	$(call build_mcs,$(X86_CC),$(X86_CFLAGS),$(BUILD)/x86/mcs)
	./rename.sh mcs $(BUILD)/x86/mcs $@ $(X86_TOOLS) $(MATHSYMS)

$(BUILD)/x86/harness: harness.c $(BUILD)/x86/phx.o $(BUILD)/x86/phs.o $(BUILD)/x86/mcs.o
	$(X86_CC) -O1 -std=gnu17 -Wall -o $@ $^ -lmpfr -lgmp -lm

run: $(BUILD)/x86/harness
	$< $(N)

# --- reference values for phoenix-rtos-tests libc/math --------------------
$(BUILD)/genrefs: genrefs.c
	@mkdir -p $(BUILD)
	$(X86_CC) -O1 -std=gnu17 -Wall -o $@ $< -lmpfr -lgmp -lm

gen-refs: $(BUILD)/genrefs
	$< > $(BUILD)/accuracy-refs.h
	@echo "wrote $(BUILD)/accuracy-refs.h"

clean:
	rm -rf $(BUILD)

.PHONY: all run gen-refs clean
