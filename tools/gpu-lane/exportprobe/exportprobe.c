/*
 * exportprobe -- two-process check of the kernel memory export (memExport/memUnexport)
 *
 * Experiment E1 of the new GPU lane (docs/gpu-new-lane/E1-vm-object-export.md).
 *
 * The process forks before any buffer exists. The child is the EXPORTER: it owns a port,
 * serves a tiny namespace "/exportprobe/<id>" on it, allocates three MAP_CONTIGUOUS buffers
 * (A cached, B uncached, C cached) and exports them. The parent is the IMPORTER: it opens
 * "/exportprobe/<id>", maps the descriptors and checks, against the exporter:
 *
 *   same_pa        both sides resolve every page to the same physical address
 *   xwrite_*       writes cross in both directions, cached and uncached
 *   memtype        a mapping with the other memory type, or past the end, is refused
 *   refuse_*       exporting anonymous (non-contiguous) memory, under a foreign port, twice
 *                  under one oid, or past the mapping, is refused
 *   scm_rights     a descriptor passed over AF_UNIX maps the same pages
 *   fork_shared    a fork()ed child's write to exported memory is seen by the parent
 *   survives_*     the importer's pages outlive the exporter's munmap+unexport, and the
 *                  exporter's exit (withdrawn by port release, not by memUnexport)
 *   reimport_refused  after unexport, mmap() of a still-open descriptor fails
 *   released_*     the pages return to the free pool when the importer unmaps last
 *   held_b         ... and do NOT while the exporter still maps them
 *
 * Every result is a tagged line: "EXPORTPROBE <key>=<0|1> ..." and one final
 * "EXPORTPROBE RESULT ... verdict=PASS|FAIL".
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* libphoenix tags sendmsg()/recvmsg() "not fully supported"; the single-iovec SCM_RIGHTS use
 * here goes straight to the kernel's fdpass code, which is what this probe exercises */
#pragma GCC diagnostic ignored "-Wattribute-warning"


#define NAME    "/exportprobe"
#define SZ      (1024u * 1024u) /* a power of two, so the contiguous block is exactly SZ */
#define PAGE    4096u
#define NBUF    3u
#define WORDS   (SZ / sizeof(uint32_t))
#define DELTA_OK(d) (((d) >= (SZ * 3u) / 4u) && ((d) <= 2u * SZ))

enum { bufA = 0, bufB, bufC }; /* A cached, B uncached, C cached (exporter-exit test) */

enum { cmdHello = 1, cmdExported, cmdImported, cmdExpWrote, cmdChecked, cmdUnexported, cmdBye };


typedef struct {
	uint32_t cmd;
	uint32_t port;
	uint64_t pa[NBUF];
	int32_t val;
} ctl_t;


static const uint32_t seed[NBUF] = { 0xa0000000u, 0xb0000000u, 0xc0000000u };
static const int uncached[NBUF] = { 0, 1, 0 };
static int failures;


static void result(const char *who, const char *key, int ok)
{
	printf("EXPORTPROBE %s %s=%d\n", who, key, ok ? 1 : 0);
	if (!ok) {
		failures++;
	}
}


/* ---- control channel (AF_UNIX socketpair, optionally carrying one descriptor) ---- */

static int ctlSend(int s, const ctl_t *c, int fd)
{
	struct msghdr mh;
	struct iovec iov;
	union {
		struct cmsghdr h;
		char buf[CMSG_SPACE(sizeof(int))];
	} cm;
	struct cmsghdr *h;

	memset(&mh, 0, sizeof(mh));
	iov.iov_base = (void *)c;
	iov.iov_len = sizeof(*c);
	mh.msg_iov = &iov;
	mh.msg_iovlen = 1;

	if (fd >= 0) {
		memset(&cm, 0, sizeof(cm));
		mh.msg_control = cm.buf;
		mh.msg_controllen = sizeof(cm.buf);
		h = CMSG_FIRSTHDR(&mh);
		h->cmsg_level = SOL_SOCKET;
		h->cmsg_type = SCM_RIGHTS;
		h->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(h), &fd, sizeof(int));
	}

	return (sendmsg(s, &mh, 0) == (ssize_t)sizeof(*c)) ? 0 : -1;
}


