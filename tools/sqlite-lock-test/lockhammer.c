/*
 * Phoenix-RTOS RPi4 — multi-process SQLite lock hammer (Phase 1)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Proves that two independent processes writing the same SQLite database
 * serialize correctly through the kernel's new fcntl record locks (SQLite's
 * default unix VFS locks via fcntl F_RDLCK/F_WRLCK on the PENDING_BYTE range).
 * Under the old no-op EOK stub the locks were fake: a writer could never see
 * SQLITE_BUSY from contention and concurrent writes would corrupt the file.
 *
 * Design (SQLite multi-process footguns avoided):
 *   - fork() FIRST, sqlite3_open() AFTER — never carry a handle across fork.
 *   - journal_mode=DELETE (rollback journal); WAL needs shm (xShmMap) we lack.
 *   - busy_timeout(0) + manual retry with a COUNTER, so BUSY>0 is observable —
 *     that count is the discriminator that contention actually hit the locks.
 *
 * Verdict (parent): row count == expected, integrity_check == ok, BUSY > 0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "sqlite3.h"

#define NCHILD    2
#define NTXN      50
#define BUSY_CAP  250 /* exit status is a byte; cap the reported count */

static int busy_total = 0;


static int exec_retry(sqlite3 *db, const char *sql)
{
	int rc;
	for (;;) {
		rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
		if ((rc == SQLITE_BUSY) || (rc == SQLITE_LOCKED)) {
			if (busy_total < 1000000) {
				busy_total++;
			}
			usleep(2000);
			continue;
		}
		return rc;
	}
}


static int child_run(const char *path, int id)
{
	sqlite3 *db = NULL;
	char sql[96];
	int i, rc;

	rc = sqlite3_open(path, &db);
	if (rc != SQLITE_OK) {
		printf("child %d: sqlite3_open failed rc=%d\n", id, rc);
		return 0;
	}
	sqlite3_busy_timeout(db, 0);
	(void)exec_retry(db, "PRAGMA journal_mode=DELETE;");

	for (i = 0; i < NTXN; i++) {
		if (exec_retry(db, "BEGIN IMMEDIATE;") != SQLITE_OK) {
			break;
		}
		snprintf(sql, sizeof(sql), "INSERT INTO t(who, seq) VALUES(%d, %d);", id, i);
		(void)exec_retry(db, sql);
		(void)exec_retry(db, "COMMIT;");

		if ((i % 10) == 0) {
			printf("child %d: %d/%d committed (busy=%d)\n", id, i, NTXN, busy_total);
		}
	}

	sqlite3_close(db);
	printf("child %d: done, %d txns, busy retries=%d\n", id, NTXN, busy_total);
	return (busy_total > BUSY_CAP) ? BUSY_CAP : busy_total;
}


static int count_rows(sqlite3 *db)
{
	sqlite3_stmt *st = NULL;
	int n = -1;
	if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM t;", -1, &st, NULL) == SQLITE_OK) {
		if (sqlite3_step(st) == SQLITE_ROW) {
			n = sqlite3_column_int(st, 0);
		}
	}
	sqlite3_finalize(st);
	return n;
}


static int integrity_ok(sqlite3 *db)
{
	sqlite3_stmt *st = NULL;
	int ok = 0;
	if (sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &st, NULL) == SQLITE_OK) {
		if (sqlite3_step(st) == SQLITE_ROW) {
			const unsigned char *s = sqlite3_column_text(st, 0);
			ok = (s != NULL) && (strcmp((const char *)s, "ok") == 0);
		}
	}
	sqlite3_finalize(st);
	return ok;
}


int main(int argc, char **argv)
{
	const char *path = (argc > 1) ? argv[1] : "/tmp/lockhammer.db";
	char jpath[256];
	sqlite3 *db = NULL;
	pid_t kids[NCHILD];
	int i, rc, wstatus, busy_sum = 0, rows, expected, failed = 0;

	printf("PHASE1: multi-process SQLite hammer on '%s' (%d procs x %d txns)\n", path, NCHILD, NTXN);

	/* fresh database: remove any stale db + rollback journal */
	(void)unlink(path);
	snprintf(jpath, sizeof(jpath), "%s-journal", path);
	(void)unlink(jpath);

	/* parent creates the schema, then CLOSES before forking */
	rc = sqlite3_open(path, &db);
	if (rc != SQLITE_OK) {
		printf("PHASE1: FATAL create open rc=%d\n", rc);
		return 2;
	}
	if (sqlite3_exec(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, who INT, seq INT);", NULL, NULL, NULL) != SQLITE_OK) {
		printf("PHASE1: FATAL create table: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return 2;
	}
	sqlite3_close(db);
	db = NULL;

	for (i = 0; i < NCHILD; i++) {
		pid_t p = fork();
		if (p == -1) {
			printf("PHASE1: FATAL fork\n");
			return 2;
		}
		if (p == 0) {
			_exit(child_run(path, i));
		}
		kids[i] = p;
	}

	for (i = 0; i < NCHILD; i++) {
		(void)waitpid(kids[i], &wstatus, 0);
		if (WIFEXITED(wstatus)) {
			busy_sum += WEXITSTATUS(wstatus);
		}
		else {
			printf("PHASE1: child %d did not exit cleanly\n", i);
			failed++;
		}
	}

	/* verdict */
	rc = sqlite3_open(path, &db);
	if (rc != SQLITE_OK) {
		printf("PHASE1: FATAL verify open rc=%d\n", rc);
		return 2;
	}
	rows = count_rows(db);
	expected = NCHILD * NTXN;
	if (rows != expected) {
		printf("PHASE1: FAIL row count %d, expected %d\n", rows, expected);
		failed++;
	}
	else {
		printf("PHASE1: OK row count %d == %d\n", rows, expected);
	}

	if (!integrity_ok(db)) {
		printf("PHASE1: FAIL integrity_check != ok\n");
		failed++;
	}
	else {
		printf("PHASE1: OK integrity_check == ok\n");
	}
	sqlite3_close(db);

	if (busy_sum <= 0) {
		printf("PHASE1: FAIL total BUSY retries == 0 (no contention observed — locks may be no-ops)\n");
		failed++;
	}
	else {
		printf("PHASE1: OK total BUSY retries = %d (contention engaged the lock table)\n", busy_sum);
	}

	printf("PHASE1: %s\n", (failed == 0) ? "PASS" : "FAIL");
	return (failed == 0) ? 0 : 1;
}
