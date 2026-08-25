/*
 * pty-run — run a program on a fresh pseudo-terminal so interactive shells work.
 *
 * NOTE (2026-08-25): the original motivation — "bash's readline hits immediate
 * EOF and exits" — was NOT an fd-0/pty problem. It was a libphoenix select()
 * bug (a NULL/infinite timeout returned 0 immediately instead of blocking, so
 * readline's blocking select() in rl_getc() aborted). That is fixed in
 * libphoenix sys/select.c, and interactive bash now works directly under psh
 * (HW-verified) with no pty-run needed. This helper is kept as a general
 * getty-style pty forwarder for programs that genuinely want their own pty:
 *
 * This helper is a minimal getty-style pty forwarder:
 *
 *   1. open the SVR4 /dev/ptmx multiplexor (posixsrv) -> pty master; unlockpt;
 *      ptsname() -> /dev/pts/N slave.
 *   2. fork; the CHILD becomes a session leader, opens the slave as its
 *      controlling terminal, dup2's it onto stdin/stdout/stderr, and exec's the
 *      target program — which now sees a real tty (isatty via tcgetattr) and
 *      runs interactively.
 *   3. the PARENT bridges bytes both ways between this process's own
 *      stdin/stdout (the console psh handed us) and the pty master, until the
 *      child exits or either side closes.
 *
 * Usage:  pty-run <program> [args...]      e.g.  pty-run bash
 *
 * Phoenix pty idiom + the "use fcntl(F_SETFL), not the FIONBIO ioctl, for
 * O_NONBLOCK on posixsrv ptys" lesson are taken from the xterm port
 * (sources/phoenix-rtos-ports/xterm get_pty()).
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>

#ifndef TIOCSCTTY
#define TIOCSCTTY _IOV('T', 0xE, int)
#endif

int main(int argc, char **argv)
{
	int mfd, sfd, mode;
	char *slave;
	pid_t pid;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <program> [args...]\n", argv[0]);
		return 2;
	}

	mfd = open("/dev/ptmx", O_RDWR);
	if (mfd < 0) {
		perror("pty-run: open /dev/ptmx");
		return 1;
	}
	(void)grantpt(mfd);
	if (unlockpt(mfd) < 0) {
		perror("pty-run: unlockpt");
		return 1;
	}
	slave = ptsname(mfd);
	if (slave == NULL) {
		perror("pty-run: ptsname");
		return 1;
	}

	pid = fork();
	if (pid < 0) {
		perror("pty-run: fork");
		return 1;
	}

	if (pid == 0) {
		/* child: new session, slave becomes the controlling terminal */
		setsid();
		sfd = open(slave, O_RDWR);
		if (sfd < 0) {
			perror("pty-run: child open pts");
			_exit(127);
		}
		(void)ioctl(sfd, TIOCSCTTY, 0);
		dup2(sfd, STDIN_FILENO);
		dup2(sfd, STDOUT_FILENO);
		dup2(sfd, STDERR_FILENO);
		if (sfd > STDERR_FILENO) {
			close(sfd);
		}
		close(mfd);
		execvp(argv[1], &argv[1]);
		perror("pty-run: exec");
		_exit(127);
	}

	/* parent: nonblocking master via fcntl (NOT FIONBIO — posixsrv EINVALs it) */
	if ((mode = fcntl(mfd, F_GETFL, 0)) >= 0) {
		(void)fcntl(mfd, F_SETFL, mode | O_NONBLOCK);
	}

	for (;;) {
		fd_set rfds;
		int n, r, status;
		char buf[1024];

		/* reap the child if it exited */
		if (waitpid(pid, &status, WNOHANG) == pid) {
			break;
		}

		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
		FD_SET(mfd, &rfds);
		n = (mfd > STDIN_FILENO) ? mfd : STDIN_FILENO;

		struct timeval tv = { 1, 0 };
		r = select(n + 1, &rfds, NULL, NULL, &tv);
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (r == 0) {
			continue; /* timeout: loop back to the waitpid poll */
		}

		if (FD_ISSET(STDIN_FILENO, &rfds)) {
			n = read(STDIN_FILENO, buf, sizeof(buf));
			if (n > 0) {
				(void)write(mfd, buf, n);
			}
			else if (n == 0) {
				break; /* console closed */
			}
		}
		if (FD_ISSET(mfd, &rfds)) {
			n = read(mfd, buf, sizeof(buf));
			if (n > 0) {
				(void)write(STDOUT_FILENO, buf, n);
			}
			else if (n == 0) {
				break; /* child closed the pty */
			}
		}
	}

	close(mfd);
	return 0;
}