static int ctlRecv(int s, ctl_t *c, uint32_t want, int *fd)
{
	struct msghdr mh;
	struct iovec iov;
	union {
		struct cmsghdr h;
		char buf[CMSG_SPACE(sizeof(int))];
	} cm;
	struct cmsghdr *h;
	ssize_t n;

	memset(&mh, 0, sizeof(mh));
	memset(&cm, 0, sizeof(cm));
	iov.iov_base = c;
	iov.iov_len = sizeof(*c);
	mh.msg_iov = &iov;
	mh.msg_iovlen = 1;
	mh.msg_control = cm.buf;
	mh.msg_controllen = sizeof(cm.buf);

	do {
		n = recvmsg(s, &mh, 0);
	} while ((n < 0) && (errno == EINTR));

	if (n != (ssize_t)sizeof(*c) || c->cmd != want) {
		printf("EXPORTPROBE protocol error: got n=%d cmd=%u, wanted cmd=%u\n", (int)n, (n > 0) ? c->cmd : 0u, want);
		return -1;
	}

	if (fd != NULL) {
		*fd = -1;
		h = CMSG_FIRSTHDR(&mh);
		if ((h != NULL) && (h->cmsg_level == SOL_SOCKET) && (h->cmsg_type == SCM_RIGHTS)) {
			memcpy(fd, CMSG_DATA(h), sizeof(int));
		}
	}

	return 0;
}


/* ---- helpers ---- */

static unsigned int freeBytes(void)
{
	meminfo_t mi;

	memset(&mi, 0, sizeof(mi));
	mi.page.mapsz = -1;
	mi.entry.mapsz = -1;
	mi.entry.kmapsz = -1;
	mi.maps.mapsz = -1;
	meminfo(&mi);

	return mi.page.free;
}


/* 1 if every page of [p, p + SZ) resolves to pa + offset */
static int samePa(void *p, uint64_t pa)
{
	unsigned int off;

	for (off = 0; off < SZ; off += PAGE) {
		if ((uint64_t)va2pa((char *)p + off) != pa + off) {
			return 0;
		}
	}

	return 1;
}


static void fill(volatile uint32_t *w, unsigned int from, unsigned int to, uint32_t base)
{
	unsigned int i;

	for (i = from; i < to; i++) {
		w[i] = base ^ i;
	}
}


static int check(volatile uint32_t *w, unsigned int from, unsigned int to, uint32_t base)
{
	unsigned int i;

	for (i = from; i < to; i++) {
		if (w[i] != (base ^ i)) {
			printf("EXPORTPROBE mismatch at word %u: 0x%08x != 0x%08x\n", i, (unsigned int)w[i], (unsigned int)(base ^ i));
			return 0;
		}
	}

	return 1;
}


/* Final contents after the protocol: [0, W/4) exporter's second write, [W/4, W/2) exporter's
 * first write, [W/2, W) importer's write */
static int checkFinal(volatile uint32_t *w, unsigned int b)
{
	return check(w, 0, WORDS / 4u, seed[b] ^ 0x00e00000u) &&
		check(w, WORDS / 4u, WORDS / 2u, seed[b]) &&
		check(w, WORDS / 2u, WORDS, ~seed[b]);
}


static void *mapFd(int fd, int flags, size_t size, off_t offs)
{
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, fd, offs);

	return (p == MAP_FAILED) ? NULL : p;
}


/* ---- exporter: namespace server ---- */

static struct {
	uint32_t port;
} exp_common;


