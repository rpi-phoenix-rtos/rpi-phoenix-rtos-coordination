/*
 * wpe-ipc-probe: the Phoenix-RTOS kernel/libc behaviours WebKit's IPC and process
 * lifecycle rely on, one numbered check per line (browser plan B4).
 *
 *   wpe-ipc-probe [N...]     run checks N (default: all)
 *
 * Every line starts with "IPCPROBE T<n> " and ends in PASS, FAIL or INFO, so a UART
 * log can be graded with one grep. A FAIL names what Linux does instead.
 *
 * Build (static, like every Phoenix program):
 *   aarch64-phoenix-gcc --sysroot=<sysroot> -O2 -o wpe-ipc-probe wpe-ipc-probe.c
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>


#define LINE(n, verdict, ...) \
	do { \
		printf("IPCPROBE T%d %s: ", (n), (verdict)); \
		printf(__VA_ARGS__); \
		printf("\n"); \
		fflush(stdout); \
	} while (0)


static long nowMs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}


/* A file of `size` bytes, so a received descriptor can be identified by fstat() */
static int sizedFile(const char *path, size_t size)
{
	static const char fill[] = "0123456789abcdef";
	int fd;

	(void)unlink(path);
	fd = open(path, O_CREAT | O_RDWR, 0666);
	if ((fd >= 0) && (write(fd, fill, size) != (ssize_t)size)) {
		close(fd);
		fd = -1;
	}
	return fd;
}


static long fdSize(int fd)
{
	struct stat st;

	return (fstat(fd, &st) == 0) ? (long)st.st_size : -1L;
}


static ssize_t sendWithFd(int sock, const char *text, int fd)
{
	union {
		char buf[CMSG_SPACE(sizeof(int))];
		struct cmsghdr align;
	} u;
	struct iovec iov = { (void *)text, strlen(text) };
	struct msghdr msg;
	struct cmsghdr *cmsg;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	if (fd >= 0) {
		msg.msg_control = u.buf;
		msg.msg_controllen = CMSG_LEN(sizeof(int));
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
	}
	return sendmsg(sock, &msg, 0);
}


/* Receives one message; fds[] gets the descriptors, *nfds their count. */
static ssize_t recvWithFds(int sock, char *text, size_t len, int *fds, int *nfds)
{
	union {
		char buf[CMSG_SPACE(sizeof(int) * 8)];
		struct cmsghdr align;
	} u;
	struct iovec iov = { text, len };
	struct msghdr msg;
	struct cmsghdr *cmsg;
	ssize_t n;
	int cnt;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = u.buf;
	msg.msg_controllen = sizeof(u.buf);

	*nfds = 0;
	n = recvmsg(sock, &msg, 0);
	if (n < 0) {
		return n;
	}
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if ((cmsg->cmsg_level != SOL_SOCKET) || (cmsg->cmsg_type != SCM_RIGHTS)) {
			continue;
		}
		cnt = (int)((cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int));
		memcpy(fds + *nfds, CMSG_DATA(cmsg), sizeof(int) * (size_t)cnt);
		*nfds += cnt;
	}
	return n;
}


static void closeAll(const int *fds, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		close(fds[i]);
	}
}


/* T1: two SEQPACKET messages with one descriptor each, both queued before the first read */
static void t1FdPerMessage(int type, int n, const char *name)
{
	int sv[2], fa, fb, fds[8], n1, n2;
	long s1, s2;
	char text[8];

	if (socketpair(AF_UNIX, type, 0, sv) < 0) {
		LINE(n, "FAIL", "%s socketpair: %s", name, strerror(errno));
		return;
	}
	fa = sizedFile("/tmp/ipcprobe-a", 1);
	fb = sizedFile("/tmp/ipcprobe-b", 2);
	if ((fa < 0) || (fb < 0) || (sendWithFd(sv[0], "A", fa) != 1) || (sendWithFd(sv[0], "B", fb) != 1)) {
		LINE(n, "FAIL", "%s setup: %s", name, strerror(errno));
		return;
	}
	close(fa);
	close(fb);

	(void)recvWithFds(sv[1], text, sizeof(text), fds, &n1);
	s1 = (n1 > 0) ? fdSize(fds[0]) : -1;
	closeAll(fds, n1);
	(void)recvWithFds(sv[1], text, sizeof(text), fds, &n2);
	s2 = (n2 > 0) ? fdSize(fds[0]) : -1;
	closeAll(fds, n2);

	if ((n1 == 1) && (n2 == 1) && (s1 == 1) && (s2 == 2)) {
		LINE(n, "PASS", "%s msg1 fds=1 (size %ld) msg2 fds=1 (size %ld)", name, s1, s2);
	}
	else {
		LINE(n, "FAIL", "%s msg1 fds=%d (first size %ld) msg2 fds=%d (first size %ld); Linux: 1 and 1, each with its own message",
			name, n1, s1, n2, s2);
	}
	close(sv[0]);
	close(sv[1]);
	unlink("/tmp/ipcprobe-a");
	unlink("/tmp/ipcprobe-b");
}


