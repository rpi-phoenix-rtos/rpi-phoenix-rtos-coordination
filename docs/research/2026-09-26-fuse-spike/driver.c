/* SPDX-License-Identifier: BSD-3-Clause -- host-only spike for the FUSE feasibility study (2026-09-26) */
/*
 * SPIKE host driver: plays the Phoenix kernel + libphoenix client side.
 * A client thread sends msg_t requests exactly as libphoenix/kernel posix
 * would (see report for the per-call message sequences); the FUSE daemon
 * thread (exfat's unmodified main() -> fuse_main -> fuse_session_loop)
 * receives them through phx_recv()/phx_respond(), which stand in for
 * msgRecv()/msgRespond() on a Phoenix port.
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include "phx_msg.h"

#define mtRename 0xf55	/* next free in libphoenix sys/file.h range (mtMountPoint = 0xf54) */
#define mtSync 0xf52
#define PORT 7

struct fuse_chan;
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static msg_t *pending;		/* request waiting to be received */
static int responded, done, mounted;
static unsigned long nmsgs;

int phx_mount(const char *dir, struct fuse_chan *fc)
{
	(void)fc;
	/* Phoenix: portCreate(&port); lookup(dir) -> mountpoint oid;
	 * msgSend(mtSetAttr atDev = {port, root id}) to the parent server,
	 * exactly like dummyfs/srv.c:59-85. */
	fprintf(stderr, "[phx] mount at %s (port %d)\n", dir, PORT);
	pthread_mutex_lock(&mx); mounted = 1; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mx);
	return 0;
}
void phx_unmount(const char *dir, struct fuse_chan *fc) { (void)fc; fprintf(stderr, "[phx] unmount %s\n", dir); }
uint32_t phx_port(struct fuse_chan *ch) { (void)ch; return PORT; }

int phx_recv(struct fuse_chan *ch, msg_t *msg, int *rid)
{
	(void)ch;
	pthread_mutex_lock(&mx);
	while (pending == NULL && !done)
		pthread_cond_wait(&cv, &mx);
	if (pending == NULL) { pthread_mutex_unlock(&mx); return -1; }
	*msg = *pending;	/* kernel copies header; data buffers are mapped */
	*rid = 1;
	pthread_mutex_unlock(&mx);
	return 0;
}

void phx_respond(struct fuse_chan *ch, msg_t *msg, int rid)
{
	(void)ch; (void)rid;
	pthread_mutex_lock(&mx);
	pending->o = msg->o;
	pending = NULL;
	responded = 1;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&mx);
}

static int msgSend(msg_t *m)
{
	pthread_mutex_lock(&mx);
	m->oid.port = PORT;
	pending = m; responded = 0; nmsgs++;
	pthread_cond_broadcast(&cv);
	while (!responded)
		pthread_cond_wait(&cv, &mx);
	pthread_mutex_unlock(&mx);
	return m->o.err;
}

/* ---- client helpers mirroring libphoenix / kernel posix ---- */
static oid_t lookup(const char *path)
{
	msg_t m = { .type = mtLookup, .oid = { PORT, 1 }, .i.data = path, .i.size = strlen(path) + 1 };
	int r = msgSend(&m);
	oid_t bad = { 0, 0 };
	return r < 0 ? bad : m.o.lookup.fil;
}

static int create(oid_t dir, const char *name, int type, unsigned mode, oid_t *res)
{
	msg_t m = { .type = mtCreate, .oid = dir, .i.create.type = type, .i.create.mode = mode, .i.data = name, .i.size = strlen(name) + 1 };
	int r = msgSend(&m);
	if (res) *res = m.o.create.oid;
	return r;
}

static int simple(int type, oid_t oid)
{
	msg_t m = { .type = type, .oid = oid };
	return msgSend(&m);
}

static int rw(int type, oid_t oid, off_t offs, void *buf, size_t len)
{
	msg_t m = { .type = type, .oid = oid, .i.io.offs = offs };
	if (type == mtWrite) { m.i.data = buf; m.i.size = len; }
	else { m.o.data = buf; m.o.size = len; }
	return msgSend(&m);
}

static long long getattr_all(oid_t oid, struct _attrAll *a)
{
	msg_t m = { .type = mtGetAttrAll, .oid = oid, .o.data = a, .o.size = sizeof(*a) };
	return msgSend(&m);
}

static void ls(oid_t dir, const char *label)
{
	char buf[sizeof(struct phx_dirent) + 256];
	struct phx_dirent *d = (void *)buf;
	off_t offs = 0;
	printf("  ls %s:", label);
	for (;;) {
		msg_t m = { .type = mtReaddir, .oid = dir, .i.readdir.offs = offs, .o.data = buf, .o.size = sizeof(buf) };
		if (msgSend(&m) < 0) break;
		printf(" %s(ino %llu,t%u)", d->d_name, (unsigned long long)d->d_ino, d->d_type);
		offs += d->d_reclen;
	}
	printf("\n");
}

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } else printf("ok:   " __VA_ARGS__), printf("\n"); } while (0)

static int mode_verify;	/* second run: only verify persistence */

