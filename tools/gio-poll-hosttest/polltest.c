/*
 * Host test of GIO's "poll" GLocalFileMonitor (ports gtk3_wayland, GLib patch
 * 0004), run against a host build of the patched GLib with statshim.so
 * preloaded. See run.sh.
 *
 *   polltest functional DIR   create/append/chmod/rename/mkdir/unlink/rmdir/
 *                             rename-over in DIR, each event within a deadline
 *   polltest samesecond DIR   an entry added while the directory's stamps stay
 *                             as the monitor last saw them (a change in the
 *                             same timestamp granule as the read before it):
 *                             must be reported on the next ticks, and must not
 *                             be mistaken for a filesystem that never updates
 *                             the stamps
 *   polltest frozen DIR       the directory's stamps never move (a filesystem
 *                             that does not update them): the first change is
 *                             reported by a sweep, every later one within a tick
 *   polltest count DIR N W M  N entries, W s warm-up, then M s idle: the calls
 *                             the monitor makes per second
 *
 * DIR must not exist; it is created and removed. SHIM_WATCH must name it.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gio/gio.h>

struct shim_counts {
	long stat_dir, stat_other, lstat, opendir, readdir, closedir;
};

static void (*counts_fn)(struct shim_counts *);
static int (*hold_fn)(const char *);
static void (*release_fn)(void);

typedef struct {
	GFileMonitorEvent event;
	char *name;
	char *other;
} Event;

static GQueue events = G_QUEUE_INIT;
static int fails;
static const char *dir;

static const char *event_name(GFileMonitorEvent e)
{
	switch (e) {
		case G_FILE_MONITOR_EVENT_CHANGED: return "CHANGED";
		case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT: return "CHANGES_DONE_HINT";
		case G_FILE_MONITOR_EVENT_DELETED: return "DELETED";
		case G_FILE_MONITOR_EVENT_CREATED: return "CREATED";
		case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED: return "ATTRIBUTE_CHANGED";
		case G_FILE_MONITOR_EVENT_RENAMED: return "RENAMED";
		case G_FILE_MONITOR_EVENT_MOVED_IN: return "MOVED_IN";
		case G_FILE_MONITOR_EVENT_MOVED_OUT: return "MOVED_OUT";
		default: return "?";
	}
}

static void on_changed(GFileMonitor *m, GFile *file, GFile *other, GFileMonitorEvent event, gpointer data)
{
	(void)m;
	(void)data;
	Event *e = g_new0(Event, 1);
	e->event = event;
	e->name = g_file_get_basename(file);
	e->other = (other != NULL) ? g_file_get_basename(other) : NULL;
	g_queue_push_tail(&events, e);
}

static void event_free(gpointer p)
{
	Event *e = p;
	g_free(e->name);
	g_free(e->other);
	g_free(e);
}

static void spin(double secs)
{
	gint64 until = g_get_monotonic_time() + (gint64)(secs * G_USEC_PER_SEC);
	while (g_get_monotonic_time() < until) {
		g_main_context_iteration(NULL, FALSE);
		g_usleep(5000);
	}
}

/* Let the previous step's events arrive, then forget them. */
static void settle(double secs)
{
	spin(secs);
	g_queue_clear_full(&events, event_free);
}

/* Wait up to @limit s for @event on @name (and @other); the seconds it took, or -1. */
static double wait_for(GFileMonitorEvent event, const char *name, const char *other, double limit, const char *what)
{
	gint64 start = g_get_monotonic_time();
	for (;;) {
		Event *e;
		while ((e = g_queue_pop_head(&events)) != NULL) {
			gboolean match = (e->event == event) && (strcmp(e->name, name) == 0) &&
				((other == NULL) || ((e->other != NULL) && (strcmp(e->other, other) == 0)));
			event_free(e);
			if (match) {
				double t = (g_get_monotonic_time() - start) / 1e6;
				printf("  [ ok ] %-44s %s after %.2f s\n", what, event_name(event), t);
				return t;
			}
		}
		if (g_get_monotonic_time() - start > (gint64)(limit * G_USEC_PER_SEC)) {
			printf("  [FAIL] %-44s no %s on %s within %.1f s\n", what, event_name(event), name, limit);
			fails++;
			return -1;
		}
		g_main_context_iteration(NULL, FALSE);
		g_usleep(5000);
	}
}

