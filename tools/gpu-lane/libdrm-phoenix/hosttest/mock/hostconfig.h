/* libdrm-phoenix host harness: the config.h meson generates for aarch64-phoenix,
 * minus the target-only facts. HAVE_OPEN_MEMSTREAM stays 0 as on Phoenix, so the
 * fallback in patch 0002 is what compiles here too. */
#pragma once
#define HAVE_ALLOCA_H 1
#define HAVE_LIBDRM_ATOMIC_PRIMITIVES 1
#define HAVE_LIB_ATOMIC_OPS 0
#define HAVE_OPEN_MEMSTREAM 0
#define HAVE_SECURE_GETENV 0
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_SYSCTL_H 0
#define HAVE_VALGRIND 0
#define HAVE_VC4 1
#define HAVE_VISIBILITY 1
#define MAJOR_IN_SYSMACROS 1
#define UDEV 0
