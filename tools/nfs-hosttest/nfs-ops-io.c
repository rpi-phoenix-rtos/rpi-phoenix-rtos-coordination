/*
 * Host test for the NFS server's read/write handlers (phoenix-rtos-filesystems
 * nfs/nfs_ops.c), linked against a scripted in-memory libnfs.
 *
 * The real nfs_ops.c, nfs_node.c and nfs_dir.c are compiled unchanged; libnfs is
 * replaced by the mock below, which keeps one file in memory and can be told to
 * time a call out (killing its context, as the sync-call deadline does), to let
 * part of a failed WRITE land first, to answer short, or to refuse a re-mount.
 *
 * What is checked (each case prints PASS/FAIL, the last line is the verdict):
 *   - no READ/WRITE RPC carries more than the negotiated maximum, however large
 *     the request (build 39: one 54 MB WRITE was never answered);
 *   - after a timeout the write is resumed on a rebuilt context at the right
 *     offset, without re-sending what the server had acknowledged, and the file
 *     ends up byte-identical to the buffer;
 *   - a failure after some progress returns the short count of what is on the
 *     server; with no progress, the error;
 *   - no handle or context is used after the reclaim destroyed it;
 *   - a timed-out open goes to the reclaim instead of retrying on a dead context;
 *   - nfs_ops_recover() rebuilds an abandoned context and leaves a live one alone.
 *
 * Last line: "NFS-OPS-IO: PASS" or "NFS-OPS-IO: FAIL (n case(s))".
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <nfsc/libnfs.h>

#include "nfs_ops.h"


/* ----------------------------------------------------------------------------
 * The mock server: one file, scripted faults.
 * ------------------------------------------------------------------------- */

#define FILE_CAP (8u << 20)
#define WMAX     (64u * 1024u)
#define RMAX     (32u * 1024u)
#define MAXCTX   64

struct nfs_context {
	int gen;
	int dead;      /* a call ran into its deadline: no socket (nfs_get_fd -1) */
	int destroyed; /* nfs_destroy_context() was called on it */
};

struct nfsfh {
	struct nfs_context *ctx;
	int closed;
};

static struct {
	struct nfs_context ctx[MAXCTX];
	int nctx;

	unsigned char file[FILE_CAP];
	unsigned char acked[FILE_CAP]; /* 1 = the server acknowledged a WRITE of this byte */
	uint64_t size;

	/* scripted faults */
	int pwriteCalls;
	int failPwriteFrom;    /* pwrite #N (1-based) and later time out; 0 = never */
	int failPwriteCount;   /* ... this many times (0 = forever) */
	int failPwriteHits;
	int landHalfOnFailure; /* a timed-out WRITE still lands its first half */
	int shortEvery;        /* every Nth pwrite only writes half; 0 = never */
	int mountFails;
	int openTimeouts;      /* the next N nfs_open calls time out */

	/* observations */
	size_t maxWrite, maxRead;
	int rewroteAcked;
	int staleUse; /* a handle or context used after it was destroyed */
	int opensOnDeadCtx;
	int mounts;
} m;


static void mockReset(void)
{
	memset(&m, 0, sizeof(m));
}


static struct nfs_context *newCtx(void)
{
	if (m.nctx >= MAXCTX) {
		abort();
	}
	struct nfs_context *c = &m.ctx[m.nctx];
	c->gen = m.nctx++;
	return c;
}


static void useCtx(struct nfs_context *c)
{
	if ((c == NULL) || (c->destroyed != 0)) {
		m.staleUse++;
	}
}


static void useFh(struct nfs_context *c, struct nfsfh *fh)
{
	useCtx(c);
	if ((fh == NULL) || (fh->closed != 0) || (fh->ctx != c) || (fh->ctx->destroyed != 0)) {
		m.staleUse++;
	}
}


/* What the patched libnfs does at the overall deadline. */
static int timeOut(struct nfs_context *c)
{
	c->dead = 1;
	return -ETIMEDOUT;
}


struct nfs_context *nfs_makeContext(int version)
{
	(void)version;
	return newCtx();
}


struct nfs_context *nfs_init_context(void)
{
	return newCtx();
}


void nfs_destroy_context(struct nfs_context *nfs)
{
	useCtx(nfs);
	nfs->destroyed = 1;
}


