/*
 * Phoenix-RTOS Raspberry Pi 4 — IPC / microkernel-core stress util (task #39).
 *
 * The highest-value suite: it hammers the Phoenix message-passing core
 * (portCreate / msgSend / msgRecv / msgRespond / lookup), which has a history of
 * mailbox-serialization races. Three tests, each heartbeated:
 *
 *   stress-ipc echo   [N]   In-process echo server: one server thread creates a
 *                           port and msgRecv/msgRespond-echoes; N client threads
 *                           each msgSend many round-trips CONCURRENTLY through
 *                           that one port. Each request carries a unique nonce in
 *                           msg.i.raw; the reply must echo it back byte-for-byte
 *                           (a mismatched/lost reply = FAULT — a crossed or
 *                           dropped message in the kernel msg path).
 *
 *   stress-ipc lookup [N]   Resolution storm: N concurrent lookup() of named
 *                           ports / file oids (/dev/vcmbox, /dev, "devfs", ...),
 *                           repeated, to stress the namespace resolver under
 *                           contention. A name that resolves once but then
 *                           intermittently fails (without the node going away) is
 *                           a FAULT; a never-present optional name is just noted.
 *
 *   stress-ipc vcmbox [N]   Shared-server serialization: N client threads issue
 *                           VideoCore property requests CONCURRENTLY to the real
 *                           /dev/vcmbox server (the single hardware mailbox FIFO,
 *                           which the server must serialize). Each thread reads
 *                           GET_FIRMWARE_REV (a constant) and GET_TEMPERATURE (a
 *                           plausible range) — a garbled / cross-talked reply is a
 *                           FAULT, directly exercising the mailbox-serialization
 *                           fix. If /dev/vcmbox is absent, falls back to concurrent
 *                           /dev/thermal reads (which route through vcmbox).
 *
 *   stress-ipc all          echo + lookup + vcmbox at default intensities.
 *
 * The Phoenix msg API is used directly (sys/msg.h) — it links cleanly into a
 * standalone static util (no posixsrv dependency for portCreate/msgSend/lookup).
 * The vcmbox wire format (an mtDevCtl carrying a packed request in msg.i.raw) is
 * replicated inline from the read-only libvcmbox so this util has no build-time
 * dependency on the devices tree.
 *
 * Generous (>=16 KB) thread stacks — the Pi4 fs/pool-thread stack-overflow
 * history (#120/#152).
 *
 * Host-side build only (build-stress-pfi.sh). Static aarch64-phoenix.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/msg.h>
#include <sys/threads.h>
#include <sys/types.h>

#include "stress.h"

#define SUITE "ipc"

#define DEFAULT_ECHO_THR   8
#define DEFAULT_LOOKUP_THR 8
#define DEFAULT_VCMBOX_THR 8

/* Round-trips each client thread performs, per test. */
#define ECHO_ROUNDS   500
#define LOOKUP_ROUNDS 200
#define VCMBOX_ROUNDS 64

/* >=16 KB floor per #120/#152; 32 KB gives the msg path comfortable headroom. */
#define IPC_STACKSZ (32u * 1024u)
#define MAX_THR     16

/* Private message types for the in-process echo server. Past mtStat so they can
 * never collide with a standard filesystem/object message the kernel routes.
 * MT_ECHO_STOP is the shutdown sentinel: main sends exactly one after every
 * client has joined, the server responds and breaks — deterministic teardown
 * with no message counting (a count-based shutdown races msgRespond vs. the
 * counter increment and can leave a filler msgSend blocked on a dead port). */
#define MT_ECHO      0x5300
#define MT_ECHO_STOP 0x5301

/* VideoCore property tags (BCM2711 firmware mailbox), from the thermal driver. */
#define VC_PROP_GET_FIRMWARE_REV 0x00000001u
#define VC_PROP_GET_TEMPERATURE  0x00030006u

/* vcmbox wire format — replicated from libvcmbox.h (read-only). A property call
 * is an mtDevCtl whose request/response live entirely in msg.i.raw / msg.o.raw. */
