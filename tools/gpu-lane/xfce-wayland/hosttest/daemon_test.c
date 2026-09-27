/* Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Host test of compat/src/xfphx_misc.c daemon() (compiled with -Ddaemon=xfphx_daemon so
 * glibc's own is not the one under test). For each of daemon(1,0), daemon(0,0), daemon(1,1):
 * the caller of daemon() (a forked test child) must exit 0 at once, and the process that
 * continues must be a session leader, in "/" or not as asked, with fds 0-2 on /dev/null or
 * untouched as asked. The continuing process reports through a pipe.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int xfphx_daemon(int nochdir, int noclose);

static int fails;

static void check(int ok, const char *what)
{
	printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) {
		fails++;
	}
}

struct report {
	int rc, sid_is_pid, fd_devnull[3], cwd_root;
};

static void one(int nochdir, int noclose)
{
	int p[2];
	struct report r;
	char label[96];
	pid_t pid;
	int st;
	ssize_t n;

	if ((pipe(p) != 0) || (chdir("/tmp") != 0)) {
		perror("setup");
		exit(2);
	}
	pid = fork();
	if (pid == 0) {
		int i;
		char path[PATH_MAX], cwd[PATH_MAX];

		close(p[0]);
		memset(&r, 0, sizeof(r));
		r.rc = xfphx_daemon(nochdir, noclose);
		/* only the continuing process gets here */
		r.sid_is_pid = (getsid(0) == getpid());
		for (i = 0; i < 3; i++) {
			char link[64];
			ssize_t l;

			snprintf(link, sizeof(link), "/proc/self/fd/%d", i);
			l = readlink(link, path, sizeof(path) - 1);
			path[(l > 0) ? l : 0] = '\0';
			r.fd_devnull[i] = (strcmp(path, "/dev/null") == 0);
		}
		r.cwd_root = (getcwd(cwd, sizeof(cwd)) != NULL) && (strcmp(cwd, "/") == 0);
		(void)!write(p[1], &r, sizeof(r));
		_exit(0);
	}
	close(p[1]);
	check((waitpid(pid, &st, 0) == pid) && WIFEXITED(st) && (WEXITSTATUS(st) == 0),
		(snprintf(label, sizeof(label), "daemon(%d,%d): the caller exits 0 at once", nochdir, noclose), label));
	n = read(p[0], &r, sizeof(r));   /* blocks until the detached process reports */
	close(p[0]);
	check(n == (ssize_t)sizeof(r), (snprintf(label, sizeof(label), "daemon(%d,%d): the detached process continues", nochdir, noclose), label));
	if (n != (ssize_t)sizeof(r)) {
		return;
	}
	check(r.rc == 0, (snprintf(label, sizeof(label), "daemon(%d,%d): returns 0 in the detached process", nochdir, noclose), label));
	check(r.sid_is_pid, (snprintf(label, sizeof(label), "daemon(%d,%d): session leader (setsid)", nochdir, noclose), label));
	check(r.cwd_root == (nochdir == 0), (snprintf(label, sizeof(label), "daemon(%d,%d): cwd %s", nochdir, noclose, nochdir ? "kept (/tmp)" : "/"), label));
	check((r.fd_devnull[0] && r.fd_devnull[1] && r.fd_devnull[2]) == (noclose == 0),
		(snprintf(label, sizeof(label), "daemon(%d,%d): stdio %s", nochdir, noclose, noclose ? "untouched" : "on /dev/null"), label));
}

int main(void)
{
	/* run.sh points stdout/stderr at a log file: the noclose case must see something other
	 * than /dev/null there */
	one(1, 0);   /* libxfce4ui's xfce_spawn() call */
	one(0, 0);
	one(1, 1);
	printf("daemon: %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