static char *child(const char *name)
{
	return g_build_filename(dir, name, NULL);
}

static void put(const char *name, const char *text, gboolean append)
{
	char *p = child(name);
	int fd = open(p, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
	if ((fd < 0) || (write(fd, text, strlen(text)) != (ssize_t)strlen(text))) {
		printf("  [FAIL] write %s: %s\n", p, strerror(errno));
		fails++;
	}
	if (fd >= 0) {
		close(fd);
	}
	g_free(p);
}

static void ren(const char *from, const char *to)
{
	char *a = child(from), *b = child(to);
	if (rename(a, b) != 0) {
		printf("  [FAIL] rename %s %s: %s\n", a, b, strerror(errno));
		fails++;
	}
	g_free(a);
	g_free(b);
}

static void del(const char *name)
{
	char *p = child(name);
	if (remove(p) != 0) {
		printf("  [FAIL] remove %s: %s\n", p, strerror(errno));
		fails++;
	}
	g_free(p);
}

static GFileMonitor *watch(void)
{
	GError *err = NULL;
	GFile *f = g_file_new_for_path(dir);
	GFileMonitor *m = g_file_monitor_directory(f, G_FILE_MONITOR_WATCH_MOVES, NULL, &err);
	g_object_unref(f);
	if (m == NULL) {
		printf("  [FAIL] monitor %s: %s\n", dir, err->message);
		exit(1);
	}
	if (strcmp(G_OBJECT_TYPE_NAME(m), "GPollLocalFileMonitor") != 0) {
		printf("  [FAIL] monitor is %s, not GPollLocalFileMonitor (wrong libgio loaded?)\n", G_OBJECT_TYPE_NAME(m));
		exit(1);
	}
	g_signal_connect(m, "changed", G_CALLBACK(on_changed), NULL);
	return m;
}

/* opendir() calls by the monitor over @secs of quiet. */
static long reads_while_idle(double secs)
{
	struct shim_counts a, b;
	counts_fn(&a);
	spin(secs);
	counts_fn(&b);
	return b.opendir - a.opendir;
}

static void functional(void)
{
	GFileMonitor *m = watch();
	settle(0.5);

	put("a", "hello\n", FALSE);
	wait_for(G_FILE_MONITOR_EVENT_CREATED, "a", NULL, 3, "create a");
	settle(0.3);
	put("a", "more\n", TRUE);
	wait_for(G_FILE_MONITOR_EVENT_CHANGED, "a", NULL, 6, "append to a (sweep)");
	settle(0.3);
	char *pa = child("a");
	chmod(pa, 0600);
	g_free(pa);
	wait_for(G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED, "a", NULL, 6, "chmod a (sweep)");
	settle(0.3);
	ren("a", "b");
	wait_for(G_FILE_MONITOR_EVENT_RENAMED, "a", "b", 3, "rename a -> b");
	settle(0.3);
	char *ps = child("sub");
	mkdir(ps, 0755);
	wait_for(G_FILE_MONITOR_EVENT_CREATED, "sub", NULL, 3, "mkdir sub");
	settle(0.3);
	del("b");
	wait_for(G_FILE_MONITOR_EVENT_DELETED, "b", NULL, 3, "unlink b");
	settle(0.3);
	rmdir(ps);
	g_free(ps);
	wait_for(G_FILE_MONITOR_EVENT_DELETED, "sub", NULL, 3, "rmdir sub");
	settle(0.3);
	put("c", "c\n", FALSE);
	put("d", "d\n", FALSE);
	wait_for(G_FILE_MONITOR_EVENT_CREATED, "d", NULL, 3, "create c, d");
	settle(0.3);
	ren("c", "d");
	wait_for(G_FILE_MONITOR_EVENT_RENAMED, "c", "d", 3, "rename c over d");
	settle(0.3);

	/* Quiet now: after the window the monitor must stop reading the entries
	 * between sweeps. 8 ticks: at most 2 sweeps (period 4, then 8). */
	settle(3.5);
	long n = reads_while_idle(8);
	printf("  [%s] %-44s %ld reads in 8 s\n", (n <= 2) ? " ok " : "FAIL", "idle: entries read only by sweeps", n);
	fails += (n > 2);

	/* Activity after a long quiet: still reported within a tick or two. */
	settle(20);
	put("e", "e\n", FALSE);
	wait_for(G_FILE_MONITOR_EVENT_CREATED, "e", NULL, 3, "create e after 30 s idle");
	settle(0.3);
	del("e");
	wait_for(G_FILE_MONITOR_EVENT_DELETED, "e", NULL, 3, "unlink e");
	del("d");
	wait_for(G_FILE_MONITOR_EVENT_DELETED, "d", NULL, 3, "unlink d");

	g_file_monitor_cancel(m);
	g_object_unref(m);
}

static void samesecond(void)
{
	GFileMonitor *m = watch();
	settle(3.5); /* trusted: the monitor skips the read while the stamps hold */

	for (int i = 0; i < 4; i++) {
		char x[8], y[8], what[64];
		snprintf(x, sizeof(x), "x%d", i);
		snprintf(y, sizeof(y), "y%d", i);
		put(x, "x\n", FALSE);
		wait_for(G_FILE_MONITOR_EVENT_CREATED, x, NULL, 3, x);
		/* The monitor read the entries a moment ago, right after it saw the
		 * stamps move. Freeze them as it saw them and add another entry. */
		hold_fn(dir);
		put(y, "y\n", FALSE);
		snprintf(what, sizeof(what), "%s, stamps as after %s", y, x);
		double t = wait_for(G_FILE_MONITOR_EVENT_CREATED, y, NULL, 6, what);
		if (t > 2.5) {
			printf("  [FAIL] %-44s took %.2f s: found by a sweep, not by the guard\n", what, t);
			fails++;
		}
		release_fn();
		settle(3.5);
	}

	/* A same-granule change is not a filesystem that ignores the stamps: the
	 * monitor must still skip the read between sweeps. */
	long n = reads_while_idle(8);
	printf("  [%s] %-44s %ld reads in 8 s\n", (n <= 2) ? " ok " : "FAIL", "idle afterwards: no fallback", n);
	fails += (n > 2);

	g_file_monitor_cancel(m);
	g_object_unref(m);
}

static void frozen(void)
{
	GFileMonitor *m = watch();
	settle(5); /* past the first sweep, so the next one is 8 ticks away */
	hold_fn(dir); /* from now on the directory's stamps never move */

	put("z", "z\n", FALSE);
	double t = wait_for(G_FILE_MONITOR_EVENT_CREATED, "z", NULL, 40, "first change (found by a sweep)");
	(void)t;
	settle(0.3);
	put("w", "w\n", FALSE);
	wait_for(G_FILE_MONITOR_EVENT_CREATED, "w", NULL, 2.5, "second change (read on every tick now)");
	settle(0.3);
	del("w");
	wait_for(G_FILE_MONITOR_EVENT_DELETED, "w", NULL, 2.5, "third change");
	del("z");
	wait_for(G_FILE_MONITOR_EVENT_DELETED, "z", NULL, 2.5, "fourth change");
	release_fn();

	g_file_monitor_cancel(m);
	g_object_unref(m);
}

static void count(int n, double warm, double secs, int depth)
{
	for (int i = 0; i < n; i++) {
		char name[32];
		snprintf(name, sizeof(name), "entry-%03d.desktop", i);
		put(name, "[Desktop Entry]\n", FALSE);
	}

	GFileMonitor *m = watch();
	spin(warm);
	struct shim_counts a, b;
	counts_fn(&a);
	spin(secs);
	counts_fn(&b);

	double sd = (b.stat_dir - a.stat_dir) / secs, lst = (b.lstat - a.lstat) / secs;
	double od = (b.opendir - a.opendir) / secs, rd = (b.readdir - a.readdir) / secs;
	double cd = (b.closedir - a.closedir) / secs;
	printf("  per second over %.0f s idle, %d entries:\n", secs, n);
	printf("    stat(dir) %.2f  lstat(entry) %.2f  opendir %.2f  readdir %.2f  closedir %.2f  (other stat %.2f)\n",
		sd, lst, od, rd, cd, (b.stat_other - a.stat_other) / secs);
	/* What these calls cost on Phoenix-RTOS (libphoenix unistd/dir.c,
	 * sys/stat.c) for a directory path of @depth components, none a symlink:
	 * resolve_path() checks every component it resolves with a lookup and an
	 * mtGetAttr, so stat() of the directory and lstat() of an entry (whose
	 * last component is not resolved) are 2*depth + 2 messages each (+ lookup,
	 * mtGetAttrAll); opendir() 2*depth + 3 (+ lookup, mtGetAttr, mtOpen); each
	 * readdir() one mtReaddir, "." and ".." and the end included as here;
	 * closedir() one mtClose. */
	double msgs = sd * (2 * depth + 2) + lst * (2 * depth + 2) + od * (2 * depth + 3) + rd + cd;
	printf("    as Phoenix-RTOS messages (depth %d): %.1f/s\n", depth, msgs);

	g_file_monitor_cancel(m);
	g_object_unref(m);
	for (int i = 0; i < n; i++) {
		char name[32];
		snprintf(name, sizeof(name), "entry-%03d.desktop", i);
		del(name);
	}
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 3) {
		fprintf(stderr, "usage: %s functional|samesecond|frozen|count DIR [N WARM SECS DEPTH]\n", argv[0]);
		return 2;
	}
	counts_fn = dlsym(RTLD_DEFAULT, "shim_counts");
	hold_fn = dlsym(RTLD_DEFAULT, "shim_hold");
	release_fn = dlsym(RTLD_DEFAULT, "shim_release");
	if ((counts_fn == NULL) || (hold_fn == NULL) || (release_fn == NULL)) {
		fprintf(stderr, "statshim.so is not preloaded\n");
		return 2;
	}
	dir = argv[2];
	if (mkdir(dir, 0755) != 0) {
		fprintf(stderr, "mkdir %s: %s\n", dir, strerror(errno));
		return 2;
	}

	if (strcmp(argv[1], "functional") == 0) {
		functional();
	}
	else if (strcmp(argv[1], "samesecond") == 0) {
		samesecond();
	}
	else if (strcmp(argv[1], "frozen") == 0) {
		frozen();
	}
	else if (strcmp(argv[1], "count") == 0) {
		count((argc > 3) ? atoi(argv[3]) : 60, (argc > 4) ? atof(argv[4]) : 30, (argc > 5) ? atof(argv[5]) : 64,
			(argc > 6) ? atoi(argv[6]) : 3);
	}
	else {
		fprintf(stderr, "unknown test %s\n", argv[1]);
		return 2;
	}

	/* whatever a failed step left behind */
	GDir *d = g_dir_open(dir, 0, NULL);
	const char *e;
	while ((d != NULL) && ((e = g_dir_read_name(d)) != NULL)) {
		char *p = child(e);
		remove(p);
		g_free(p);
	}
	if (d != NULL) {
		g_dir_close(d);
	}
	rmdir(dir);

	printf("%s: %s, %d failure(s)\n", argv[1], fails ? "FAIL" : "PASS", fails);
	return fails ? 1 : 0;
}