#define VCMBOX_MAX_WORDS 12u
typedef struct {
	uint32_t tag;
	uint32_t valBufSize;
	uint32_t nIn;
	uint32_t in[VCMBOX_MAX_WORDS];
} vcmbox_req_t;
typedef struct {
	int err;
	uint32_t nOut;
	uint32_t out[VCMBOX_MAX_WORDS];
} vcmbox_resp_t;


/* ============================ echo test ============================ */

typedef struct {
	uint32_t port;       /* server port, shared by all clients */
	int idx;             /* client index, mixed into the nonce */
	int rounds;
	volatile int rc;     /* 0=ok 1=fault 2=limit, set before exit */
	volatile int done;   /* round-trips completed */
	char detail[160];
} echo_arg_t;

/* The echo server thread: msgRecv on its port, copy i.raw->o.raw verbatim for
 * MT_ECHO, and msgRespond. It runs until it receives the MT_ECHO_STOP sentinel,
 * which it responds to and then exits — so teardown is deterministic and the
 * server is always blocked in msgRecv while any client could be sending. */
typedef struct {
	uint32_t port;
	volatile long served;
	volatile int up;
} echo_srv_t;

static void echo_server(void *arg)
{
	echo_srv_t *s = arg;
	msg_t msg;
	msg_rid_t rid;
	int err;

	s->up = 1;
	for (;;) {
		err = msgRecv(s->port, &msg, &rid);
		if (err < 0) {
			if (err == -EINTR)
				continue;
			break; /* port closed / fatal */
		}
		if (msg.type == MT_ECHO_STOP) {
			/* Acknowledge the shutdown, then exit the loop. Only main sends this,
			 * and only after every client has joined, so nothing else is in
			 * flight. */
			msg.o.err = EOK;
			msgRespond(s->port, &msg, rid);
			break;
		}
		/* Echo the request payload back into the response region. For any other
		 * type, respond with an error so a stray message is visible, not hung. */
		if (msg.type == MT_ECHO) {
			memcpy(msg.o.raw, msg.i.raw, sizeof(msg.o.raw));
			msg.o.err = EOK;
		}
		else {
			msg.o.err = -ENOSYS;
		}
		msgRespond(s->port, &msg, rid);
		s->served++;
	}
	endthread();
}

static void echo_client(void *arg)
{
	echo_arg_t *a = arg;
	int i;

	a->rc = 0;
	a->done = 0;
	a->detail[0] = '\0';

	for (i = 0; i < a->rounds; i++) {
		msg_t msg;
		uint32_t nonce[4];
		int err;

		/* Unique, self-describing nonce: (idx, round, magic, idx^round). A reply
		 * carrying anyone else's nonce = a crossed message in the msg path. */
		nonce[0] = (uint32_t)a->idx;
		nonce[1] = (uint32_t)i;
		nonce[2] = 0xA5A5C3C3u;
		nonce[3] = (uint32_t)a->idx ^ (uint32_t)i ^ 0x5A5A3C3Cu;

		memset(&msg, 0, sizeof(msg));
		msg.type = MT_ECHO;
		memcpy(msg.i.raw, nonce, sizeof(nonce));

		err = msgSend(a->port, &msg);
		if (err < 0) {
			if (err == -EAGAIN || err == -ENOMEM) {
				a->rc = 2;
				snprintf(a->detail, sizeof(a->detail), "thr=%d msgSend limit err=%d round=%d", a->idx, err, i);
			}
			else {
				a->rc = 1;
				snprintf(a->detail, sizeof(a->detail), "thr=%d msgSend err=%d round=%d", a->idx, err, i);
			}
			endthread();
		}
		if (msg.o.err != EOK) {
			a->rc = 1;
			snprintf(a->detail, sizeof(a->detail), "thr=%d server o.err=%d round=%d", a->idx, msg.o.err, i);
			endthread();
		}
		if (memcmp(msg.o.raw, nonce, sizeof(nonce)) != 0) {
			uint32_t got[4];
			memcpy(got, msg.o.raw, sizeof(got));
			a->rc = 1;
			snprintf(a->detail, sizeof(a->detail),
				"thr=%d round=%d CROSSED reply: sent[%u,%u] got[%u,%u] (lost/crossed msg)",
				a->idx, i, nonce[0], nonce[1], got[0], got[1]);
			endthread();
		}
		a->done++;
	}
	endthread();
}