int nfs_mount(struct nfs_context *nfs, const char *server, const char *exportname)
{
	(void)server;
	(void)exportname;
	useCtx(nfs);
	m.mounts++;
	return (m.mountFails != 0) ? -EIO : 0;
}


int nfs_get_fd(struct nfs_context *nfs)
{
	useCtx(nfs);
	return (nfs->dead != 0) ? -1 : 3;
}


char *nfs_get_error(struct nfs_context *nfs)
{
	(void)nfs;
	return "mock";
}


size_t nfs_get_writemax(struct nfs_context *nfs)
{
	(void)nfs;
	return WMAX;
}


size_t nfs_get_readmax(struct nfs_context *nfs)
{
	(void)nfs;
	return RMAX;
}


int nfs_open(struct nfs_context *nfs, const char *path, int flags, struct nfsfh **nfsfh)
{
	(void)path;
	(void)flags;
	useCtx(nfs);
	if (nfs->dead != 0) {
		m.opensOnDeadCtx++;
		return -ETIMEDOUT;
	}
	if (m.openTimeouts > 0) {
		m.openTimeouts--;
		return timeOut(nfs);
	}
	struct nfsfh *fh = calloc(1, sizeof(*fh));
	fh->ctx = nfs;
	*nfsfh = fh;
	return 0;
}


int nfs_close(struct nfs_context *nfs, struct nfsfh *nfsfh)
{
	useFh(nfs, nfsfh);
	nfsfh->closed = 1; /* kept, not freed: a later use is then detectable */
	return 0;
}


int nfs_pwrite(struct nfs_context *nfs, struct nfsfh *nfsfh, const void *buf, size_t count, uint64_t offset)
{
	useFh(nfs, nfsfh);
	m.pwriteCalls++;
	if (count > m.maxWrite) {
		m.maxWrite = count;
	}
	if (nfs->dead != 0) {
		return -ETIMEDOUT;
	}
	if ((offset + count) > FILE_CAP) {
		return -EFBIG;
	}
	for (size_t i = 0; i < count; i++) {
		if (m.acked[offset + i] != 0) {
			m.rewroteAcked++;
			break;
		}
	}

	if ((m.failPwriteFrom != 0) && (m.pwriteCalls >= m.failPwriteFrom) &&
		((m.failPwriteCount == 0) || (m.failPwriteHits < m.failPwriteCount))) {
		m.failPwriteHits++;
		if (m.landHalfOnFailure != 0) {
			/* Landed but never acknowledged: the caller must resend it. */
			memcpy(&m.file[offset], buf, count / 2);
			if ((offset + count / 2) > m.size) {
				m.size = offset + count / 2;
			}
		}
		return timeOut(nfs);
	}

	size_t n = count;
	if ((m.shortEvery != 0) && ((m.pwriteCalls % m.shortEvery) == 0) && (count > 1)) {
		n = count / 2;
	}
	memcpy(&m.file[offset], buf, n);
	memset(&m.acked[offset], 1, n);
	if ((offset + n) > m.size) {
		m.size = offset + n;
	}
	return (int)n;
}


int nfs_pread(struct nfs_context *nfs, struct nfsfh *nfsfh, void *buf, size_t count, uint64_t offset)
{
	useFh(nfs, nfsfh);
	if (count > m.maxRead) {
		m.maxRead = count;
	}
	if (nfs->dead != 0) {
		return -ETIMEDOUT;
	}
	if (offset >= m.size) {
		return 0;
	}
	size_t n = ((offset + count) > m.size) ? (size_t)(m.size - offset) : count;
	memcpy(buf, &m.file[offset], n);
	return (int)n;
}


int nfs_lstat64(struct nfs_context *nfs, const char *path, struct nfs_stat_64 *st)
{
	(void)path;
	useCtx(nfs);
	if (nfs->dead != 0) {
		return -ETIMEDOUT;
	}
	memset(st, 0, sizeof(*st));
	st->nfs_mode = 0100644;
	st->nfs_size = m.size;
	st->nfs_ino = 42;
	return 0;
}


