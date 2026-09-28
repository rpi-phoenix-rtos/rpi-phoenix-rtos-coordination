/*
 * Phoenix-RTOS
 *
 * rpi4-kms host test - scaled display modes (M9): the connector's mode list, a
 * client setting a lower mode (SET_CRTC, atomic MODE_ID), the SET_PLANE value the
 * plane backend then sends (src = the framebuffer, dst = the screen, aspect kept,
 * centred), and the way back to the native mode (client close, SetCrtc fb 0,
 * RMFB, ACTIVE 0). No Pi.
 *
 * The REAL server: kms_main.c is #included (its request handlers are static),
 * kms_backend.c and kms_bo.c are linked, all from host copies whose only change is
 * that the aarch64 instructions (cntvct/cntfrq reads, dsb) are replaced by host
 * equivalents (run.sh checks every substitution). The firmware (kms_fw.c), the
 * vblank thread (kms_vblank.c) and the Phoenix calls are stand-ins below; the
 * mailbox stand-in records every SET_PLANE the backend sends.
 *
 * The test only talks the wire protocol (handle_raw) and reads what reached the
 * firmware, so the same file builds against any rpi4-kms source. Negative control
 * (run.sh): built against the g8 source (commit f90f74b4a, /bin/rpi4-kms-g8) it
 * must FAIL - g8 lists one mode and refuses every other. So must the g9 source
 * built with -DMODE_TEST_NATIVE_ONLY (the server's `-M native` switch).
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#define main kms_server_main
#include "kms_main.c"
#undef main

#include <stdlib.h>


uint64_t kms_host_now = 1000000u;   /* the cntvct stand-in (kms.h host copy) */

static int checks, fails;
static kms_parked_t answers[KMS_MAX_PARKED];
static uint32_t nans;


/* ---- the firmware: SET_PLANE recorder, blank, console ---- */

#define TAG_SET_PLANE_T 0x00048015u

typedef struct {
	uint8_t display, plane_id, vc_image_type;
	int8_t layer;
	uint16_t width, height, pitch, vpitch;
	uint32_t src_x, src_y, src_w, src_h;
	int16_t dst_x, dst_y;
	uint16_t dst_w, dst_h;
	uint8_t alpha, num_planes, is_vu, color_encoding;
	uint32_t planes[4];
	uint32_t transform;
} fwp_t;

_Static_assert(sizeof(fwp_t) == 60, "the SET_PLANE value (vc4_firmware_kms.c struct set_plane)");

static fwp_t fw_plane[KMS_PLANES_PER_CRTC];   /* last value per plane id */
static uint32_t fw_setplane_calls;
static int fw_blanked, fw_blank_calls;

int vcmbox_callXL(uint32_t tag, const void *in, uint32_t valBufSize, void *out)
{
	fwp_t v;

	(void)out;
	if ((tag != TAG_SET_PLANE_T) || (valBufSize != sizeof(fwp_t))) {
		return -ENOSYS;
	}
	memcpy(&v, in, sizeof(v));
	if (v.plane_id < KMS_PLANES_PER_CRTC) {
		fw_plane[v.plane_id] = v;
	}
	fw_setplane_calls++;
	return 0;
}

int vcmbox_call(uint32_t tag, uint32_t valBufSize, const uint32_t *in, uint32_t nIn, uint32_t *out, uint32_t nOut)
{
	(void)tag;
	(void)valBufSize;
	(void)in;
	(void)nIn;
	(void)out;
	(void)nOut;
	return -ENOSYS;
}

int kms_fw_init(void)
{
	return -ENODEV;
}

int kms_fw_prop(uint32_t tag, uint32_t valWords, const uint32_t *in, uint32_t nIn, uint32_t *out)
{
	(void)tag;
	(void)valWords;
	(void)in;
	(void)nIn;
	(void)out;
	return -ENOSYS;
}

int kms_fw_pan(uint32_t yoff, uint32_t *got)
{
	if (got != NULL) {
		*got = yoff;
	}
	return 0;
}

int kms_fw_console(int enable)
{
	(void)enable;
	return 0;
}