static void run_echo(sr_tally_t *t, int nthr)
{
	static echo_arg_t cargs[MAX_THR];
	static echo_srv_t srv;
	void *cstacks[MAX_THR] = { 0 };
	void *sstack = NULL;
	handle_t ctids[MAX_THR];
	handle_t stid;
	int i, started = 0, faults = 0, limits = 0;
	long total_done = 0;

	if (nthr > MAX_THR) nthr = MAX_THR;
	if (nthr < 1) nthr = 1;

	if (portCreate(&srv.port) != EOK) {
		SR_FAULT(t, SUITE, "echo", "portCreate failed errno=%d (%s)", errno, strerror(errno));
		return;
	}
	srv.served = 0;
	srv.up = 0;

	SR_HB(SUITE, "echo port=%u threads=%d rounds=%d", srv.port, nthr, ECHO_ROUNDS);

	/* Start the server thread first and let it reach msgRecv. */
	sstack = malloc(IPC_STACKSZ);
	if (sstack == NULL) {
		SR_FAULT(t, SUITE, "echo", "cannot allocate server stack");
		portDestroy(srv.port);
		return;
	}
	if (beginthreadex(echo_server, 3, sstack, IPC_STACKSZ, &srv, &stid) < 0) {
		SR_FAULT(t, SUITE, "echo", "beginthreadex(server) failed errno=%d (%s)", errno, strerror(errno));
		free(sstack);
		portDestroy(srv.port);
		return;
	}
	for (i = 0; i < 100 && srv.up == 0; i++)
		usleep(10 * 1000);

	/* Launch the client threads concurrently. */
	for (i = 0; i < nthr; i++) {
		cargs[i].port = srv.port;
		cargs[i].idx = i;
		cargs[i].rounds = ECHO_ROUNDS;
		cargs[i].rc = -1;
		cargs[i].done = 0;
		cstacks[i] = malloc(IPC_STACKSZ);
		if (cstacks[i] == NULL) {
			SR_LIMIT(t, SUITE, "echo", "cannot allocate client stack thr=%d (started=%d)", i, started);
			break;
		}
		if (beginthreadex(echo_client, 4, cstacks[i], IPC_STACKSZ, &cargs[i], &ctids[i]) < 0) {
			SR_LIMIT(t, SUITE, "echo", "beginthreadex(client) failed thr=%d (started=%d)", i, started);
			free(cstacks[i]);
			cstacks[i] = NULL;
			break;
		}
		started++;
	}

	SR_HB(SUITE, "echo clients started=%d", started);

	/* Join clients. */
	for (i = 0; i < started; i++) {
		(void)threadJoin(ctids[i], 0);
		total_done += cargs[i].done;
		if (cargs[i].rc == 1) {
			faults++;
			SR_FAULT(t, SUITE, "echo", "%s", cargs[i].detail);
		}
		else if (cargs[i].rc == 2) {
			limits++;
			SR_LIMIT(t, SUITE, "echo", "%s", cargs[i].detail);
		}
		free(cstacks[i]);
	}

	/* All clients have joined, so nothing else is sending. The server is blocked
	 * in msgRecv; send the single STOP sentinel to make it respond + exit, then
	 * join it. Deterministic — no message counting, no off-by-one race. */
	{
		msg_t m;
		memset(&m, 0, sizeof(m));
		m.type = MT_ECHO_STOP;
		(void)msgSend(srv.port, &m);
	}
	(void)threadJoin(stid, 0);
	free(sstack);
	portDestroy(srv.port);

	SR_HB(SUITE, "echo served=%ld done=%ld", srv.served, total_done);

	if (faults == 0 && started > 0) {
		SR_OK(t, SUITE, "echo", "%d clients x %d rounds = %ld round-trips, all nonces intact%s",
			started, ECHO_ROUNDS, total_done, limits ? " [some limited]" : "");
	}
}


/* ============================ lookup test ============================ */

/* Names we storm. /dev and devfs are always present once user-space is up;
 * /dev/vcmbox / /dev/thermal are present on this build but tolerated-absent so
 * the test is portable. An optional name that is simply never there is NOTED,
 * not a fault; a name that resolves once and then flickers IS a fault. */
