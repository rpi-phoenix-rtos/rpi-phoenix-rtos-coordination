/*
 * stdclose: a CLOSED standard stream must stay safe to name.
 *
 * libstdc++'s std::cout/cerr/clog keep the FILE * that stdout/stderr held at
 * startup and fflush() it from ios_base::Init::~Init(), an exit-time
 * destructor. A program that fclose()s stdout first (SuperTuxKart ends main()
 * with fclose(stderr); fclose(stdout);) therefore flushes the closed stream
 * during exit. When libphoenix free()d the standard stream objects, that was a
 * use-after-free: on the Pi it became a Data Abort in file_rawSeek (stk-drm,
 * 2026-09-27, far=0xba) once the recycled words read as an F_OPS stream.
 *
 * This drives libphoenix's real stdio/file.c (symbols renamed ph_*) through
 * the same sequence, built with AddressSanitizer: before the fix ASan reports
 * heap-use-after-free on the first fflush(); after it every check passes.
 *
 *   1. fclose(stdout), then -- through the pointer saved before, as libstdc++
 *      does -- fflush() must return 0, fileno() must be -1, a write must fail,
 *      and a second fclose() must fail with EBADF.
 *   2. freopen() of stdin onto a path that cannot be opened (the other way
 *      file.c releases a stream) must leave the saved stdin pointer as valid.
 *   3. fflush(NULL) afterwards must not touch either stream.
 *
 * The host's own descriptors 0 and 1 are saved and restored around the
 * closes, so the harness can still print.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


typedef struct ph_FILE ph_FILE;

extern void ph__file_init(void);
extern ph_FILE *ph_stdin, *ph_stdout, *ph_stderr;
extern int ph_fclose(ph_FILE *f);
extern int ph_fflush(ph_FILE *f);
extern int ph_fileno(ph_FILE *f);
extern int ph_ferror(ph_FILE *f);
extern size_t ph_fwrite(const void *p, size_t sz, size_t n, ph_FILE *f);
extern ph_FILE *ph_freopen(const char *path, const char *mode, ph_FILE *f);

static int nFail, nPass;


static void check(int ok, const char *what)
{
	if (ok) {
		nPass++;
	}
	else {
		nFail++;
		printf("  FAIL %s\n", what);
	}
}


int main(void)
{
	ph_FILE *out, *in;
	int save0, save1, ret, err;
	void *spray[64];
	unsigned int i;

	ph__file_init();

	save0 = dup(0);
	save1 = dup(1);
	if ((save0 < 0) || (save1 < 0)) {
		perror("dup");
		return 2;
	}

	/* 1. fclose(stdout) with data still buffered, as a program's last printf() */
	out = ph_stdout;
	check(ph_fwrite("bye\n", 1, 4, out) == 4, "buffered write before fclose");
	check(ph_fclose(out) == 0, "fclose(stdout) == 0");
	dup2(save1, 1); /* file.c closed the host's fd 1 */

	/* Recycle the heap the way the rest of an exiting program does. */
	for (i = 0; i < sizeof(spray) / sizeof(spray[0]); i++) {
		spray[i] = malloc(72);
		if (spray[i] != NULL) {
			memset(spray[i], 0xff, 72);
		}
	}

	ret = ph_fflush(out); /* std::ios_base::Init::~Init() */
	check(ret == 0, "fflush(closed stdout) == 0");
	check(ph_fileno(out) == -1, "fileno(closed stdout) == -1");
	check(ph_fwrite("x", 1, 1, out) == 0, "write to closed stdout fails");
	check(ph_ferror(out) != 0, "ferror(closed stdout) set");
	errno = 0;
	ret = ph_fclose(out);
	err = errno;
	check((ret == EOF) && (err == EBADF), "second fclose(stdout) == EOF/EBADF");

	/* 2. freopen() failure releases the stream too */
	in = ph_stdin;
	check(ph_freopen("/nonexistent-dir/stdclose", "r", in) == NULL, "freopen(bad path, stdin) == NULL");
	dup2(save0, 0);
	check(ph_fflush(in) == 0, "fflush(failed-freopen stdin) == 0");
	check(ph_fileno(in) == -1, "fileno(failed-freopen stdin) == -1");

	/* 3. the exit-time flush of every open stream */
	check(ph_fflush(NULL) == 0, "fflush(NULL) after both");

	for (i = 0; i < sizeof(spray) / sizeof(spray[0]); i++) {
		free(spray[i]);
	}

	printf("stdclose   : %d passed, %d failed\n", nPass, nFail);
	return (nFail == 0) ? 0 : 1;
}
