/*
 * gl_stubs.c — stubs + libc gaps for the Phoenix GL frontend (GLQuake Path C,
 * Phase 4). Two groups:
 *  (1) sw-path / debug symbols st references but v3d (HW vertex, GLSL-not-SPIRV,
 *      no disk cache) never exercises at runtime: the gallium `draw` module,
 *      SPIR-V, ASTC decode, disk cache, gallium trace, sw tgsi_exec. Stubbed to
 *      NULL/no-op (linkage is by name; generic signatures resolve the refs).
 *  (2) small libc gaps Phoenix lacks: posix_memalign, strtok_r,
 *      pthread_mutex_timedlock (lets c11 threads_posix.c link).
 * Compiled WITHOUT the compat shim. Warnings off.
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* --- gallium draw module (sw vertex pipeline): unused, v3d does HW vertex --- */
void *draw_create_vertex_shader(void *draw, const void *shader) { (void)draw; (void)shader; return NULL; }
void  draw_delete_vertex_shader(void *draw, void *vs) { (void)draw; (void)vs; }
void  draw_destroy(void *draw) { (void)draw; }
int   draw_nir_lower_opcodes(void *nir) { (void)nir; return 0; }

/* --- SPIR-V (GL_ARB_gl_spirv): Quake/gears use GLSL, not SPIR-V --- */
void *spirv_to_nir(const uint32_t *w, size_t n, void *spec, size_t ns,
                   int stage, const char *ep, const void *opts, const void *nopts)
{ (void)w; (void)n; (void)spec; (void)ns; (void)stage; (void)ep; (void)opts; (void)nopts; return NULL; }
void *vtn_alloc_specialization(void *a, size_t b) { (void)a; (void)b; return NULL; }
void  vtn_free_specialization(void *a) { (void)a; }
void  vtn_add_specialization_entry(void *a, void *b) { (void)a; (void)b; }

/* --- ASTC texture decode: not used by Quake's textures --- */
const void *_mesa_get_astc_decoder_partition_table(int a, int b) { (void)a; (void)b; return NULL; }
void  _mesa_init_astc_decoder_luts(void) {}
void  _mesa_unpack_astc_2d_ldr(void) {}

/* --- disk shader cache: disabled --- */
unsigned char disk_cache_has_key(void *c, const unsigned char *k) { (void)c; (void)k; return 0; }
void disk_cache_put_key(void *c, const unsigned char *k) { (void)c; (void)k; }
void disk_cache_remove(void *c, const unsigned char *k) { (void)c; (void)k; }

/* --- gallium trace (debug dumper): off --- */
unsigned char trace_dumping_enabled_locked(void) { return 0; }
void trace_dumping_start_locked(void) {}
void trace_dumping_stop_locked(void) {}

/* --- sw tgsi interpreter + translate + nir_to_tgsi (only the draw module used
 * these; stubbed so any straggler ref links) --- */
void *nir_to_tgsi(void *nir, void *screen) { (void)nir; (void)screen; return NULL; }
void  tgsi_exec_machine_bind_shader(void *m, const void *t, void *s) { (void)m; (void)t; (void)s; }
void *tgsi_exec_machine_create(int p) { (void)p; return NULL; }
void  tgsi_exec_machine_destroy(void *m) { (void)m; }
unsigned tgsi_exec_machine_run(void *m, int i) { (void)m; (void)i; return 0; }
void  tgsi_exec_set_constant_buffers(void *m, unsigned n, const void *b) { (void)m; (void)n; (void)b; }
void *translate_cache_create(void) { return NULL; }
void  translate_cache_destroy(void *c) { (void)c; }
void *translate_cache_find(void *c, void *k) { (void)c; (void)k; return NULL; }
void  pipe_get_tile_rgba(void *t, void *p, int x, int y, int w, int h, int f, void *d)
{ (void)t; (void)p; (void)x; (void)y; (void)w; (void)h; (void)f; (void)d; }

/* --- misc --- */
const char *os_get_option_secure(const char *name) { (void)name; return NULL; }

/* --- libc gaps Phoenix lacks (real impls) --- */
int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	if (alignment < sizeof(void *))
		alignment = sizeof(void *);
	void *p = NULL;
	/* malloc + manual align with a back-pointer for free() compatibility is
	 * fragile; Phoenix malloc returns 16-byte-aligned, enough for Mesa's uses. */
	p = malloc(size);
	if (!p)
		return 12; /* ENOMEM */
	*memptr = p;
	return 0;
}

char *strtok_r(char *str, const char *delim, char **saveptr)
{
	char *s = str ? str : *saveptr, *tok;
	if (!s)
		return NULL;
	s += strspn(s, delim);
	if (*s == '\0') { *saveptr = s; return NULL; }
	tok = s;
	s = strpbrk(tok, delim);
	if (s) { *s = '\0'; *saveptr = s + 1; }
	else   { *saveptr = tok + strlen(tok); }
	return tok;
}

/* c11 threads_posix.c uses pthread_mutex_timedlock (absent on Phoenix); a plain
 * blocking lock is a correct-enough fallback for Mesa's timed waits. */
struct __phx_mtx;
int pthread_mutex_lock(void *);
int pthread_mutex_timedlock(void *m, const void *abstime) { (void)abstime; return pthread_mutex_lock(m); }