static void *client(void *arg)
{
	int fails = 0, r;
	oid_t root = { PORT, 1 }, d, f, g;
	struct _attrAll a;
	static char wbuf[300000], rbuf[300000];
	(void)arg;

	pthread_mutex_lock(&mx);
	while (!mounted) pthread_cond_wait(&cv, &mx);
	pthread_mutex_unlock(&mx);

	if (mode_verify) {
		g = lookup("moved.txt");
		CHECK(g.port == PORT, "persist: lookup moved.txt -> id %llu", (unsigned long long)g.id);
		simple(mtOpen, g);
		memset(rbuf, 0, 64);
		r = rw(mtRead, g, 0, rbuf, 64);
		CHECK(r == 5 && memcmp(rbuf, "Hello", 5) == 0, "persist: read back after remount: %d bytes '%.5s'", r, rbuf);
		simple(mtClose, g);
		g = lookup("d2/deep/x.bin");
		CHECK(g.port == PORT, "persist: nested file lookup d2/deep/x.bin");
		ls(root, "/");
		goto out;
	}

	CHECK(getattr_all(root, &a) == 0 && a.type.val == otDir, "stat / (GetAttrAll) type=%lld", a.type.val);
	CHECK(create(root, "d", otDir, 0755, &d) == 0, "mkdir d -> id %llu", (unsigned long long)d.id);
	CHECK(create(d, "hello.txt", otFile, 0644, &f) == 0, "create d/hello.txt -> id %llu", (unsigned long long)f.id);
	CHECK(simple(mtOpen, f) == 0, "open");
	for (size_t i = 0; i < sizeof(wbuf); i++) wbuf[i] = "Hello from a Phoenix msg_t\n"[i % 27];
	size_t off = 0;
	while (off < sizeof(wbuf)) {	/* kernel splits I/O; use 64 KiB chunks */
		size_t n = sizeof(wbuf) - off < 65536 ? sizeof(wbuf) - off : 65536;
		r = rw(mtWrite, f, off, wbuf + off, n);
		if (r <= 0) break;
		off += r;
	}
	CHECK(off == sizeof(wbuf), "write %zu bytes in 64K mtWrite chunks", off);
	r = rw(mtRead, f, 0, rbuf, sizeof(rbuf));
	CHECK(r == (int)sizeof(rbuf) && memcmp(rbuf, wbuf, sizeof(rbuf)) == 0, "read back %d bytes, content matches", r);
	CHECK(simple(mtClose, f) == 0, "close");
	CHECK(getattr_all(f, &a) == 0 && a.size.val == (long long)sizeof(wbuf), "GetAttrAll size=%lld links=%lld", a.size.val, a.links.val);
	CHECK(lookup("d/hello.txt").id == f.id, "multi-component lookup 'd/hello.txt' returns same id");
	CHECK(lookup("d/nope").port == 0, "lookup of missing name fails");
	ls(root, "/");
	ls(d, "/d");

	/* libphoenix rename() today = link(old,new) + unlink(old) */
	{
		msg_t m = { .type = mtLink, .oid = root, .i.ln.oid = f, .i.data = "moved.txt", .i.size = 10 };
		r = msgSend(&m);
		CHECK(r < 0, "rename via mtLink on exFAT fails as expected (no hard links): err=%d (%s)", r, strerror(-r));
	}
	{	/* proposed mtRename */
		const char names[] = "hello.txt\0moved.txt";
		msg_t m = { .type = mtRename, .oid = d, .i.ln.oid = root, .i.data = names, .i.size = sizeof(names) };
		r = msgSend(&m);
		CHECK(r == 0, "PROPOSED mtRename d/hello.txt -> /moved.txt: %d", r);
	}
	g = lookup("moved.txt");
	CHECK(g.port == PORT, "lookup moved.txt after rename (id %llu, was %llu)", (unsigned long long)g.id, (unsigned long long)f.id);
	{
		msg_t m = { .type = mtTruncate, .oid = g, .i.io.len = 5 };
		CHECK(msgSend(&m) == 0, "truncate to 5");
	}
	CHECK(getattr_all(g, &a) == 0 && a.size.val == 5, "size after truncate = %lld", a.size.val);
	{
		msg_t m = { .type = mtUnlink, .oid = root, .i.data = "d", .i.size = 2 };
		CHECK(msgSend(&m) == 0, "rmdir d (mtUnlink on a dir)");
	}
	{
		oid_t d2, deep, x;
		create(root, "d2", otDir, 0755, &d2);
		create(d2, "deep", otDir, 0755, &deep);
		CHECK(create(deep, "x.bin", otFile, 0644, &x) == 0, "nested create d2/deep/x.bin");
	}
	{
		struct statvfs sv;
		msg_t m = { .type = mtStat, .o.data = &sv, .o.size = sizeof(sv) };
		CHECK(msgSend(&m) == 0, "statvfs (mtStat): bsize=%lu blocks=%lu bfree=%lu", sv.f_bsize, (unsigned long)sv.f_blocks, (unsigned long)sv.f_bfree);
	}
	ls(root, "/");
out:
	printf("RESULT: %d failure(s), %lu messages\n", fails, nmsgs);
	pthread_mutex_lock(&mx); done = 1; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mx);
	return NULL;
}

extern int exfat_main(int argc, char **argv);

int main(int argc, char **argv)
{
	pthread_t t;
	char *av[] = { "mount.exfat", argv[1], "/mnt/exfat", NULL };
	if (argc < 2) { fprintf(stderr, "usage: %s image.exfat [verify]\n", argv[0]); return 2; }
	mode_verify = argc > 2;
	pthread_create(&t, NULL, client, NULL);
	int r = exfat_main(3, av);	/* UNMODIFIED exfat-fuse main() */
	pthread_join(t, NULL);
	return r;
}