int kms_fw_blank(int on)
{
	fw_blanked = on;
	fw_blank_calls++;
	return 0;
}

int kms_bus_addr(uint64_t pa, size_t len, uint32_t *bus)   /* kms_fw.c's rule */
{
	if (pa + len > KMS_GIB) {
		return -ERANGE;
	}
	*bus = (uint32_t)pa;
	return 0;
}

void kms_mode_set_refresh(kms_crtc_state_t *c, uint32_t mhz)
{
	(void)c;
	(void)mhz;
}

/* kms_vblank.c */
int kms_vblank_init(void)
{
	return 0;
}

void kms_vblank_thread(void *arg)
{
	(void)arg;
}

void kms_vblank_fini(void)
{
}

const char *kms_vbl_name(int src)
{
	(void)src;
	return "host";
}


/* ---- the Phoenix calls ---- */

int memExport(oid_t *oid, void *va, size_t size)
{
	(void)oid;
	(void)va;
	(void)size;
	return 0;
}

int memUnexport(oid_t *oid)
{
	(void)oid;
	return 0;
}

addr_t va2pa(void *va)
{
	return (addr_t)srv.pool_pa + (addr_t)((uint8_t *)va - (uint8_t *)srv.pool_va);
}

int mutexLock(handle_t h)
{
	(void)h;
	return 0;
}

int mutexUnlock(handle_t h)
{
	(void)h;
	return 0;
}

int mutexCreate(handle_t *h)
{
	*h = 1;
	return 0;
}

int condCreate(handle_t *h)
{
	*h = 2;
	return 0;
}

int condSignal(handle_t h)
{
	(void)h;
	return 0;
}

int beginthread(void (*start)(void *), int priority, void *stack, unsigned int stacksz, void *arg)
{
	(void)start;
	(void)priority;
	(void)stack;
	(void)stacksz;
	(void)arg;
	return -ENOSYS;
}

int setPriority(int priority)
{
	(void)priority;
	return 0;
}

int msgSend(uint32_t port, msg_t *m)
{
	(void)port;
	(void)m;
	return -ENOSYS;
}

int msgRecv(uint32_t port, msg_t *m, msg_rid_t *rid)
{
	(void)port;
	(void)m;
	(void)rid;
	return -ENOSYS;
}

int msgRespond(uint32_t port, msg_t *m, msg_rid_t rid)
{
	(void)port;
	(void)m;
	(void)rid;
	return 0;
}

int portCreate(uint32_t *port)
{
	*port = 30u;
	return 0;
}

int portRegister(uint32_t port, const char *name, oid_t *oid)
{
	(void)port;
	(void)name;
	(void)oid;
	return 0;
}

int portUnregister(const char *name)
{
	(void)name;
	return 0;
}

int lookup(const char *name, oid_t *file, oid_t *dev)
{
	(void)name;
	(void)file;
	(void)dev;
	return -ENOENT;
}

int create_dev(oid_t *oid, const char *path)
{
	(void)oid;
	(void)path;
	return 0;
}

int destroy_dev(const char *path)
{
	(void)path;
	return 0;
}

const void *ioctl_unpack(msg_t *msg, unsigned long *request, id_t *id)
{
	(void)msg;
	*request = 0u;
	*id = 0u;
	return NULL;
}

void ioctl_setResponse(msg_t *msg, unsigned long request, int err, const void *data)
{
	(void)msg;
	(void)request;
	(void)err;
	(void)data;
}


/* ---- helpers ---- */

static void expect(const char *what, long long got, long long want)
{
	int ok = (got == want);

	checks++;
	if (!ok) {
		fails++;
	}
	printf("KMSHOST mode %-58s got=%-9lld want=%-9lld %s\n", what, got, want, ok ? "ok" : "FAIL");
}