/* T2: a message read without a control buffer must not leave its descriptor to the next one */
static void t2NoCarryOver(void)
{
	int sv[2], fa, fds[8], cnt;
	char text[8];

	if ((socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) < 0) || ((fa = sizedFile("/tmp/ipcprobe-a", 1)) < 0)) {
		LINE(2, "FAIL", "setup: %s", strerror(errno));
		return;
	}
	(void)sendWithFd(sv[0], "A", fa);
	(void)sendWithFd(sv[0], "B", -1);
	close(fa);

	(void)recv(sv[1], text, sizeof(text), 0);
	(void)recvWithFds(sv[1], text, sizeof(text), fds, &cnt);
	closeAll(fds, cnt);

	if (cnt == 0) {
		LINE(2, "PASS", "second message (sent without descriptors) got 0 descriptors");
	}
	else {
		LINE(2, "FAIL", "second message (sent without descriptors) got %d; Linux: 0 (the first message's are closed, MSG_CTRUNC)", cnt);
	}
	close(sv[0]);
	close(sv[1]);
	unlink("/tmp/ipcprobe-a");
}


/* T3: largest WebKit inline message (4096 bytes) on a fresh SEQPACKET socketpair */
static void t3Message4096(void)
{
	static char big[4096], got[4097];
	socklen_t len = sizeof(int);
	int sv[2], rcvbuf = -1;
	ssize_t n, m = -2;

	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0, sv) < 0) {
		LINE(3, "FAIL", "socketpair: %s", strerror(errno));
		return;
	}
	(void)getsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &rcvbuf, &len);
	memset(big, 'x', sizeof(big));
	n = send(sv[0], big, sizeof(big), 0);
	if (n < 0) {
		LINE(3, "FAIL", "send(4096) = -1 errno=%d (%s), SO_RCVBUF=%d; Linux: 4096", errno, strerror(errno), rcvbuf);
	}
	else {
		m = recv(sv[1], got, sizeof(got), 0);
		LINE(3, (m == 4096) ? "PASS" : "FAIL", "send(4096)=%zd recv=%zd SO_RCVBUF=%d", n, m, rcvbuf);
	}
	close(sv[0]);
	close(sv[1]);
}


/* T4: the peer process exits; a poll()ing reader sees POLLHUP and recv() returns 0 */
static void t4PeerExit(void)
{
	struct pollfd pfd;
	char c;
	int sv[2], r;
	ssize_t n;
	pid_t pid;
	long t0;

	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) < 0) {
		LINE(4, "FAIL", "socketpair: %s", strerror(errno));
		return;
	}
	pid = fork();
	if (pid == 0) {
		close(sv[1]);
		usleep(200 * 1000);
		_exit(0); /* sv[0] closes with the process */
	}
	close(sv[0]);
	t0 = nowMs();
	pfd.fd = sv[1];
	pfd.events = POLLIN;
	pfd.revents = 0;
	r = poll(&pfd, 1, 5000);
	n = recv(sv[1], &c, 1, MSG_DONTWAIT);
	(void)waitpid(pid, NULL, 0);
	LINE(4, ((r == 1) && ((pfd.revents & POLLHUP) != 0) && (n == 0)) ? "PASS" : "FAIL",
		"peer exited: poll=%d revents=0x%x (POLLIN=0x%x POLLHUP=0x%x) after %ld ms, recv=%zd", r, pfd.revents, POLLIN, POLLHUP,
		nowMs() - t0, n);
	close(sv[1]);
}


/* What a child process's extra thread blocks in, for T5..T8 */
enum { blockPipeRead, blockSocketRecv, blockPoll, blockTrap };

static int blockFds[2];


static void *blockerThread(void *arg)
{
	struct pollfd pfd;
	char c;

	switch ((int)(long)arg) {
		case blockPipeRead:
		case blockSocketRecv:
			(void)read(blockFds[0], &c, 1);
			break;
		case blockPoll:
			pfd.fd = blockFds[0];
			pfd.events = POLLIN;
			(void)poll(&pfd, 1, -1);
			break;
		case blockTrap:
			/* what g_error() does on Phoenix: no /proc/self/status, so GLib assumes a
			 * debugger and raises SIGTRAP instead of abort(), then spins in for (;;) */
			(void)raise(SIGTRAP);
			for (;;) {
			}
		default:
			break;
	}
	return NULL;
}


