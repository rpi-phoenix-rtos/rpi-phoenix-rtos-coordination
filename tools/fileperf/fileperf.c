/*
 * fileperf -- per-syscall cost of touching many small files.
 *
 * Measurements through `cat`/`ls` in a shell loop time fork+exec, and even
 * `tar` bundles open/read/close/stat with its own per-file bookkeeping.  This
 * removes all of that: one process, one loop, each syscall timed separately, so
 * the ~100 ms/file cost seen on BOTH nfs and a ramdisk can be attributed to a
 * specific call rather than to a harness.
 *
 * Usage: fileperf <dir> [max_files]
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>

static long long now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ((long long)ts.tv_sec * 1000000LL) + (ts.tv_nsec / 1000);
}

int main(int argc, char **argv)
{
	const char *dir = (argc > 1) ? argv[1] : ".";
	int maxf = (argc > 2) ? atoi(argv[2]) : 200;
	long long t_open = 0, t_read = 0, t_close = 0, t_stat = 0, t_readdir = 0;
	long long bytes = 0, t0;
	int n = 0, nstat = 0;
	static char buf[65536];
	DIR *d;

	t0 = now_us();
	d = opendir(dir);
	if (d == NULL) {
		fprintf(stderr, "fileperf: cannot open dir %s\n", dir);
		return 1;
	}

	for (;;) {
		struct dirent *de;
		char path[512];
		long long a, b;
		int fd;

		a = now_us();
		de = readdir(d);
		t_readdir += now_us() - a;
		if (de == NULL) {
			break;
		}
		if ((strcmp(de->d_name, ".") == 0) || (strcmp(de->d_name, "..") == 0)) {
			continue;
		}
		if (n >= maxf) {
			break;
		}
		snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);

		{
			struct stat st;

			a = now_us();
			if (stat(path, &st) == 0) {
				t_stat += now_us() - a;
				nstat++;
				if (!S_ISREG(st.st_mode)) {
					continue;
				}
			}
			else {
				t_stat += now_us() - a;
				continue;
			}
		}

		a = now_us();
		fd = open(path, O_RDONLY);
		b = now_us();
		t_open += b - a;
		if (fd < 0) {
			continue;
		}

		for (;;) {
			ssize_t r;

			a = now_us();
			r = read(fd, buf, sizeof(buf));
			t_read += now_us() - a;
			if (r <= 0) {
				break;
			}
			bytes += r;
		}

		a = now_us();
		close(fd);
		t_close += now_us() - a;
		n++;
	}
	closedir(d);

	printf("FILEPERF dir=%s files=%d bytes=%lld total=%lldms\n", dir, n, bytes,
			(now_us() - t0) / 1000);
	printf("FILEPERF per-file us: readdir=%lld stat=%lld open=%lld read=%lld close=%lld\n",
			(n > 0) ? (t_readdir / n) : 0, (nstat > 0) ? (t_stat / nstat) : 0,
			(n > 0) ? (t_open / n) : 0, (n > 0) ? (t_read / n) : 0,
			(n > 0) ? (t_close / n) : 0);

	return 0;
}