/* One request through the server's dispatch (handle_raw), as libdrm-phoenix sends it. */
static int req(uint32_t client, uint32_t op, const void *u, size_t usz, const void *idata, size_t isz, void *odata,
	size_t osz, kms_resp_t *out)
{
	msg_t msg;
	kms_req_t rq;
	kms_resp_t r;

	memset(&msg, 0, sizeof(msg));
	memset(&rq, 0, sizeof(rq));
	rq.magic = KMS_MAGIC;
	rq.op = op;
	if (u != NULL) {
		memcpy(&rq.u, u, usz);
	}
	msg.type = mtDevCtl;
	msg.pid = 100 + (int)client;
	msg.oid.port = srv.port;
	msg.oid.id = client;
	memcpy(msg.i.raw, &rq, sizeof(rq));
	msg.i.data = idata;
	msg.i.size = isz;
	msg.o.data = odata;
	msg.o.size = osz;
	nans = 0u;
	(void)handle_raw(&msg, 0, answers, &nans);
	memcpy(&r, msg.o.raw, sizeof(r));
	if (out != NULL) {
		*out = r;
	}
	return r.err;
}


static void vblank(uint32_t n)
{
	while (n-- > 0u) {
		kms_host_now += kms_us_cnt(16667u);
		nans = 0u;
		kms_on_vblank(&srv.crtc[0], kms_host_now, 1u, answers, &nans);
	}
}


static uint32_t open_client(void)
{
	static int pid = 200;
	return (uint32_t)client_open(pid++);
}


static void close_client(uint32_t id)
{
	nans = 0u;
	client_close(id, answers, &nans);
}


/* CREATE_DUMB + ADDFB2 of a WxH XRGB8888 framebuffer. Returns the fb id (0 = failed). */
static uint32_t make_fb(uint32_t client, uint32_t w, uint32_t h, uint32_t *handle)
{
	kms_create_dumb_req_t cd;
	kms_addfb2_req_t af;
	kms_resp_t r;

	memset(&cd, 0, sizeof(cd));
	cd.width = w;
	cd.height = h;
	cd.bpp = 32u;
	cd.flags = KMS_DUMB_POOL;
	if (req(client, KMS_OP_CREATE_DUMB, &cd, sizeof(cd), NULL, 0u, NULL, 0u, &r) != 0) {
		return 0u;
	}
	if (handle != NULL) {
		*handle = r.u.dumb.handle;
	}
	memset(&af, 0, sizeof(af));
	af.width = w;
	af.height = h;
	af.format = KMS_FMT_XRGB8888;
	af.handle = r.u.dumb.handle;
	af.pitch = r.u.dumb.pitch;
	af.modifier = KMS_MOD_LINEAR;
	if (req(client, KMS_OP_ADDFB2, &af, sizeof(af), NULL, 0u, NULL, 0u, &r) != 0) {
		return 0u;
	}
	return r.u.fb.fb_id;
}


/* drmModeSetCrtc(fd, crtc, fb, 0, 0, &conn, 1, &mode): libdrm-phoenix ioc_setcrtc */
static int set_crtc(uint32_t client, uint32_t fb, uint32_t w, uint32_t h)
{
	kms_set_crtc_req_t q;

	memset(&q, 0, sizeof(q));
	q.crtc_id = KMS_ID_CRTC(0u);
	q.fb_id = fb;
	q.conn_id = KMS_ID_CONNECTOR(0u);
	q.mode_hdisplay = w;
	q.mode_vdisplay = h;
	return req(client, KMS_OP_SET_CRTC, &q, sizeof(q), NULL, 0u, NULL, 0u, NULL);
}


static uint32_t crtc_w, crtc_h, crtc_fb, crtc_mode_w;

static void get_crtc(uint32_t client)
{
	kms_obj_req_t q;
	kms_resp_t r;
	kms_modeinfo_t mi;

	memset(&q, 0, sizeof(q));
	memset(&mi, 0, sizeof(mi));
	q.id = KMS_ID_CRTC(0u);
	(void)req(client, KMS_OP_GET_CRTC, &q, sizeof(q), NULL, 0u, &mi, sizeof(mi), &r);
	crtc_w = r.u.crtc.hdisplay;
	crtc_h = r.u.crtc.vdisplay;
	crtc_fb = r.u.crtc.fb_id;
	crtc_mode_w = mi.hdisplay;
}


