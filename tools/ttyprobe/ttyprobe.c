/* ttyprobe — minimal reproduction of readline's interactive read path, to pin
 * why GNU bash gets read()==0 (EOF) at its prompt where busybox ash blocks.
 * Prints fd0 identity, whether raw VMIN=1 is actually applied by tcsetattr,
 * and what select()+read() do (readline treats read()==0 as EOF).
 * SPDX-License-Identifier: BSD-3-Clause */
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
int main(void)
{
	struct stat st;
	struct termios t, t2;
	fd_set rfds;
	struct timeval tv;
	int s;
	char c;
	ssize_t n;

	fstat(0, &st);
	printf("PROBE fd0: isatty=%d ttyname=%s rdev=0x%lx\n",
		isatty(0), ttyname(0) ? ttyname(0) : "(null)", (unsigned long)st.st_rdev);
	fflush(stdout);

	if (tcgetattr(0, &t) == 0) {
		printf("PROBE before: VMIN=%u VTIME=%u ICANON=%d\n",
			t.c_cc[VMIN], t.c_cc[VTIME], (t.c_lflag & ICANON) != 0);
		t.c_lflag &= ~(ICANON | ECHO);
		t.c_cc[VMIN] = 1;
		t.c_cc[VTIME] = 0;
		if (tcsetattr(0, TCSANOW, &t) != 0)
			printf("PROBE tcsetattr FAILED\n");
		memset(&t2, 0, sizeof(t2));
		tcgetattr(0, &t2);
		printf("PROBE after:  VMIN=%u VTIME=%u ICANON=%d  (readline wants VMIN=1 VTIME=0 ICANON=0)\n",
			t2.c_cc[VMIN], t2.c_cc[VTIME], (t2.c_lflag & ICANON) != 0);
	}
	else {
		printf("PROBE tcgetattr FAILED (not a tty?)\n");
	}
	fflush(stdout);

	/* readline loops select()+read(); drain leftover input then observe the
	 * EMPTY-fifo case: does select() spuriously report readable -> read()==0 (EOF)? */
	for (int i = 0; i < 8; i++) {
		FD_ZERO(&rfds);
		FD_SET(0, &rfds);
		tv.tv_sec = 2; tv.tv_usec = 0;
		s = select(1, &rfds, NULL, NULL, &tv);
		if (s > 0 && FD_ISSET(0, &rfds)) {
			n = read(0, &c, 1);
			printf("PROBE iter%d: select=readable read=%zd byte=0x%02x%s\n",
				i, n, (n == 1) ? (unsigned char)c : 0,
				(n == 0) ? "  <== READ==0 EOF-BUG (spurious-readable + empty read=0)" : "");
			fflush(stdout);
			if (n <= 0)
				break;
		}
		else {
			printf("PROBE iter%d: select timed out (empty fifo, no spurious readable => readline would BLOCK here, correct)\n", i);
			fflush(stdout);
			break;
		}
	}
	/* readline's type-ahead gather (rl_gather_tyi) sets O_NONBLOCK and reads a
	 * batch; POSIX says an empty non-blocking read returns -1/EAGAIN, but if
	 * Phoenix returns 0 here, readline interprets it as EOF -> bash exits. */
	{
		int fl = fcntl(0, F_GETFL);
		char buf[8];
		ssize_t nn;
		fcntl(0, F_SETFL, fl | O_NONBLOCK);
		errno = 0;
		nn = read(0, buf, sizeof(buf));
		printf("PROBE nonblock-empty: read=%zd errno=%d (want -1/EAGAIN=%d; read==0 => the readline type-ahead EOF bug)\n",
			nn, errno, EAGAIN);
		fcntl(0, F_SETFL, fl);
		fflush(stdout);
	}
	/* THE decisive test: readline's rl_getc does select() with a NULL (blocking)
	 * timeout; if Phoenix returns 0 there (instead of blocking), rl_getc treats it
	 * as a timeout -> _rl_timeout_handle -> _rl_abort_internal -> bash exits. */
	signal(SIGALRM, SIG_DFL);
	alarm(4);  /* if the NULL-timeout select correctly BLOCKS, SIGALRM kills us at 4s */
	{
		fd_set nrf; FD_ZERO(&nrf); FD_SET(0, &nrf);
		int ns = select(1, &nrf, NULL, NULL, NULL);  /* NULL timeout = block */
		int ne = errno;
		printf("PROBE null-select: ret=%d errno=%d  (ret==0 => Phoenix select(NULL) doesn't block => THE bash EOF bug; if this line never prints, it blocked correctly and alarm fired)\n", ns, ne);
	}
	alarm(0);
	printf("PROBE done\n");
	fflush(stdout);
	return 0;
}
