/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * labwc-drm host test: compat C11 threads (lwphx_threads.c), unnamed semaphores
 * (lwphx_sem.c) and the wide-string additions (lwphx_wchar.c), built natively
 * against compat/include (which shadows the host's <threads.h>, <semaphore.h>).
 * The semaphore check is foot's render-worker pattern: N workers wait on
 * `start`, the main thread posts N times, each worker posts `done`.
 */

#include <semaphore.h>
#include <stdatomic.h>
#include <string.h>
#include <threads.h>
#include <wchar.h>

#include "test.h"

#define WORKERS 4
#define ROUNDS  2000

static sem_t start, done;
static atomic_int work;
static mtx_t m;
static cnd_t cv;
static int flag;

static int worker(void *arg)
{
	(void)arg;
	for (;;) {
		sem_wait(&start);
		if (atomic_load(&work) < 0) {
			return 7;
		}
		atomic_fetch_add(&work, 1);
		sem_post(&done);
	}
}

static int waiter(void *arg)
{
	(void)arg;
	mtx_lock(&m);
	while (flag == 0) {
		cnd_wait(&cv, &m);
	}
	mtx_unlock(&m);
	return 42;
}

int main(void)
{
	thrd_t t[WORKERS], w;
	int i, r, rc, v, bad = 0;

	CHECK(sem_init(&start, 0, 0) == 0 && sem_init(&done, 0, 0) == 0, "sem_init x2");
	for (i = 0; i < WORKERS; i++) {
		CHECK(thrd_create(&t[i], worker, NULL) == thrd_success, "thrd_create worker %d", i);
	}
	for (r = 0; r < ROUNDS; r++) {
		int before = atomic_load(&work);
		for (i = 0; i < WORKERS; i++) {
			sem_post(&start);
		}
		for (i = 0; i < WORKERS; i++) {
			sem_wait(&done);
		}
		if (atomic_load(&work) != before + WORKERS) {
			bad++;
		}
	}
	CHECK(bad == 0, "%d rounds of %d-worker start/done, no lost wake-up (%d bad)", ROUNDS, WORKERS, bad);
	CHECK((sem_getvalue(&done, &v) == 0) && (v == 0), "done drained (value %d)", v);
	CHECK((sem_trywait(&done) < 0), "sem_trywait on 0 fails (EAGAIN)");
	atomic_store(&work, -1);
	for (i = 0; i < WORKERS; i++) {
		sem_post(&start);
	}
	for (i = 0, bad = 0; i < WORKERS; i++) {
		if ((thrd_join(t[i], &rc) != thrd_success) || (rc != 7)) {
			bad++;
		}
	}
	CHECK(bad == 0, "thrd_join returns each worker's int result");
	sem_destroy(&start);
	sem_destroy(&done);

	CHECK(mtx_init(&m, mtx_plain) == thrd_success && cnd_init(&cv) == thrd_success, "mtx_init/cnd_init");
	CHECK(thrd_create(&w, waiter, NULL) == thrd_success, "waiter thread");
	mtx_lock(&m);
	flag = 1;
	cnd_broadcast(&cv);
	mtx_unlock(&m);
	CHECK((thrd_join(w, &rc) == thrd_success) && (rc == 42), "cnd_broadcast wakes the waiter (rc=%d)", rc);
	CHECK(mtx_init(&m, mtx_recursive) == thrd_success && mtx_lock(&m) == thrd_success && mtx_lock(&m) == thrd_success &&
		mtx_unlock(&m) == thrd_success && mtx_unlock(&m) == thrd_success, "recursive mutex locks twice");
	CHECK(mtx_init(&m, 99) == thrd_error, "unknown mutex type -> thrd_error");

	wchar_t buf[16] = L"foo";
	CHECK((wcsncat(buf, L"barbaz", 3) == buf) && (wcscmp(buf, L"foobar") == 0), "wcsncat(\"foo\", \"barbaz\", 3) = \"foobar\"");
	CHECK(wcscasecmp(L"Foot", L"fOOT") == 0, "wcscasecmp equal ignoring case");
	CHECK(wcscasecmp(L"abc", L"abd") < 0 && wcscasecmp(L"abd", L"ABC") > 0, "wcscasecmp orders");
	CHECK(wcsncasecmp(L"LABWC", L"labxx", 3) == 0 && wcsncasecmp(L"LABWC", L"labxx", 4) != 0, "wcsncasecmp bound");
	CHECK(wcscasecmp(L"", L"") == 0 && wcscasecmp(L"a", L"") > 0, "empty strings");
	return RESULT("sync");
}