/* The CRTC's MODE_ID property -> its blob's hdisplay x vdisplay (w * 10000 + h). */
static long long mode_id_size(uint32_t client)
{
	kms_obj_req_t q;
	kms_prop_value_t pv[20];
	kms_blob_t bq;
	kms_modeinfo_t mi;
	kms_resp_t r;
	uint32_t i, n, blob = 0u;

	memset(&q, 0, sizeof(q));
	q.id = KMS_ID_CRTC(0u);
	q.type = KMS_OBJ_CRTC;
	q.max = 20u;
	if (req(client, KMS_OP_GET_PROPERTIES, &q, sizeof(q), NULL, 0u, pv, sizeof(pv), &r) != 0) {
		return -1;
	}
	n = (r.u.count < 20u) ? r.u.count : 20u;
	for (i = 0u; i < n; i++) {
		if (pv[i].prop_id == KMS_PROP_MODE_ID) {
			blob = (uint32_t)pv[i].value;
		}
	}
	memset(&bq, 0, sizeof(bq));
	memset(&mi, 0, sizeof(mi));
	bq.id = blob;
	if (req(client, KMS_OP_GET_BLOB, &bq, sizeof(bq), NULL, 0u, &mi, sizeof(mi), &r) != 0) {
		return -2;
	}
	return (long long)mi.hdisplay * 10000 + mi.vdisplay;
}


static kms_modeinfo_t conn_modes[16];
static uint32_t conn_nmodes;

static void get_connector(uint32_t client)
{
	kms_obj_req_t q;
	kms_resp_t r;

	memset(&q, 0, sizeof(q));
	memset(conn_modes, 0, sizeof(conn_modes));
	q.id = KMS_ID_CONNECTOR(0u);
	q.max = 16u;
	(void)req(client, KMS_OP_GET_CONNECTOR, &q, sizeof(q), NULL, 0u, conn_modes, sizeof(conn_modes), &r);
	conn_nmodes = r.u.conn.nmodes;
}


static const kms_modeinfo_t *listed(uint32_t w, uint32_t h)
{
	uint32_t i;

	for (i = 0u; (i < conn_nmodes) && (i < 16u); i++) {
		if ((conn_modes[i].hdisplay == w) && (conn_modes[i].vdisplay == h)) {
			return &conn_modes[i];
		}
	}
	return NULL;
}


/* an atomic commit of one plane (+ optional MODE_ID blob, ACTIVE) */
static int atomic1(uint32_t client, uint32_t plane, uint32_t fb, int32_t cx, int32_t cy, uint32_t cw, uint32_t ch,
	uint32_t sw, uint32_t sh, uint32_t mode_blob, uint32_t active, uint32_t flags)
{
	kms_atomic_req_t a;
	kms_atomic_plane_t st;

	memset(&a, 0, sizeof(a));
	memset(&st, 0, sizeof(st));
	st.plane_id = KMS_ID_PLANE(0u, plane);
	st.fb_id = fb;
	st.crtc_id = (fb != 0u) ? KMS_ID_CRTC(0u) : 0u;
	st.crtc_x = cx;
	st.crtc_y = cy;
	st.crtc_w = cw;
	st.crtc_h = ch;
	st.src_w = sw << 16;
	st.src_h = sh << 16;
	st.alpha = 0xffffu;
	st.rotation = 1u;
	a.flags = flags;
	a.nplanes = 1u;
	a.crtc_id = KMS_ID_CRTC(0u);
	a.mode_blob = mode_blob;
	a.active = active;
	return req(client, KMS_OP_ATOMIC, &a, sizeof(a), &st, sizeof(st), NULL, 0u, NULL);
}


static uint32_t create_blob(uint32_t client, const kms_modeinfo_t *mi)
{
	kms_resp_t r;

	return (req(client, KMS_OP_CREATE_BLOB, NULL, 0u, mi, sizeof(*mi), NULL, 0u, &r) == 0) ? r.u.blob.id : 0u;
}