/* Unused by these cases; present so the real nfs_ops.c links. */
int nfs_renew(struct nfs_context *nfs) { useCtx(nfs); return 0; }
int nfs_readlink(struct nfs_context *nfs, const char *path, char *buf, int bufsize) { (void)nfs; (void)path; (void)buf; (void)bufsize; return -EIO; }
int nfs_truncate(struct nfs_context *nfs, const char *path, uint64_t length) { (void)nfs; (void)path; (void)length; return -EIO; }
int nfs_ftruncate(struct nfs_context *nfs, struct nfsfh *fh, uint64_t length) { (void)nfs; (void)fh; (void)length; return -EIO; }
int nfs_chmod(struct nfs_context *nfs, const char *path, int mode) { (void)nfs; (void)path; (void)mode; return -EIO; }
int nfs_utimes(struct nfs_context *nfs, const char *path, struct timeval *times) { (void)nfs; (void)path; (void)times; return -EIO; }
int nfs_creat(struct nfs_context *nfs, const char *path, int mode, struct nfsfh **fh) { (void)nfs; (void)path; (void)mode; (void)fh; return -EIO; }
int nfs_mkdir2(struct nfs_context *nfs, const char *path, int mode) { (void)nfs; (void)path; (void)mode; return -EIO; }
int nfs_symlink(struct nfs_context *nfs, const char *target, const char *linkname) { (void)nfs; (void)target; (void)linkname; return -EIO; }
int nfs_rmdir(struct nfs_context *nfs, const char *path) { (void)nfs; (void)path; return -EIO; }
int nfs_unlink(struct nfs_context *nfs, const char *path) { (void)nfs; (void)path; return -EIO; }
int nfs_link(struct nfs_context *nfs, const char *oldpath, const char *newpath) { (void)nfs; (void)oldpath; (void)newpath; return -EIO; }
int nfs_opendir(struct nfs_context *nfs, const char *path, struct nfsdir **dir) { (void)nfs; (void)path; (void)dir; return -EIO; }
struct nfsdirent *nfs_readdir(struct nfs_context *nfs, struct nfsdir *dir) { (void)nfs; (void)dir; return NULL; }
void nfs_closedir(struct nfs_context *nfs, struct nfsdir *dir) { (void)nfs; (void)dir; }
int nfs_statvfs64(struct nfs_context *nfs, const char *path, struct nfs_statvfs_64 *svfs) { (void)nfs; (void)path; (void)svfs; return -EIO; }

void _phoenix_initAttrsStruct(struct _attrAll *attrs, int err)
{
	(void)err;
	memset(attrs, 0, sizeof(*attrs));
}


/* ----------------------------------------------------------------------------
 * Cases
 * ------------------------------------------------------------------------- */

static nfs_fs_t fs;
static oid_t fileOid;
static int failedCases;


static void fsSetup(void)
{
	mockReset();
	memset(&fs, 0, sizeof(fs));
	fs.nfs = newCtx();
	fs.server = "mock";
	fs.export = "/";
	fs.version = 4;
	fs.port = 7;
	if (nfs_node_init(&fs.nodes) != 0) {
		abort();
	}
	nfs_node_t *n = nfs_node_get(&fs.nodes, "/file");
	n->type = otFile;
	fileOid.port = fs.port;
	fileOid.id = n->id;
}


static void pattern(unsigned char *buf, size_t len, unsigned seed)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = (unsigned char)((i * 2654435761u + seed) >> 13);
	}
}


static void verdict(const char *name, int ok, const char *why)
{
	printf("%-62s %s%s%s\n", name, ok ? "PASS" : "FAIL", ok ? "" : " -- ", ok ? "" : why);
	if (!ok) {
		failedCases++;
	}
}


/* Common post-conditions of a write case. */
static int checkWrite(const char *name, int rc, int expectRc, const unsigned char *buf, uint64_t off, size_t contentLen)
{
	char why[200];

	if (rc != expectRc) {
		snprintf(why, sizeof(why), "returned %d, expected %d", rc, expectRc);
	}
	else if (m.maxWrite > WMAX) {
		snprintf(why, sizeof(why), "a WRITE RPC carried %zu bytes (max %u)", m.maxWrite, WMAX);
	}
	else if (m.rewroteAcked != 0) {
		snprintf(why, sizeof(why), "re-sent bytes the server had acknowledged");
	}
	else if (m.staleUse != 0) {
		snprintf(why, sizeof(why), "%d use(s) of a handle/context after it was destroyed", m.staleUse);
	}
	else if ((contentLen > 0) && (memcmp(&m.file[off], buf, contentLen) != 0)) {
		snprintf(why, sizeof(why), "file content differs from the buffer");
	}
	else {
		verdict(name, 1, "");
		return 1;
	}
	verdict(name, 0, why);
	return 0;
}