static void *exporterServe(void *arg)
{
	msg_t msg;
	msg_rid_t rid;
	char name[16];
	unsigned long id;
	char *end;
	size_t len;

	(void)arg;

	for (;;) {
		if (msgRecv(exp_common.port, &msg, &rid) < 0) {
			continue;
		}

		switch (msg.type) {
			case mtLookup:
				/* "<id>" relative to NAME; ids 1..NBUF */
				len = (msg.i.data != NULL) ? strnlen(msg.i.data, msg.i.size) : 0u;
				if ((len == 0u) || (len >= sizeof(name))) {
					msg.o.err = -ENOENT;
					break;
				}
				memcpy(name, msg.i.data, len);
				name[len] = '\0';
				id = strtoul(name, &end, 10);
				if ((*end != '\0') || (id < 1u) || (id > NBUF)) {
					msg.o.err = -ENOENT;
					break;
				}
				msg.o.lookup.fil.port = exp_common.port;
				msg.o.lookup.fil.id = id;
				msg.o.lookup.dev = msg.o.lookup.fil;
				msg.o.err = (int)len;
				break;

			case mtGetAttr:
				if (msg.i.attr.type == atMode) {
					msg.o.attr.val = (msg.oid.id == 0u) ? (S_IFDIR | 0555) : (S_IFCHR | 0666);
					msg.o.err = 0;
				}
				else if (msg.i.attr.type == atType) {
					msg.o.attr.val = (msg.oid.id == 0u) ? otDir : otDev;
					msg.o.err = 0;
				}
				else {
					/* In particular atSize: the kernel asks for it only when mmap() finds no
					 * live export under the oid. Refusing it means such an mmap() fails
					 * instead of creating a file-backed object that would shadow the name. */
					msg.o.err = -ENOENT;
				}
				break;

			case mtOpen:
			case mtClose:
				/* 0, not an id: a positive mtOpen reply would make the descriptor's oid a
				 * multiplexer id (posix_open) */
				msg.o.err = 0;
				break;

			default:
				msg.o.err = -ENOSYS;
				break;
		}

		msgRespond(exp_common.port, &msg, rid);
	}

	return NULL;
}