static const char *const lookup_names[] = {
	"/dev",
	"devfs",
	"/dev/vcmbox",
	"/dev/thermal",
};
#define N_LOOKUP_NAMES ((int)(sizeof(lookup_names) / sizeof(lookup_names[0])))

typedef struct {
	int idx;
	int rounds;
	volatile int rc;
	volatile int done;
	char detail[160];
} lookup_arg_t;

static void lookup_thread(void *arg)
{
	lookup_arg_t *a = arg;
	int i, n;
	/* per-name: have we ever seen it resolve? then a later miss is suspicious */
	int seen[N_LOOKUP_NAMES] = { 0 };

	a->rc = 0;
	a->done = 0;
	a->detail[0] = '\0';

	for (i = 0; i < a->rounds; i++) {
		for (n = 0; n < N_LOOKUP_NAMES; n++) {
			oid_t oid;
			int rc = lookup(lookup_names[n], NULL, &oid);
			if (rc == 0) {
				seen[n] = 1;
			}
			else if (seen[n]) {
				/* Resolved before, now gone without removal — a resolver fault. */
				a->rc = 1;
				snprintf(a->detail, sizeof(a->detail),
					"thr=%d '%s' resolved earlier then FAILED rc=%d round=%d (flaky resolver)",
					a->idx, lookup_names[n], rc, i);
				endthread();
			}
			a->done++;
		}
	}
	endthread();
}

static void run_lookup(sr_tally_t *t, int nthr)
{
	static lookup_arg_t args[MAX_THR];
	void *stacks[MAX_THR] = { 0 };
	handle_t tids[MAX_THR];
	int i, started = 0, faults = 0;
	long total_done = 0;
	int present[N_LOOKUP_NAMES] = { 0 };

	if (nthr > MAX_THR) nthr = MAX_THR;
	if (nthr < 1) nthr = 1;

	/* Probe each name once up front so we can report which were present. */
	for (i = 0; i < N_LOOKUP_NAMES; i++) {
		oid_t oid;
		present[i] = (lookup(lookup_names[i], NULL, &oid) == 0);
	}
	SR_HB(SUITE, "lookup threads=%d rounds=%d (dev=%d devfs=%d vcmbox=%d thermal=%d)",
		nthr, LOOKUP_ROUNDS, present[0], present[1], present[2], present[3]);

	for (i = 0; i < nthr; i++) {
		args[i].idx = i;
		args[i].rounds = LOOKUP_ROUNDS;
		args[i].rc = -1;
		args[i].done = 0;
		stacks[i] = malloc(IPC_STACKSZ);
		if (stacks[i] == NULL) {
			SR_LIMIT(t, SUITE, "lookup", "cannot allocate stack thr=%d (started=%d)", i, started);
			break;
		}
		if (beginthreadex(lookup_thread, 4, stacks[i], IPC_STACKSZ, &args[i], &tids[i]) < 0) {
			SR_LIMIT(t, SUITE, "lookup", "beginthreadex failed thr=%d (started=%d)", i, started);
			free(stacks[i]);
			stacks[i] = NULL;
			break;
		}
		started++;
	}

	for (i = 0; i < started; i++) {
		(void)threadJoin(tids[i], 0);
		total_done += args[i].done;
		if (args[i].rc == 1) {
			faults++;
			SR_FAULT(t, SUITE, "lookup", "%s", args[i].detail);
		}
		free(stacks[i]);
	}

	SR_HB(SUITE, "lookup done lookups=%ld", total_done);
	if (faults == 0 && started > 0) {
		SR_OK(t, SUITE, "lookup", "%d threads x %d rounds = %ld lookups, resolver stable",
			started, LOOKUP_ROUNDS, total_done);
	}
}


/* ============================ vcmbox test ============================ */

/* One property transaction through a pre-resolved /dev/vcmbox oid. Returns 0 and
 * stores up to nOut response words, or a negative errno. Mirrors libvcmbox's
 * vcmbox_call, inlined so this util needs no devices-tree dependency. */
