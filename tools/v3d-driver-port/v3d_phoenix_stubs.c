/*
 * v3d_phoenix_stubs.c — no-op stubs for Mesa peripherals excluded from the Phoenix
 * v3d driver subset (GLQuake Path C). None of these are on the CL/shader-generation
 * path; they are disk shader cache, the CL debug dumper, build-id, logging, memstream,
 * and driconf option lookup. Stubbing them keeps the cross-build self-contained.
 * Plus a real qsort_r (Phoenix libc has only plain qsort) and a NEON-blake3 stub.
 *
 * Linkage is by symbol name only, so generic signatures here resolve the driver's
 * references compiled against the real prototypes. Compiled with warnings off.
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* --- disk shader cache: disabled (no persistent cache on Phoenix) --- */
void *disk_cache_create(const char *name, const char *key, uint64_t flags) { return NULL; }
void  disk_cache_destroy(void *cache) { }
void  disk_cache_compute_key(void *c, const void *data, size_t size, unsigned char *key) { }
void *disk_cache_get(void *c, const unsigned char *key, size_t *size) { if (size) *size = 0; return NULL; }
void  disk_cache_put(void *c, const unsigned char *key, const void *data, size_t size, void *m) { }

/* --- CL debug dumper (V3D_DEBUG=cl): disabled --- */
void *clif_dump_init(const void *devinfo, void *out, int pretty, int nocolor) { return NULL; }
void  clif_dump_destroy(void *clif) { }
void  clif_dump_add_bo(void *clif, const char *name, uint32_t offset, uint32_t size, void *vaddr) { }
int   clif_dump(void *clif) { return 0; }

/* --- build-id (used by the disk cache key): none --- */
const void *build_id_data(const void *note) { return NULL; }
void       *build_id_find_nhdr_for_addr(const void *addr) { return NULL; }

/* --- logging: route to nowhere (the driver only logs debug/perf) --- */
void  mesa_log(int level, const char *tag, const char *fmt, ...) { }
void  _mesa_log_multiline(int level, const char *tag, const char *lines) { }
void *_mesa_log_stream_create(int level, char *tag) { return NULL; }
void  mesa_log_stream_destroy(void *stream) { }
void  mesa_log_stream_printf(void *stream, const char *fmt, ...) { }

/* sha1 hex formatter (only feeds the disabled disk cache key) */
void _mesa_sha1_format(char *buf, const unsigned char *sha1) { if (buf) buf[0] = '\0'; }

/* --- memstream (used by the disabled disk cache): unavailable --- */
int u_memstream_open(void *mem, char **bufp, size_t *sizep) { return 0; }
int u_memstream_close(void *mem) { return 0; }

/* --- driconf option lookup: always defaults --- */
void  driParseConfigFiles(void *opt, const void *info, int sc, const char *d,
                          const char *e, const char *a, const char *b, unsigned n, void *v) { }
unsigned char driCheckOption(const void *opt, const char *name, int type) { return 0; }
unsigned char driQueryOptionb(const void *opt, const char *name) { return 0; }
float driQueryOptionf(const void *opt, const char *name) { return 0.0f; }

/* --- NEON blake3 (aarch64 dispatch picks this): not used on the screen/CL path.
 * TODO: route to blake3_hash_many_portable or compile blake3_neon.c if hashing
 * is ever exercised (currently only the disabled disk cache hashes). --- */
void blake3_hash_many_neon(const uint8_t *const *inputs, size_t num_inputs,
                           size_t blocks, const uint32_t key[8], uint64_t counter,
                           int increment_counter, uint8_t flags, uint8_t flags_start,
                           uint8_t flags_end, uint8_t *out)
{
	/* zero the output so identical inputs hash identically (not garbage stack);
	 * 64 bytes/output is blake3's block-hash stride. */
	if (out)
		__builtin_memset(out, 0, num_inputs * 64);
}

/* --- os abstraction: Phoenix isn't a Mesa-recognized OS, so os_misc.c/os_time.c
 * don't build. Provide minimal Phoenix-appropriate impls. --- */
#include <time.h>
int os_get_page_size(uint64_t *size) { if (size) *size = 4096; return 1; }
int os_get_total_physical_memory(uint64_t *size) { if (size) *size = 1ull << 30; return 1; }
int os_get_available_system_memory(uint64_t *size) { if (size) *size = 1ull << 30; return 1; }
const char *os_get_option(const char *name) { (void)name; return NULL; }
const char *os_get_option_cached(const char *name) { (void)name; return NULL; }
void os_log_message(const char *message) { (void)message; }

int64_t os_time_get_nano(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

int64_t os_time_get_absolute_timeout(uint64_t timeout)
{
	return os_time_get_nano() + (int64_t)timeout;
}

/* --- renderonly: KMS dumb-buffer import. We always pass ro=NULL, so these are
 * never reached; stub to satisfy the link. --- */
void *renderonly_create_gpu_import_for_resource(void *rsc, void *ro, void *out_handle)
{ return NULL; }
void renderonly_scanout_destroy(void *scanout, void *ro) { }

/* --- C11 threads: Mesa's threads_posix.c needs pthread_mutex_timedlock (absent on
 * Phoenix). The driver only uses call_once + mtx_init/lock/unlock/destroy, so back
 * those directly with pthread (mtx_t == pthread_mutex_t, once_flag == pthread_once_t
 * by Mesa's c11 typedefs, so the void* casts are size-correct). --- */
#include <pthread.h>
void call_once(void *flag, void (*func)(void)) { pthread_once((pthread_once_t *)flag, func); }
int  mtx_init(void *mtx, int type) { (void)type; return pthread_mutex_init((pthread_mutex_t *)mtx, NULL) ? 1 : 0; }
void mtx_destroy(void *mtx) { pthread_mutex_destroy((pthread_mutex_t *)mtx); }
int  mtx_lock(void *mtx) { return pthread_mutex_lock((pthread_mutex_t *)mtx) ? 1 : 0; }
int  mtx_unlock(void *mtx) { return pthread_mutex_unlock((pthread_mutex_t *)mtx) ? 1 : 0; }

/* --- locale-aware strtod/strtof (Mesa's _mesa_strtod uses newlocale): plain libc --- */
double _mesa_strtod(const char *s, char **end) { return strtod(s, end); }
float  _mesa_strtof(const char *s, char **end) { return strtof(s, end); }

/* --- v3d perfcounters: report none (not needed for screen/CL/draw). The driver
 * inits an empty counter set. TODO: compile v3dx_counter.c@V3D_VERSION=42 if perf
 * queries are ever wired. --- */
const void *v3d42_perfcounters_get(unsigned index) { (void)index; return NULL; }
unsigned    v3d42_perfcounters_num(void) { return 0; }

/* --- GNU qsort_r (Phoenix libc has only plain qsort). Simple insertion-sort
 * impl: correct, no global state, fine for Mesa's small sorts. --- */
void qsort_r(void *base, size_t nmemb, size_t size,
             int (*compar)(const void *, const void *, void *), void *arg)
{
	char *a = (char *)base;
	char tmp[256];
	if (size > sizeof(tmp))
		return;                 /* Mesa's qsort_r elements are tiny */
	for (size_t i = 1; i < nmemb; i++) {
		size_t j = i;
		while (j > 0 && compar(a + (j - 1) * size, a + j * size, arg) > 0) {
			__builtin_memcpy(tmp, a + (j - 1) * size, size);
			__builtin_memcpy(a + (j - 1) * size, a + j * size, size);
			__builtin_memcpy(a + j * size, tmp, size);
			j--;
		}
	}
}