static int exporter(int s)
{
	static const char who[] = "exp";
	volatile uint32_t *buf[NBUF], *anon;
	uint64_t pa[NBUF];
	uint32_t foreign;
	pthread_t thr;
	oid_t oid, dir;
	ctl_t c;
	void *p;
	int fd, ok, b;

	if (ctlRecv(s, &c, cmdHello, NULL) < 0) {
		return 1;
	}
	foreign = c.port; /* a port the IMPORTER owns */

	if (portCreate(&exp_common.port) < 0) {
		printf("EXPORTPROBE exp portCreate failed\n");
		return 1;
	}
	dir.port = exp_common.port;
	dir.id = 0;
	if (portRegister(exp_common.port, NAME, &dir) < 0) {
		printf("EXPORTPROBE exp portRegister(%s) failed\n", NAME);
		return 1;
	}
	if (pthread_create(&thr, NULL, exporterServe, NULL) != 0) {
		printf("EXPORTPROBE exp pthread_create failed\n");
		return 1;
	}

	/* A kernel without the facility rejects the (out-of-range) syscall number with -EINVAL,
	 * which would make every "refused with EINVAL" check below pass for the wrong reason.
	 * Only the real facility answers -ENOENT for an oid with nothing exported under it. */
	oid.port = exp_common.port;
	oid.id = 7;
	ok = (memUnexport(&oid) == -ENOENT);
	result(who, "kernel_has_export", ok);
	if (!ok) {
		return 1;
	}

	for (b = 0; b < (int)NBUF; b++) {
		p = mmap(NULL, SZ, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_CONTIGUOUS | (uncached[b] ? MAP_UNCACHED : 0), -1, 0);
		if (p == MAP_FAILED) {
			printf("EXPORTPROBE exp mmap contiguous %u failed errno=%d\n", SZ, errno);
			return 1;
		}
		buf[b] = p;
		pa[b] = va2pa(p);
		fill(buf[b], 0, WORDS, seed[b]);
	}

	/* Refusals, before anything is exported */
	oid.port = exp_common.port;
	oid.id = 1;
	p = mmap(NULL, SZ, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
	anon = (p == MAP_FAILED) ? NULL : p;
	if (anon != NULL) {
		anon[0] = 1; /* populate */
	}
	result(who, "refuse_anon", (anon != NULL) && (memExport(&oid, (void *)anon, SZ) == -EINVAL));
	result(who, "refuse_past_mapping", memExport(&oid, (void *)buf[bufA], SZ + PAGE) == -EINVAL);
	result(who, "refuse_unaligned", memExport(&oid, (char *)buf[bufA] + 64, PAGE) == -EINVAL);
	oid.port = foreign;
	result(who, "refuse_foreign_port", memExport(&oid, (void *)buf[bufA], SZ) == -EPERM);

	/* The exports: ids 1..3 */
	ok = 1;
	for (b = 0; b < (int)NBUF; b++) {
		oid.port = exp_common.port;
		oid.id = (id_t)(b + 1);
		if (memExport(&oid, (void *)buf[b], SZ) != 0) {
			printf("EXPORTPROBE exp memExport id=%d failed\n", b + 1);
			ok = 0;
		}
	}
	result(who, "export", ok);
	oid.id = 1;
	result(who, "refuse_duplicate", memExport(&oid, (void *)buf[bufA], SZ) == -EEXIST);

	memset(&c, 0, sizeof(c));
	c.cmd = cmdExported;
	c.port = exp_common.port;
	memcpy(c.pa, pa, sizeof(pa));
	if (ctlSend(s, &c, -1) < 0) {
		return 1;
	}

	/* Importer mapped, checked, wrote its half, and passed back its descriptor for B */
	if (ctlRecv(s, &c, cmdImported, &fd) < 0) {
		return 1;
	}
	result(who, "xwrite_imp_to_exp_cached", check(buf[bufA], WORDS / 2u, WORDS, ~seed[bufA]));
	result(who, "xwrite_imp_to_exp_uncached", check(buf[bufB], WORDS / 2u, WORDS, ~seed[bufB]));

	/* SCM_RIGHTS: the received descriptor maps the same pages, with B's memory type only */
	p = (fd >= 0) ? mapFd(fd, MAP_UNCACHED, SZ, 0) : NULL;
	result(who, "scm_rights_same_pa", (p != NULL) && samePa(p, pa[bufB]));
	result(who, "scm_rights_memtype_refused", (fd >= 0) && (mapFd(fd, 0, SZ, 0) == NULL) && (errno == EINVAL));
	if (p != NULL) {
		munmap(p, SZ);
	}
	if (fd >= 0) {
		close(fd); /* mtClose to our own port: served by exporterServe */
	}

	/* Second exporter write, after the importer mapped */
	fill(buf[bufA], 0, WORDS / 4u, seed[bufA] ^ 0x00e00000u);
	fill(buf[bufB], 0, WORDS / 4u, seed[bufB] ^ 0x00e00000u);
	fill(buf[bufC], 0, WORDS / 4u, seed[bufC] ^ 0x00e00000u);
	fill(buf[bufC], WORDS / 2u, WORDS, ~seed[bufC]); /* C is not written by the importer */
	c.cmd = cmdExpWrote;
	if ((ctlSend(s, &c, -1) < 0) || (ctlRecv(s, &c, cmdChecked, NULL) < 0)) {
		return 1;
	}

	/* A: unmap and withdraw while the importer still maps it. B: withdraw, keep mapped. */
	munmap((void *)buf[bufA], SZ);
	oid.port = exp_common.port;
	oid.id = 1;
	result(who, "unexport", memUnexport(&oid) == 0);
	result(who, "unexport_twice_enoent", memUnexport(&oid) == -ENOENT);
	oid.id = 2;
	result(who, "unexport_b", memUnexport(&oid) == 0);

	c.cmd = cmdUnexported;
	if ((ctlSend(s, &c, -1) < 0) || (ctlRecv(s, &c, cmdBye, NULL) < 0)) {
		return 1;
	}

	/* Exit with C still exported and mapped: the port release must withdraw it */
	portUnregister(NAME);
	printf("EXPORTPROBE exp exiting failures=%d\n", failures);
	return (failures == 0) ? 0 : 2;
}


/* ---- importer ---- */

static int importer(int s, pid_t child, unsigned int free0)
{
	static const char who[] = "imp";
	volatile uint32_t *map[NBUF];
	unsigned int f1, f2;
	char path[32];
	int fd[NBUF], ok, b, st, xfail;
	uint32_t port;
	oid_t oid;
	ctl_t c;
	void *p;
	pid_t gc;

	if (portCreate(&port) < 0) {
		printf("EXPORTPROBE imp portCreate failed\n");
		return 1;
	}
	memset(&c, 0, sizeof(c));
	c.cmd = cmdHello;
	c.port = port;
	if ((ctlSend(s, &c, -1) < 0) || (ctlRecv(s, &c, cmdExported, NULL) < 0)) {
		return 1;
	}

	/* Open through the exporter's namespace and map */
	ok = 1;
	for (b = 0; b < (int)NBUF; b++) {
		snprintf(path, sizeof(path), NAME "/%d", b + 1);
		fd[b] = open(path, O_RDONLY);
		map[b] = (fd[b] >= 0) ? mapFd(fd[b], uncached[b] ? MAP_UNCACHED : 0, SZ, 0) : NULL;
		if (map[b] == NULL) {
			printf("EXPORTPROBE imp open/mmap %s failed fd=%d errno=%d\n", path, fd[b], errno);
			ok = 0;
		}
	}
	result(who, "import", ok);
	if (!ok) {
		return 1;
	}

	ok = 1;
	for (b = 0; b < (int)NBUF; b++) {
		ok = ok && samePa((void *)map[b], c.pa[b]);
	}
	result(who, "same_pa", ok);
	result(who, "xwrite_exp_to_imp_cached", check(map[bufA], 0, WORDS, seed[bufA]));
	result(who, "xwrite_exp_to_imp_uncached", check(map[bufB], 0, WORDS, seed[bufB]));

	/* Memory type and range are enforced */
	result(who, "memtype_refused_cached_as_uncached", (mapFd(fd[bufA], MAP_UNCACHED, SZ, 0) == NULL) && (errno == EINVAL));
	result(who, "memtype_refused_uncached_as_cached", (mapFd(fd[bufB], 0, SZ, 0) == NULL) && (errno == EINVAL));
	result(who, "refuse_oversize", (mapFd(fd[bufA], 0, SZ + PAGE, 0) == NULL) && (errno == EINVAL));
	result(who, "refuse_offset_past_end", (mapFd(fd[bufA], 0, PAGE, SZ) == NULL) && (errno == EINVAL));
	p = mapFd(fd[bufA], 0, PAGE, SZ - PAGE);
	result(who, "partial_map_same_pa", (p != NULL) && ((uint64_t)va2pa(p) == c.pa[bufA] + SZ - PAGE));
	if (p != NULL) {
		munmap(p, PAGE);
	}
	oid.port = c.port;
	oid.id = 1;
	result(who, "refuse_foreign_unexport", memUnexport(&oid) == -EPERM);

	/* fork(): exported memory stays shared (a COW copy would hide the child's write) */
	gc = fork();
	if (gc == 0) {
		map[bufA][WORDS - 1u] = 0x5a5a5a5au;
		_exit(0);
	}
	if (gc > 0) {
		waitpid(gc, &st, 0);
	}
	result(who, "fork_shared", (gc > 0) && (map[bufA][WORDS - 1u] == 0x5a5a5a5au));

	/* Importer's write: second half of A and B */
	fill(map[bufA], WORDS / 2u, WORDS, ~seed[bufA]);
	fill(map[bufB], WORDS / 2u, WORDS, ~seed[bufB]);
	c.cmd = cmdImported;
	if (ctlSend(s, &c, fd[bufB]) < 0) {
		return 1;
	}

	if (ctlRecv(s, &c, cmdExpWrote, NULL) < 0) {
		return 1;
	}
	xfail = !check(map[bufA], 0, WORDS / 4u, seed[bufA] ^ 0x00e00000u);
	result(who, "xwrite_exp_to_imp_after_map_cached", !xfail);
	result(who, "xwrite_exp_to_imp_after_map_uncached", check(map[bufB], 0, WORDS / 4u, seed[bufB] ^ 0x00e00000u));
	c.cmd = cmdChecked;
	if ((ctlSend(s, &c, -1) < 0) || (ctlRecv(s, &c, cmdUnexported, NULL) < 0)) {
		return 1;
	}

	/* A: the exporter has unmapped and withdrawn it; our mapping still holds the pages */
	result(who, "survives_unexport", checkFinal(map[bufA], bufA) && samePa((void *)map[bufA], c.pa[bufA]));
	result(who, "reimport_refused", mapFd(fd[bufA], 0, SZ, 0) == NULL);

	f1 = freeBytes();
	munmap((void *)map[bufA], SZ);
	f2 = freeBytes();
	printf("EXPORTPROBE imp free_delta_a=%u (expect ~%u)\n", f2 - f1, SZ);
	result(who, "released_a", DELTA_OK(f2 - f1));

	/* B: withdrawn, but the exporter still maps it -- unmapping here must NOT free it */
	result(who, "b_final", checkFinal(map[bufB], bufB));
	f1 = freeBytes();
	munmap((void *)map[bufB], SZ);
	f2 = freeBytes();
	printf("EXPORTPROBE imp free_delta_b=%d (expect ~0)\n", (int)(f2 - f1));
	result(who, "held_b", ((int)(f2 - f1) < (int)(SZ / 4u)) && ((int)(f2 - f1) > -(int)(SZ / 4u)));

	for (b = 0; b < (int)NBUF; b++) {
		close(fd[b]);
	}
	c.cmd = cmdBye;
	if (ctlSend(s, &c, -1) < 0) {
		return 1;
	}

	/* C: the exporter exits with C still exported; its port release withdraws the export,
	 * our mapping keeps the pages until we unmap */
	waitpid(child, &st, 0);
	result(who, "exporter_exit_ok", WIFEXITED(st) && (WEXITSTATUS(st) == 0));
	result(who, "survives_exporter_exit", check(map[bufC], 0, WORDS / 4u, seed[bufC] ^ 0x00e00000u) &&
		check(map[bufC], WORDS / 4u, WORDS / 2u, seed[bufC]) && check(map[bufC], WORDS / 2u, WORDS, ~seed[bufC]) &&
		samePa((void *)map[bufC], c.pa[bufC]));
	f1 = freeBytes();
	munmap((void *)map[bufC], SZ);
	f2 = freeBytes();
	printf("EXPORTPROBE imp free_delta_c=%u (expect ~%u)\n", f2 - f1, SZ);
	result(who, "released_c", DELTA_OK(f2 - f1));

	portDestroy(port);
	printf("EXPORTPROBE imp free_total_delta=%d (informational: fork/exit noise)\n", (int)(freeBytes() - free0));
	return 0;
}


int main(void)
{
	unsigned int free0;
	int sv[2], rc;
	pid_t pid;

	setvbuf(stdout, NULL, _IOLBF, 0);

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
		printf("EXPORTPROBE socketpair failed errno=%d\n", errno);
		return 1;
	}

	free0 = freeBytes();

	/* Fork before any buffer exists: the exporter's buffers must never be COW-shared */
	pid = fork();
	if (pid < 0) {
		printf("EXPORTPROBE fork failed errno=%d\n", errno);
		return 1;
	}
	if (pid == 0) {
		close(sv[0]);
		exit(exporter(sv[1]));
	}

	close(sv[1]);
	rc = importer(sv[0], pid, free0);

	printf("EXPORTPROBE RESULT failures=%d verdict=%s\n", failures, ((rc == 0) && (failures == 0)) ? "PASS" : "FAIL");
	return ((rc == 0) && (failures == 0)) ? 0 : 1;
}