static int vcmbox_xact(oid_t oid, uint32_t tag, uint32_t valBufSize,
	const uint32_t *in, uint32_t nIn, uint32_t *out, uint32_t nOut)
{
	msg_t msg;
	vcmbox_req_t *req = (vcmbox_req_t *)msg.i.raw;
	const vcmbox_resp_t *resp = (const vcmbox_resp_t *)msg.o.raw;
	uint32_t i;
	int err;

	if (nIn > VCMBOX_MAX_WORDS || nOut > VCMBOX_MAX_WORDS)
		return -EINVAL;

	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid = oid;
	req->tag = tag;
	req->valBufSize = valBufSize;
	req->nIn = nIn;
	for (i = 0; i < nIn; i++)
		req->in[i] = in[i];

	err = msgSend(oid.port, &msg);
	if (err < 0)
		return err;
	if (resp->err != 0)
		return resp->err;
	if (out != NULL) {
		for (i = 0; i < nOut && i < resp->nOut; i++)
			out[i] = resp->out[i];
	}
	return 0;
}

typedef struct {
	oid_t oid;
	int idx;
	int rounds;
	uint32_t fw_expect;  /* firmware rev seen by main thread; all must agree */
	volatile int rc;
	volatile int done;
	char detail[160];
} vcmbox_arg_t;

static void vcmbox_thread(void *arg)
{
	vcmbox_arg_t *a = arg;
	int i;

	a->rc = 0;
	a->done = 0;
	a->detail[0] = '\0';

	for (i = 0; i < a->rounds; i++) {
		uint32_t fw[2] = { 0, 0 };
		uint32_t tv[2] = { 0, 0 };
		int rc;

		/* GET_FIRMWARE_REV: a constant — every concurrent caller must get the
		 * same value the main thread saw. A different value = a garbled / crossed
		 * reply from the shared FIFO (the serialization fix must prevent this). */
		rc = vcmbox_xact(a->oid, VC_PROP_GET_FIRMWARE_REV, 4u, NULL, 0, fw, 1u);
		if (rc < 0) {
			a->rc = (rc == -EAGAIN || rc == -ENOMEM) ? 2 : 1;
			snprintf(a->detail, sizeof(a->detail), "thr=%d fwrev xact rc=%d round=%d", a->idx, rc, i);
			endthread();
		}
		if (fw[0] != a->fw_expect) {
			a->rc = 1;
			snprintf(a->detail, sizeof(a->detail),
				"thr=%d fwrev=0x%08x != expected 0x%08x round=%d (GARBLED mailbox reply)",
				a->idx, fw[0], a->fw_expect, i);
			endthread();
		}

		/* GET_TEMPERATURE: an 8-byte value buffer (id echoed in word0, temp in
		 * word1). A plausible SoC temp is 0..150 C = 0..150000 mC; anything wildly
		 * out of range is a corrupted reply, not real telemetry. */
		rc = vcmbox_xact(a->oid, VC_PROP_GET_TEMPERATURE, 8u, NULL, 0, tv, 2u);
		if (rc < 0) {
			a->rc = (rc == -EAGAIN || rc == -ENOMEM) ? 2 : 1;
			snprintf(a->detail, sizeof(a->detail), "thr=%d temp xact rc=%d round=%d", a->idx, rc, i);
			endthread();
		}
		if (tv[1] > 150000u) {
			a->rc = 1;
			snprintf(a->detail, sizeof(a->detail),
				"thr=%d temp=%u mC out of range round=%d (GARBLED mailbox reply)",
				a->idx, tv[1], i);
			endthread();
		}
		a->done++;
	}
	endthread();
}