/* dst of plane p as the firmware got it: x, y, w, h packed for one expect row each */
static int plane_is_set(uint32_t p)
{
	return (fw_plane[p].vc_image_type != 0u) ? 1 : 0;
}


static void expect_plane(const char *what, uint32_t p, uint32_t sw, uint32_t sh, int32_t dx, int32_t dy, uint32_t dw,
	uint32_t dh)
{
	char line[160];

	snprintf(line, sizeof(line), "%s: plane %u set", what, p);
	expect(line, plane_is_set(p), 1);
	snprintf(line, sizeof(line), "  src WxH (16.16 >> 16) = %ux%u", sw, sh);
	expect(line, (long long)(fw_plane[p].src_w >> 16) * 10000 + (fw_plane[p].src_h >> 16), (long long)sw * 10000 + sh);
	snprintf(line, sizeof(line), "  dst x,y = %d,%d", dx, dy);
	expect(line, (long long)fw_plane[p].dst_x * 10000 + fw_plane[p].dst_y, (long long)dx * 10000 + dy);
	snprintf(line, sizeof(line), "  dst WxH = %ux%u", dw, dh);
	expect(line, (long long)fw_plane[p].dst_w * 10000 + fw_plane[p].dst_h, (long long)dw * 10000 + dh);
}


static void setup(void)
{
	enum { POOL_MIB = 64 };
	kms_crtc_state_t *c = &srv.crtc[0];
	kms_modeinfo_t *m = &c->mode;
	void *pool = NULL;

	if (posix_memalign(&pool, _PAGE_SIZE, (size_t)POOL_MIB * KMS_MIB) != 0) {
		exit(2);
	}
	memset(&srv, 0, sizeof(srv));
	kms_cnt_hz = 54000000u;
	srv.be = &kms_backend_plane;
	srv.xl = 1;
	srv.port = 30u;
	srv.buf_port = 31u;
	srv.tty_fd = -1;
	srv.gate_us = 500u;
	srv.latch_guard_us = 2000u;
	srv.pool_max_end = KMS_GIB;
	srv.pool_va = pool;
	srv.pool_pa = 0x10000000u;
	srv.pool_size = (size_t)POOL_MIB * KMS_MIB;
	srv.pool_mib = POOL_MIB;
	srv.pool_ok = 1;
	srv.next_handle = 1u;
	srv.next_fb = KMS_ID_FB_BASE;
	srv.next_blob = KMS_ID_BLOB_BASE;
	srv.fb_w = 1920u;
	srv.fb_h = 1080u;
	srv.fb_pitch = 7680u;
	srv.fb_virt_h = 3240u;
	srv.fb_slots = 3u;
	srv.fb_layer = -127;
	srv.have_fb_layer = 1;
	srv.fb_format = KMS_FMT_XBGR8888;
	srv.ncrtc = 1u;
#ifdef MODE_TEST_NATIVE_ONLY
	srv.native_only = 1;   /* run.sh control: `rpi4-kms -M native` must fail the scaled rows like g8 */
#endif

	/* the bench's display, as kms_fw.c mode_fallback synthesizes it (GET_DISPLAY_TIMING answers zeros) */
	c->idx = 0;
	c->fw_display_id = 2u;
	c->connection = KMS_CONNECTED;
	m->clock = 148500u;
	m->hdisplay = 1920u;
	m->hsync_start = 2008u;
	m->hsync_end = 2052u;
	m->htotal = 2200u;
	m->vdisplay = 1080u;
	m->vsync_start = 1084u;
	m->vsync_end = 1089u;
	m->vtotal = 1125u;
	m->vrefresh = 60u;
	m->flags = KMS_MODE_FLAG_PHSYNC | KMS_MODE_FLAG_PVSYNC;
	m->type = KMS_MODE_TYPE_PREFERRED | KMS_MODE_TYPE_DRIVER;
	snprintf(m->name, sizeof(m->name), "1920x1080");
	c->refresh_mhz = 60000u;
	c->mode_blob = kms_blob_create(0u, m, sizeof(*m));
	if (srv.be->init(c) != 0) {
		exit(2);
	}
}