int main(void)
{
	/* 1 MiB + 13 at an odd offset: 17 WRITE RPCs at WMAX = 64 KiB */
	const size_t len = (1u << 20) + 13;
	const uint64_t off = 777;
	unsigned char *buf = malloc(len);
	int rc;

	pattern(buf, len, 1);

	fsSetup();
	rc = nfs_ops_write(&fs, &fileOid, (off_t)off, buf, len);
	checkWrite("write: large buffer goes out in RPC-sized chunks", rc, (int)len, buf, off, len);

	fsSetup();
	m.failPwriteFrom = 5;
	m.failPwriteCount = 1;
	m.landHalfOnFailure = 1;
	rc = nfs_ops_write(&fs, &fileOid, (off_t)off, buf, len);
	if (checkWrite("write: timeout mid-way -> reclaim, resume at the right offset", rc, (int)len, buf, off, len)) {
		verdict("write: ... and exactly one re-mount", m.mounts == 1, "re-mount count != 1");
	}

	fsSetup();
	m.shortEvery = 3;
	rc = nfs_ops_write(&fs, &fileOid, (off_t)off, buf, len);
	checkWrite("write: short WRITE replies are continued, not lost", rc, (int)len, buf, off, len);

	fsSetup();
	m.failPwriteFrom = 4; /* chunks 1-3 land, everything after times out */
	rc = nfs_ops_write(&fs, &fileOid, (off_t)off, buf, len);
	if (checkWrite("write: persistent failure after progress -> short count", rc, (int)(3 * WMAX), buf, off, 3 * WMAX)) {
		verdict("write: ... after spending the reclaim budget (2)", m.mounts == 2, "re-mount count != 2");
	}

	fsSetup();
	m.failPwriteFrom = 1;
	m.mountFails = 1;
	rc = nfs_ops_write(&fs, &fileOid, (off_t)off, buf, len);
	checkWrite("write: no progress and reclaim impossible -> the error", rc, -ETIMEDOUT, buf, off, 0);

	fsSetup();
	rc = nfs_ops_open(&fs, &fileOid); /* the node now caches an fh */
	m.failPwriteFrom = 2;
	m.failPwriteCount = 1;
	rc = (rc == 0) ? nfs_ops_write(&fs, &fileOid, (off_t)off, buf, len) : rc;
	checkWrite("write: cached fh of an open file is not reused after reclaim", rc, (int)len, buf, off, len);
	(void)nfs_ops_close(&fs, &fileOid);

	fsSetup();
	m.openTimeouts = 1;
	rc = nfs_ops_open(&fs, &fileOid);
	verdict("open: a timed-out open reclaims instead of retrying on the dead context",
		(rc == 0) && (m.opensOnDeadCtx == 0) && (m.mounts == 1) && (m.staleUse == 0),
		"retried on the dead context, or did not reclaim");
	(void)nfs_ops_close(&fs, &fileOid);

	fsSetup();
	(void)nfs_ops_write(&fs, &fileOid, 0, buf, len);
	{
		unsigned char *rb = malloc(len);
		rc = nfs_ops_read(&fs, &fileOid, 0, rb, len);
		verdict("read: one READ RPC at most the negotiated maximum",
			(rc > 0) && ((size_t)rc <= RMAX) && (m.maxRead <= RMAX) && (memcmp(rb, buf, (size_t)rc) == 0),
			"READ larger than readmax, or wrong data");
		free(rb);
	}

	fsSetup();
	nfs_ops_recover(&fs);
	{
		int liveUntouched = (m.mounts == 0);
		struct nfs_context *old = fs.nfs;
		old->dead = 1;
		nfs_ops_recover(&fs);
		verdict("recover: live context left alone, abandoned one rebuilt",
			liveUntouched && (m.mounts == 1) && (fs.nfs != old) && (old->destroyed != 0) && (fs.nfs->dead == 0),
			"recover did not behave");
	}

	free(buf);
	if (failedCases == 0) {
		printf("NFS-OPS-IO: PASS\n");
		return 0;
	}
	printf("NFS-OPS-IO: FAIL (%d case(s))\n", failedCases);
	return 1;
}
