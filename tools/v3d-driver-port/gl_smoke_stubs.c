/*
 * gl_smoke_stubs.c - link glue for the gl_frontend_smoke GL render-clear test.
 *
 * gl_frontend_smoke.c (unchanged) does not carry the two Mesa link stubs that
 * gl_det_harness.c / gl_uif_probe.c define inline, so provide them here as a
 * separate object linked into BOTH the in-process baseline and the daemon-client
 * builds. This is pure link glue - identical to the stubs those harnesses embed -
 * not a modification of the test:
 *
 *   trace_context_create_threaded  Mesa's GALLIUM_TRACE wrapper. We never enable
 *                                  tracing, so pass the pipe_context through
 *                                  (same shim as pl_phoenix_glctx.c / gl_det_harness).
 *   pthread_getcpuclockid          Phoenix libc lacks it (Mesa u_thread timing
 *                                  references it); monotonic-clock stand-in.
 *
 * Copyright 2026 Phoenix Systems  SPDX-License-Identifier: BSD-3-Clause
 */
#include <pthread.h>
#include <time.h>

struct pipe_screen;
struct pipe_context;

struct pipe_context *trace_context_create_threaded(struct pipe_screen *screen,
                                                   struct pipe_context *pipe)
{
	(void)screen;
	return pipe;
}

int pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id)
{
	(void)thread;
	if (clock_id)
		*clock_id = CLOCK_MONOTONIC;
	return 0;
}