int main(void)
{
	uint32_t a, b, x, fb1, fb2, fb3, fbc, fb4, fb5, fb6, fb7, blob, i, clk_ok = 1u;
	kms_modeinfo_t bad;
	const kms_modeinfo_t *mi;
	uint32_t calls;

	setup();

	/* --- the connector's mode list --- */
	a = open_client();
	get_connector(a);
	expect("connector: nmodes >= 8", conn_nmodes >= 8u, 1);                      /* g8: 1 */
	expect("  mode 0 = 1920x1080", (long long)conn_modes[0].hdisplay * 10000 + conn_modes[0].vdisplay, 19201080);
	expect("  mode 0 is PREFERRED", (conn_modes[0].type & KMS_MODE_TYPE_PREFERRED) != 0u, 1);
	expect("  lists 1600x900", listed(1600u, 900u) != NULL, 1);
	expect("  lists 1440x1080", listed(1440u, 1080u) != NULL, 1);
	expect("  lists 1280x720", listed(1280u, 720u) != NULL, 1);
	expect("  lists 1024x768", listed(1024u, 768u) != NULL, 1);
	expect("  lists 960x540", listed(960u, 540u) != NULL, 1);
	expect("  lists 800x600", listed(800u, 600u) != NULL, 1);
	expect("  lists 640x480", listed(640u, 480u) != NULL, 1);
	expect("  nothing larger than the screen (1920x1200)", listed(1920u, 1200u) == NULL, 1);
	for (i = 1u; (i < conn_nmodes) && (i < 16u); i++) {
		const kms_modeinfo_t *s = &conn_modes[i];
		uint32_t hz = ((s->htotal != 0u) && (s->vtotal != 0u)) ?
			(uint32_t)(((uint64_t)s->clock * 1000u + ((uint64_t)s->htotal * s->vtotal) / 2u) / ((uint64_t)s->htotal * s->vtotal)) :
			0u;
		if ((s->vrefresh != 60u) || (hz != 60u) || ((s->type & KMS_MODE_TYPE_PREFERRED) != 0u)) {
			clk_ok = 0u;
		}
	}
	expect("  every lower mode: vrefresh 60, clock/htotal/vtotal 60 Hz, not PREFERRED",
		(conn_nmodes > 1u) && clk_ok, 1);

	/* --- SDL KMSDRM, fullscreen 1280x720: GBM surface 1280x720, first swap SetCrtc(mode) --- */
	fb1 = make_fb(a, 1280u, 720u, NULL);
	expect("1280x720 framebuffer", fb1 != 0u, 1);
	expect("SetCrtc 1280x720 (rc)", set_crtc(a, fb1, 1280u, 720u), 0);   /* g8: -EINVAL */
	expect_plane("SetCrtc 1280x720", 0u, 1280u, 720u, 0, 0, 1920u, 1080u);
	expect("  width/height/pitch = the framebuffer's",
		(long long)fw_plane[0].width * 100000 + fw_plane[0].height * 10 + (fw_plane[0].pitch == 5120u), 128007201);
	vblank(2u);
	get_crtc(a);
	expect("GET_CRTC: the mode clients see = 1280x720", (long long)crtc_w * 10000 + crtc_h, 12800720);
	expect("  its modeinfo = 1280x720", crtc_mode_w, 1280);
	expect("  shows the framebuffer", crtc_fb, fb1);
	expect("MODE_ID blob = 1280x720", mode_id_size(a), 12800720);
	expect("16:9 fills the screen: firmware fb not blanked", fw_blanked, 0);

	fb2 = make_fb(a, 1280u, 720u, NULL);
	{
		kms_page_flip_req_t pf;
		memset(&pf, 0, sizeof(pf));
		pf.crtc_id = KMS_ID_CRTC(0u);
		pf.fb_id = fb2;
		pf.flags = KMS_PAGE_FLIP_EVENT;
		expect("page flip to a second 1280x720 buffer (rc)",
			req(a, KMS_OP_PAGE_FLIP, &pf, sizeof(pf), NULL, 0u, NULL, 0u, NULL), 0);
	}
	expect_plane("  the flip", 0u, 1280u, 720u, 0, 0, 1920u, 1080u);
	vblank(2u);

	/* cursor plane in the lower mode: scaled like everything else (a panel fitter) */
	fbc = make_fb(a, 64u, 64u, NULL);
	expect("cursor 64x64 at 100,100 (atomic, rc)", atomic1(a, 7u, fbc, 100, 100, 64u, 64u, 64u, 64u, 0u, 1u, 0u), 0);
	expect_plane("  cursor in 1280x720", 7u, 64u, 64u, 150, 150, 96u, 96u);
	vblank(2u);

	/* --- a 4:3 mode: pillarboxed, and the cursor follows --- */
	fb3 = make_fb(a, 1024u, 768u, NULL);
	calls = fw_setplane_calls;
	expect("SetCrtc 1024x768 (rc)", set_crtc(a, fb3, 1024u, 768u), 0);   /* g8: -EINVAL */
	expect_plane("SetCrtc 1024x768", 0u, 1024u, 768u, 240, 0, 1440u, 1080u);
	expect_plane("  the cursor re-applied in 1024x768", 7u, 64u, 64u, 381, 141, 90u, 90u);
	expect("  SET_PLANE calls = primary + cursor", fw_setplane_calls - calls, 2);
	expect("  bars: firmware fb blanked", fw_blanked, 1);
	vblank(2u);
	get_crtc(a);
	expect("GET_CRTC 1024x768", (long long)crtc_w * 10000 + crtc_h, 10240768);

	/* --- the game exits (or dies): its close takes the mode with it --- */
	close_client(a);
	expect("client closed: primary unset", plane_is_set(0u), 0);
	expect("  cursor unset", plane_is_set(7u), 0);
	expect("  firmware fb unblanked (console back)", fw_blanked, 0);
	b = open_client();
	get_crtc(b);
	expect("next client: GET_CRTC = native 1920x1080", (long long)crtc_w * 10000 + crtc_h, 19201080);
	expect("  modeinfo native", crtc_mode_w, 1920);
	expect("  nothing on the primary", crtc_fb, 0);
	expect("  MODE_ID = native", mode_id_size(b), 19201080);

	/* --- the native mode is the identity (the g8 behaviour) --- */
	fb4 = make_fb(b, 1920u, 1080u, NULL);
	expect("SetCrtc native 1920x1080 (rc)", set_crtc(b, fb4, 1920u, 1080u), 0);
	expect_plane("SetCrtc native", 0u, 1920u, 1080u, 0, 0, 1920u, 1080u);
	vblank(2u);

	/* --- SDL KMSDRM DestroySurfaces: SetCrtc(fb 0 = the console, original mode) --- */
	fb5 = make_fb(b, 1600u, 900u, NULL);
	expect("SetCrtc 1600x900 (rc)", set_crtc(b, fb5, 1600u, 900u), 0);   /* g8: -EINVAL */
	expect_plane("SetCrtc 1600x900", 0u, 1600u, 900u, 0, 0, 1920u, 1080u);
	vblank(2u);
	get_crtc(b);
	expect("GET_CRTC 1600x900", (long long)crtc_w * 10000 + crtc_h, 16000900);
	expect("SetCrtc fb 0 + native mode (rc)", set_crtc(b, 0u, 1920u, 1080u), 0);
	vblank(2u);
	get_crtc(b);
	expect("  back to native", (long long)crtc_w * 10000 + crtc_h, 19201080);
	expect("  primary unset", plane_is_set(0u), 0);

	/* --- RMFB of the shown framebuffer (DRM: disables the CRTC) --- */
	fb6 = make_fb(b, 960u, 540u, NULL);
	expect("SetCrtc 960x540 (rc)", set_crtc(b, fb6, 960u, 540u), 0);   /* g8: -EINVAL */
	expect_plane("SetCrtc 960x540", 0u, 960u, 540u, 0, 0, 1920u, 1080u);
	vblank(2u);
	{
		kms_fb_resp_t q;
		memset(&q, 0, sizeof(q));
		q.fb_id = fb6;
		expect("RMFB of the shown 960x540 framebuffer (rc)", req(b, KMS_OP_RMFB, &q, sizeof(q), NULL, 0u, NULL, 0u, NULL), 0);
	}
	get_crtc(b);
	expect("  back to native", (long long)crtc_w * 10000 + crtc_h, 19201080);

	/* --- atomic MODE_ID (wlroots, Weston, v3dv's display WSI) --- */
	get_connector(b);
	mi = listed(1440u, 1080u);
	blob = (mi != NULL) ? create_blob(b, mi) : 0u;
	fb7 = make_fb(b, 1440u, 1080u, NULL);
	expect("atomic MODE_ID 1440x1080 + primary (rc)",
		atomic1(b, 0u, fb7, 0, 0, 1440u, 1080u, 1440u, 1080u, blob, 1u, KMS_ATOMIC_ALLOW_MODESET), 0);   /* g8: -EINVAL */
	expect_plane("  atomic 1440x1080", 0u, 1440u, 1080u, 240, 0, 1440u, 1080u);
	vblank(2u);
	expect("  MODE_ID = 1440x1080", mode_id_size(b), 14401080);
	memset(&bad, 0, sizeof(bad));
	bad.hdisplay = 1366u;
	bad.vdisplay = 768u;
	bad.vrefresh = 60u;
	expect("atomic MODE_ID 1366x768 (not listed): -EINVAL",
		atomic1(b, 0u, fb7, 0, 0, 1366u, 768u, 1440u, 1080u, create_blob(b, &bad), 1u, 0u), -EINVAL);
	mi = listed(1280u, 720u);
	calls = fw_setplane_calls;
	expect("TEST_ONLY with MODE_ID 1280x720 (rc)",
		atomic1(b, 0u, fb7, 0, 0, 1280u, 720u, 1440u, 1080u, (mi != NULL) ? create_blob(b, mi) : 0u, 1u,
			KMS_ATOMIC_TEST_ONLY | KMS_ATOMIC_ALLOW_MODESET), 0);
	get_crtc(b);
	expect("  changes nothing: still 1440x1080", (long long)crtc_w * 10000 + crtc_h, 14401080);
	expect("  no firmware call", fw_setplane_calls - calls, 0);
	expect("ACTIVE 0 (rc)", atomic1(b, 0u, 0u, 0, 0, 0u, 0u, 0u, 0u, 0u, 0u, 0u), 0);
	vblank(2u);
	get_crtc(b);
	expect("  back to native", (long long)crtc_w * 10000 + crtc_h, 19201080);

	/* --- plain plane scaling at native (SetPlane with crtc != src): passed through --- */
	fb1 = make_fb(b, 640u, 360u, NULL);
	expect("SetPlane 640x360 -> 0,0 1920x1080 at native (rc)",
		atomic1(b, 0u, fb1, 0, 0, 1920u, 1080u, 640u, 360u, 0u, 1u, 0u), 0);
	expect_plane("  plane scaling", 0u, 640u, 360u, 0, 0, 1920u, 1080u);
	vblank(2u);

	/* --- another client's close does not take a mode it did not set --- */
	fb2 = make_fb(b, 1280u, 720u, NULL);
	expect("SetCrtc 1280x720 by client b (rc)", set_crtc(b, fb2, 1280u, 720u), 0);
	vblank(2u);
	x = open_client();
	close_client(x);
	get_crtc(b);
	expect("an unrelated client closes: still 1280x720", (long long)crtc_w * 10000 + crtc_h, 12800720);
	close_client(b);
	x = open_client();
	get_crtc(x);
	expect("client b closes: native", (long long)crtc_w * 10000 + crtc_h, 19201080);
	expect("  primary unset", plane_is_set(0u), 0);
	close_client(x);
	expect("  bos_live 0 at the end", srv.st.bos_live, 0);

	printf("KMSHOST RESULT mode checks=%d fails=%d verdict=%s\n", checks, fails, (fails == 0) ? "PASS" : "FAIL");
	free((void *)srv.pool_va);
	return (fails == 0) ? 0 : 1;
}