/* T5..T8: does a process whose second thread is blocked exit when its main thread calls _exit()? */
static void tBlockedExit(int n, int what, const char *name)
{
	pthread_t th;
	int status = 0;
	pid_t pid, r = 0;
	long t0, waited;

	pid = fork();
	if (pid == 0) {
		int ok = (what == blockSocketRecv) ? socketpair(AF_UNIX, SOCK_SEQPACKET, 0, blockFds) : pipe(blockFds);
		if ((ok < 0) || (pthread_create(&th, NULL, blockerThread, (void *)(long)what) != 0)) {
			_exit(99);
		}
		usleep(300 * 1000); /* the thread is blocked by now */
		if (what == blockTrap) {
			for (;;) {
				pause();
			}
		}
		_exit(7);
	}
	if (pid < 0) {
		LINE(n, "FAIL", "%s: fork: %s", name, strerror(errno));
		return;
	}

	t0 = nowMs();
	while ((waited = nowMs() - t0) < 4000) {
		r = waitpid(pid, &status, WNOHANG);
		if (r != 0) {
			break;
		}
		usleep(50 * 1000);
	}

	if (r == pid) {
		if (what == blockTrap) {
			LINE(n, (WIFSIGNALED(status) || (WIFEXITED(status) && (WEXITSTATUS(status) != 0))) ? "PASS" : "FAIL",
				"%s: process died in %ld ms (status 0x%x, signaled=%d sig=%d)", name, waited, status, WIFSIGNALED(status),
				WIFSIGNALED(status) ? WTERMSIG(status) : 0);
		}
		else {
			LINE(n, "PASS", "%s: process reaped %ld ms after its main thread called _exit (status 0x%x)", name, waited, status);
		}
		return;
	}

	LINE(n, "FAIL", "%s: process NOT gone %ld ms after %s; Linux: gone at once", name, waited,
		(what == blockTrap) ? "raise(SIGTRAP) in a thread" : "_exit() in the main thread");
	(void)kill(pid, SIGKILL);
	t0 = nowMs();
	while ((waited = nowMs() - t0) < 3000) {
		if (waitpid(pid, &status, WNOHANG) == pid) {
			LINE(n, "INFO", "%s: SIGKILL reaped it in %ld ms", name, waited);
			return;
		}
		usleep(50 * 1000);
	}
	LINE(n, "INFO", "%s: still not gone 3000 ms after SIGKILL (pid %d left behind: an unkillable thread)", name, (int)pid);
}


static void t9ProcStatus(void)
{
	int fd = open("/proc/self/status", O_RDONLY);

	/* GLib's _g_log_abort(): without this file it assumes a debugger is attached and
	 * g_error() raises SIGTRAP rather than calling abort() */
	LINE(9, "INFO", "/proc/self/status %s", (fd >= 0) ? "exists" : "absent (GLib g_error() -> raise(SIGTRAP), not abort())");
	if (fd >= 0) {
		close(fd);
	}
}


static int wanted(int argc, char **argv, int n)
{
	int i;

	if (argc < 2) {
		return 1;
	}
	for (i = 1; i < argc; i++) {
		if (atoi(argv[i]) == n) {
			return 1;
		}
	}
	return 0;
}


int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("IPCPROBE begin pid=%d\n", (int)getpid());

	if (wanted(argc, argv, 1)) {
		t1FdPerMessage(SOCK_SEQPACKET, 1, "SEQPACKET");
		t1FdPerMessage(SOCK_DGRAM, 1, "DGRAM");
	}
	if (wanted(argc, argv, 2)) {
		t2NoCarryOver();
	}
	if (wanted(argc, argv, 3)) {
		t3Message4096();
	}
	if (wanted(argc, argv, 4)) {
		t4PeerExit();
	}
	if (wanted(argc, argv, 5)) {
		tBlockedExit(5, blockPipeRead, "thread in read() on an empty pipe");
	}
	if (wanted(argc, argv, 6)) {
		tBlockedExit(6, blockSocketRecv, "thread in read() on a SEQPACKET socket");
	}
	if (wanted(argc, argv, 7)) {
		tBlockedExit(7, blockPoll, "thread in poll(-1) on a pipe");
	}
	if (wanted(argc, argv, 8)) {
		tBlockedExit(8, blockTrap, "raise(SIGTRAP) in a thread, then for (;;)");
	}
	if (wanted(argc, argv, 9)) {
		t9ProcStatus();
	}

	printf("IPCPROBE end\n");
	return 0;
}