static void run_vcmbox(sr_tally_t *t, int nthr)
{
	static vcmbox_arg_t args[MAX_THR];
	void *stacks[MAX_THR] = { 0 };
	handle_t tids[MAX_THR];
	oid_t oid;
	uint32_t fw[2] = { 0, 0 };
	int i, started = 0, faults = 0, limits = 0;
	long total_done = 0;

	if (nthr > MAX_THR) nthr = MAX_THR;
	if (nthr < 1) nthr = 1;

	/* Resolve /dev/vcmbox once. We're well past `bind devfs /dev`, so a plain
	 * lookup() suffices (no early-boot devfs-port fallback needed). */
	if (lookup("/dev/vcmbox", NULL, &oid) != 0) {
		/* Fall back to concurrent /dev/thermal reads (which route through the
		 * same shared vcmbox server). Note it and skip — not a fault. */
		oid_t toid;
		if (lookup("/dev/thermal", NULL, &toid) == 0) {
			SR_LIMIT(t, SUITE, "vcmbox", "/dev/vcmbox absent; /dev/thermal present "
				"(routes through vcmbox) — see report for fallback rationale");
		}
		else {
			SR_LIMIT(t, SUITE, "vcmbox", "/dev/vcmbox and /dev/thermal both absent; skipped");
		}
		return;
	}

	/* Establish the firmware-rev reference from the main thread first. */
	if (vcmbox_xact(oid, VC_PROP_GET_FIRMWARE_REV, 4u, NULL, 0, fw, 1u) < 0) {
		SR_FAULT(t, SUITE, "vcmbox", "baseline GET_FIRMWARE_REV failed (server not answering)");
		return;
	}
	SR_HB(SUITE, "vcmbox oid.port=%u fwrev=0x%08x threads=%d rounds=%d",
		oid.port, fw[0], nthr, VCMBOX_ROUNDS);

	for (i = 0; i < nthr; i++) {
		args[i].oid = oid;
		args[i].idx = i;
		args[i].rounds = VCMBOX_ROUNDS;
		args[i].fw_expect = fw[0];
		args[i].rc = -1;
		args[i].done = 0;
		stacks[i] = malloc(IPC_STACKSZ);
		if (stacks[i] == NULL) {
			SR_LIMIT(t, SUITE, "vcmbox", "cannot allocate stack thr=%d (started=%d)", i, started);
			break;
		}
		if (beginthreadex(vcmbox_thread, 4, stacks[i], IPC_STACKSZ, &args[i], &tids[i]) < 0) {
			SR_LIMIT(t, SUITE, "vcmbox", "beginthreadex failed thr=%d (started=%d)", i, started);
			free(stacks[i]);
			stacks[i] = NULL;
			break;
		}
		started++;
	}

	SR_HB(SUITE, "vcmbox clients started=%d", started);

	for (i = 0; i < started; i++) {
		(void)threadJoin(tids[i], 0);
		total_done += args[i].done;
		if (args[i].rc == 1) {
			faults++;
			SR_FAULT(t, SUITE, "vcmbox", "%s", args[i].detail);
		}
		else if (args[i].rc == 2) {
			limits++;
			SR_LIMIT(t, SUITE, "vcmbox", "%s", args[i].detail);
		}
		free(stacks[i]);
	}

	SR_HB(SUITE, "vcmbox done xacts=%ld", total_done * 2);
	if (faults == 0 && started > 0) {
		SR_OK(t, SUITE, "vcmbox", "%d threads x %d rounds = %ld property xacts, "
			"FIFO serialized correctly (no garbled/crossed replies)%s",
			started, VCMBOX_ROUNDS, total_done * 2, limits ? " [some limited]" : "");
	}
}


int main(int argc, char *argv[])
{
	sr_tally_t tally = { 0 };
	const char *mode = (argc >= 2) ? argv[1] : "all";
	long intensity = (argc >= 3) ? strtol(argv[2], NULL, 10) : 0;
	int do_echo = 0, do_lookup = 0, do_vcmbox = 0;

	sr_init();

	if (strcmp(mode, "echo") == 0)
		do_echo = 1;
	else if (strcmp(mode, "lookup") == 0)
		do_lookup = 1;
	else if (strcmp(mode, "vcmbox") == 0)
		do_vcmbox = 1;
	else
		do_echo = do_lookup = do_vcmbox = 1; /* "all" */

	if (do_echo)
		run_echo(&tally, intensity > 0 && mode[0] == 'e' ? (int)intensity : DEFAULT_ECHO_THR);
	if (do_lookup)
		run_lookup(&tally, intensity > 0 && mode[0] == 'l' ? (int)intensity : DEFAULT_LOOKUP_THR);
	if (do_vcmbox)
		run_vcmbox(&tally, intensity > 0 && mode[0] == 'v' ? (int)intensity : DEFAULT_VCMBOX_THR);

	SR_SUMMARY(&tally, SUITE, "mode=%s", mode);
	return 0;
}
