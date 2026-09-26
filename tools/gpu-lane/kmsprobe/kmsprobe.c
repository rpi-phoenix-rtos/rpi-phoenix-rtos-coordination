/*
 * Phoenix-RTOS
 *
 * kmsprobe - new GPU lane experiments E3 / E4 / E6 (Raspberry Pi 4, BCM2711)
 *
 * Question: can a display server drive the VideoCore firmware's plane API
 * (property-mailbox SET_PLANE, the "fake KMS" interface) for buffers WE
 * allocate, for several planes at once, paced by the SMI vblank interrupt the
 * firmware raises - and which physical addresses can such a plane scan out?
 *
 * Everything goes through /dev/vcmbox (never the mailbox FIFO directly). The
 * HVS register block and display-list memory are mapped READ-ONLY and used as
 * an independent witness: after a SET_PLANE we look for our buffer's address in
 * the display list the firmware actually handed to the hardware, and the HVS
 * frame counters give a vblank reference that does not depend on the SMI IRQ.
 *
 * Every result line starts with "KMSPROBE " so a log can be graded by grep.
 * The display is restored (every plane we touched is unset, the firmware
 * framebuffer unblanked) on normal exit, on SIGINT/SIGTERM, and by the
 * explicit `kmsprobe restore` mode (for use after a crash).
 *
 * Hardware/firmware facts used here (tag numbers, struct layouts, the SMI
 * vblank convention, HVS register offsets) were read from the Raspberry Pi
 * Linux fork and the bench DTB; the image-type values come from the BSD-3
 * raspberrypi/userland vc_image_types.h. No code was copied. Sources are
 * listed in docs/gpu-new-lane/E3-firmware-planes-vblank.md.
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/interrupt.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/platform.h>
#include <sys/threads.h>
#include <sys/types.h>

#include <phoenix/arch/aarch64/generic/generic.h>

#include "libvcmbox.h"


#ifndef _PAGE_SIZE
#define _PAGE_SIZE 4096UL
#endif

#define PAGE_ROUND(x) (((x) + _PAGE_SIZE - 1UL) & ~(_PAGE_SIZE - 1UL))
#define MIB           (1024UL * 1024UL)
#define GIB           (1024ULL * 1024ULL * 1024ULL)


/* ------------------------------------------------------------------------ */
/* Firmware property tags (raspberrypi-firmware.h in the RPi Linux fork)     */
/* ------------------------------------------------------------------------ */

#define TAG_GET_FIRMWARE_REV   0x00000001u
#define TAG_GET_BOARD_REV      0x00010002u
#define TAG_GET_ARM_MEMORY     0x00010005u
#define TAG_GET_VC_MEMORY      0x00010006u
#define TAG_FB_BLANK           0x00040002u
#define TAG_FB_GET_PHYS_WH     0x00040003u
#define TAG_FB_GET_VIRT_WH     0x00040004u
#define TAG_FB_GET_DEPTH       0x00040005u
#define TAG_FB_GET_PIXEL_ORDER 0x00040006u
#define TAG_FB_GET_PITCH       0x00040008u
#define TAG_FB_GET_VIRT_OFFSET 0x00040009u
#define TAG_FB_GET_LAYER       0x0004000cu
#define TAG_FB_GET_NUM_DISP    0x00040013u
#define TAG_FB_GET_DISPLAY_ID  0x00040016u
#define TAG_FB_SET_DISPLAY_NUM 0x00048013u
#define TAG_FB_SET_VIRT_OFFSET 0x00048009u
#define TAG_FB_SET_VSYNC       0x0004800eu /* blocks in firmware until the next vsync */
#define TAG_SET_PLANE          0x00048015u
#define TAG_GET_DISPLAY_TIMING 0x00040017u
#define TAG_GET_DISPLAY_CFG    0x00040018u

/* VC_IMAGE_TYPE_T values (raspberrypi/userland interface/vctypes/vc_image_types.h,
 * BSD-3; values checked by compiling that header on the host). */
#define VC_IMAGE_MIN      0u  /* "no image": what an unset plane carries */
#define VC_IMAGE_ARGB8888 43u
#define VC_IMAGE_XRGB8888 44u /* u32 pixel 0x00RRGGBB, little-endian */

#define PLANES_PER_DISPLAY 8u


/* The firmware's SET_PLANE value buffer: 60 bytes, 15 words. Field order and
 * sizes from struct set_plane, vc4_firmware_kms.c:64-96. */
typedef struct {
	uint8_t display;       /* firmware display id (GET_DISPLAY_ID), NOT the index */
	uint8_t plane_id;      /* index + display_index * 8 */
	uint8_t vc_image_type; /* VC_IMAGE_*; VC_IMAGE_MIN = unset */
	int8_t layer;          /* dispmanx layer; fkms maps its primary to -127 */
	uint16_t width;        /* source image size */
	uint16_t height;
	uint16_t pitch; /* bytes */
	uint16_t vpitch;
	uint32_t src_x; /* 16.16 */
	uint32_t src_y;
	uint32_t src_w;
	uint32_t src_h;
	int16_t dst_x; /* screen position */
	int16_t dst_y;
	uint16_t dst_w;
	uint16_t dst_h;
	uint8_t alpha;
	uint8_t num_planes;
	uint8_t is_vu;
	uint8_t color_encoding;
	uint32_t planes[4]; /* 32-bit bus address of each image plane */
	uint32_t transform;
} fw_plane_t;

_Static_assert(sizeof(fw_plane_t) == 60, "SET_PLANE value buffer must be 60 bytes");

#define FW_PLANE_WORDS 15u


/* ------------------------------------------------------------------------ */
/* Register blocks                                                           */
/* ------------------------------------------------------------------------ */

/* SMI: the firmware signals vblank by raising the SMI interrupt.
 * vc4_firmware_kms.c:262-271, :1225-1273; bench DTB firmwarekms@7e600000:
 * reg <0x7e600000 0x100>, interrupts <GIC_SPI 112 level-high>. */
#define SMI_BASE         0xfe600000UL
#define SMICS            0x00u
#define SMIDSW0          0x14u
#define SMIDSW1          0x1cu
#define SMICS_INTS       ((1u << 9) | (1u << 10) | (1u << 11)) /* INTD|INTT|INTR */
#define SMI_NEW          0xabcd0000u /* firmware reports per-display bits in SMIDSWx */
#define SMI_IRQ_SPI      112u
#define SMI_IRQ_PHOENIX  (SMI_IRQ_SPI + 32u) /* SPI + 32, as bcm-genet's 157 -> 189 */

/* HVS (BCM2711 = HVS5): reg <0x7e400000 0x8000> (bcm2711.dtsi hvs node).
 * Offsets from vc4_regs.h; display-list memory at +0x4000 for HVS5
 * (vc4_hvs.c:2182, vc4_regs.h:550), 16 KiB = 4096 words (vc4_regs.h:548). */
#define HVS_BASE          0xfe400000UL
#define HVS_SIZE          0x8000UL
#define SCALER_DISPCTRL   0x00u
#define SCALER_DISPSTAT   0x04u
#define SCALER_DISPLISTX(x) (0x20u + 4u * (x))
#define SCALER_DISPCTRLX(x) (0x40u + 0x10u * (x))
#define SCALER_DISPSTATX(x) (0x48u + 0x10u * (x))
#define SCALER_DISPSTAT1  0x58u
#define SCALER_DISPSTAT2  0x68u
#define HVS_DLIST_OFF     0x4000u
#define HVS_DLIST_WORDS   4096u
#define HVS_CHANNELS      3u
#define CTL0_END          (1u << 31)
#define CTL0_VALID        (1u << 30)
#define CTL0_SIZE(w)      (((w) >> 24) & 0x3fu)
#define DISPSTAT_MODE(v)  (((v) >> 30) & 3u) /* 0 disabled, 1 init, 2 run, 3 eof */

/* GIC-400 distributor (bench DTB interrupt-controller@40041000 -> 0xff841000).
 * Read-only: ISENABLER / ISPENDR / ISACTIVER for the SMI line. */
#define GICD_BASE         0xff841000UL
#define GICD_ISENABLER(n) (0x100u + 4u * (n))
#define GICD_ISPENDR(n)   (0x200u + 4u * (n))
#define GICD_ISACTIVER(n) (0x300u + 4u * (n))


/* ------------------------------------------------------------------------ */
/* Output + timing                                                           */
/* ------------------------------------------------------------------------ */

static void kp(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void kp(const char *fmt, ...)
{
	va_list ap;

	fputs("KMSPROBE ", stdout);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
	fflush(stdout);
}


static inline uint64_t cnt_now(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v)::"memory");
	return v;
}


static uint64_t cnt_hz;


static inline uint64_t cnt_us(uint64_t ticks)
{
	return (ticks * 1000000ULL) / cnt_hz;
}


static inline void dsb(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
}


static int cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return (x > y) - (x < y);
}


/* Percentile of an array (sorts it in place). */
static uint32_t pctl(uint32_t *v, uint32_t n, uint32_t pct)
{
	if (n == 0u) {
		return 0u;
	}
	qsort(v, n, sizeof(*v), cmp_u32);
	return v[((uint64_t)(n - 1u) * pct) / 100u];
}


/* ------------------------------------------------------------------------ */
/* Global state + options                                                    */
/* ------------------------------------------------------------------------ */

enum { XPORT_AUTO = 0, XPORT_XL, XPORT_RAW60, XPORT_V48 };
enum { BUS_00 = 0, BUS_C0 };

static struct {
	/* options */
	int xport;       /* SET_PLANE transport (see kms_setPlane) */
	int xlAvailable; /* the vcmbox server understands the large-buffer call */
	int bus;         /* bus-address convention for planes[] */
	int blankFb;     /* -B: blank the firmware fb under our planes */
	unsigned int dispIdx;
	uint64_t bouncePaOpt; /* -P: /dev/vcmbox bounce-buffer PA (from its boot banner) */
	const char *tail;     /* raw60/v48 tail state: clean | zeroed | dirty | unknown | n/a */

	/* display */
	unsigned int numDisplays;
	uint32_t dispId; /* firmware display id of dispIdx */
	uint32_t w, h, refresh;
	int haveTiming;

	/* firmware fb (plo-allocated, fbcon + /dev/fb0 draw into it) */
	uint64_t fbPa;
	uint32_t fbW, fbH, fbPitch, fbVirtH, fbYoff;
	int32_t fbLayer;
	int haveFbLayer;

	/* mappings */
	volatile uint32_t *smi;
	volatile uint32_t *hvs;
	volatile uint32_t *gicd;
	oid_t vcmboxOid;
	int vcmboxResolved;

	/* restore bookkeeping */
	uint32_t touchedPlanes; /* bit i = plane_id (dispIdx*8 + i) was set */
	int fbBlanked;
	int fbInDlist0; /* was the fb pointer in the hardware list when we started */
	int dirty;      /* something needs restoring */
	int restoring;
} g = { .xport = XPORT_AUTO, .bus = BUS_00, .tail = "n/a" };

static volatile sig_atomic_t g_stop;


static void onSignal(int sig)
{
	(void)sig;
	g_stop = 1;
}


/* ------------------------------------------------------------------------ */
/* Mailbox via /dev/vcmbox                                                   */
/* ------------------------------------------------------------------------ */

/* Standard path: up to 12 value words (libvcmbox). The server bounds every
 * FIFO wait (MBOX_SPINS) and retries a bounded number of times, so the call
 * always returns. */
static int mb(uint32_t tag, uint32_t valWords, const uint32_t *in, uint32_t nIn, uint32_t *out)
{
	return vcmbox_call(tag, valWords * 4u, in, nIn, out, (out != NULL) ? valWords : 0u);
}


static int mb1(uint32_t tag, uint32_t in, uint32_t *out)
{
	uint32_t o[1] = { 0u };
	int rc = mb(tag, 1u, &in, 1u, o);
	if ((rc == 0) && (out != NULL)) {
		*out = o[0];
	}
	return rc;
}


static int vcmbox_oid(void)
{
	int tries;

	if (g.vcmboxResolved != 0) {
		return 0;
	}
	for (tries = 0; tries < 50; tries++) {
		if (lookup("/dev/vcmbox", NULL, &g.vcmboxOid) == 0) {
			g.vcmboxResolved = 1;
			return 0;
		}
		usleep(100 * 1000);
	}
	return -ETIMEDOUT;
}


/* Proposed large-buffer call (tools/gpu-lane/kmsprobe/vcmbox-xl.patch, NOT in
 * the tree): req->nIn carries this sentinel and the value buffer travels in
 * msg.i.data / msg.o.data. A server without the extension rejects nIn > 12
 * with -EINVAL before touching the FIFO (rpi4-vcmbox.c vcmbox_handleMsg), so
 * probing for it is harmless. */
#ifndef VCMBOX_NIN_XL
#define VCMBOX_NIN_XL 0x584c0000u
#endif

static int mb_xl(uint32_t tag, const uint32_t *val, uint32_t valWords, uint32_t *out)
{
	msg_t msg;
	vcmbox_req_t *req = (vcmbox_req_t *)msg.i.raw;
	const vcmbox_resp_t *resp = (const vcmbox_resp_t *)msg.o.raw;
	int err;

	if (vcmbox_oid() != 0) {
		return -EIO;
	}
	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid = g.vcmboxOid;
	req->tag = tag;
	req->valBufSize = valWords * 4u;
	req->nIn = VCMBOX_NIN_XL;
	msg.i.data = val;
	msg.i.size = valWords * 4u;
	msg.o.data = out;
	msg.o.size = (out != NULL) ? valWords * 4u : 0u;

	err = msgSend(g.vcmboxOid.port, &msg);
	if (err < 0) {
		return err;
	}
	if (msg.o.err < 0) {
		return msg.o.err;
	}
	return resp->err;
}


/* Workaround for a server WITHOUT the extension: send the 60-byte tag through
 * the existing message with the first 12 value words. The server writes the
 * uncapped valBufSize (60) into the tag header but only 12 value words + the
 * END tag (rpi4-vcmbox.c:167-183), so the firmware reads value words 12..14
 * (planes[2], planes[3], transform) from the END word (zeroed by the server's
 * memset) and from bounce-buffer words 18..19. No vcmbox transaction writes
 * those (the most it builds is 18 words) but MAP_CONTIGUOUS pages are NOT
 * zeroed, so they hold whatever DRAM held when the server started - see
 * bounce_tailCheck(), which locates the page, verifies it and zeroes words
 * 18..20 before this path is used. The firmware's handling of a total-size
 * word smaller than the tag it describes is unknown. */
static int mb_raw60(uint32_t tag, const uint32_t *val, uint32_t *out12)
{
	msg_t msg;
	vcmbox_req_t *req = (vcmbox_req_t *)msg.i.raw;
	const vcmbox_resp_t *resp = (const vcmbox_resp_t *)msg.o.raw;
	uint32_t i;
	int err;

	if (vcmbox_oid() != 0) {
		return -EIO;
	}
	memset(&msg, 0, sizeof(msg));
	msg.type = mtDevCtl;
	msg.oid = g.vcmboxOid;
	req->tag = tag;
	req->valBufSize = FW_PLANE_WORDS * 4u;
	req->nIn = VCMBOX_MAX_WORDS;
	for (i = 0; i < VCMBOX_MAX_WORDS; i++) {
		req->in[i] = val[i];
	}
	err = msgSend(g.vcmboxOid.port, &msg);
	if (err < 0) {
		return err;
	}
	if (msg.o.err < 0) {
		return msg.o.err;
	}
	if ((resp->err == 0) && (out12 != NULL)) {
		for (i = 0; (i < VCMBOX_MAX_WORDS) && (i < resp->nOut); i++) {
			out12[i] = resp->out[i];
		}
	}
	return resp->err;
}


/* Find the /dev/vcmbox bounce buffer's PA in the kernel log (the server's
 * boot banner "rpi4-vcmbox: mailbox @ ..., bounce buf_pa=0x...", which lands
 * in the 64 KiB kernel ring on this project). 0 if not found. */
static uint64_t bounce_fromKmsg(void)
{
	static const char key[] = "bounce buf_pa=";
	char buf[512 + 64 + 1];
	size_t keep = 0u;
	uint64_t pa = 0u;
	int fd, iter;

	fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		return 0u;
	}
	for (iter = 0; iter < 8192; iter++) {
		ssize_t n = read(fd, buf + keep, 512);
		size_t len;
		char *p;

		if (n <= 0) {
			if ((n < 0) && ((errno == EINTR) || (errno == EPIPE))) {
				continue;
			}
			break;
		}
		len = keep + (size_t)n;
		buf[len] = '\0';
		p = strstr(buf, key);
		if ((p != NULL) && (strlen(p) >= sizeof(key) + 9u)) {
			pa = strtoull(p + sizeof(key) - 1u, NULL, 16); /* keep the last banner seen */
		}
		keep = (len > 32u) ? 32u : len;
		memmove(buf, buf + len - keep, keep);
	}
	close(fd);
	return pa;
}


/* The raw60 / v48 paths make the firmware read bounce-buffer words 18..19
 * (and treat word 20 as the END of the declared 60-byte tag). Measure the
 * instrument: locate the page (-P, else the kernel log), map it, prove it is
 * the bounce buffer by fingerprinting a transaction we just made, and zero
 * words 18..20 if they are not zero. The server itself never reads or writes
 * those words, and both mappings are uncached. Sets g.tail. */
static void bounce_tailCheck(void)
{
	volatile uint32_t *bb;
	uint64_t pa = g.bouncePaOpt;
	const char *src = "opt";
	uint32_t fw = 0u, w[4];
	int tries, verified = 0;
	void *va;

	if (pa == 0u) {
		pa = bounce_fromKmsg();
		src = "kmsg";
	}
	if ((pa == 0u) || ((pa & (_PAGE_SIZE - 1u)) != 0u)) {
		g.tail = "unknown";
		kp("vcmbox_tail located=0 pa=0x%llx (pass -P <bounce_pa> from the rpi4-vcmbox boot banner)",
			(unsigned long long)pa);
		return;
	}
	va = mmap(NULL, _PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (off_t)pa);
	if (va == MAP_FAILED) {
		g.tail = "unknown";
		kp("vcmbox_tail located=1 src=%s pa=0x%llx map_failed", src, (unsigned long long)pa);
		return;
	}
	bb = va;
	/* Fingerprint: right after our GET_FIRMWARE_REVISION the page must hold
	 * [28, RESP_OK, tag 1, 4, resp, rev, END]. Retry: another client may slip in. */
	for (tries = 0; (tries < 5) && !verified; tries++) {
		if (mb1(TAG_GET_FIRMWARE_REV, 0u, &fw) != 0) {
			continue;
		}
		verified = (bb[0] == 28u) && (bb[1] == 0x80000000u) && (bb[2] == TAG_GET_FIRMWARE_REV) && (bb[5] == fw);
	}
	w[0] = bb[17];
	w[1] = bb[18];
	w[2] = bb[19];
	w[3] = bb[20];
	if (!verified) {
		g.tail = "unknown";
	}
	else if ((w[1] | w[2] | w[3]) == 0u) {
		g.tail = "clean";
	}
	else {
		bb[18] = 0u;
		bb[19] = 0u;
		bb[20] = 0u;
		dsb();
		g.tail = ((bb[18] | bb[19] | bb[20]) == 0u) ? "zeroed" : "dirty";
	}
	kp("vcmbox_tail located=1 src=%s pa=0x%llx verified=%d w17=0x%08x w18=0x%08x w19=0x%08x w20=0x%08x tail=%s", src,
		(unsigned long long)pa, verified, w[0], w[1], w[2], w[3], g.tail);
	(void)munmap(va, _PAGE_SIZE);
}


static const char *xportName(int x)
{
	switch (x) {
		case XPORT_XL: return "xl";
		case XPORT_RAW60: return "raw60";
		case XPORT_V48: return "v48";
		default: return "auto";
	}
}


/* Decide the SET_PLANE transport once. */
static void xport_init(void)
{
	uint32_t v[1] = { 0u }, o[1] = { 0u };
	int rc = mb_xl(TAG_GET_FIRMWARE_REV, v, 1u, o);

	g.xlAvailable = (rc == 0) && (o[0] != 0u);
	if (g.xport == XPORT_AUTO) {
		g.xport = g.xlAvailable ? XPORT_XL : XPORT_RAW60;
	}
	kp("vcmbox xl=%d xl_probe_rc=%d setplane_transport=%s", g.xlAvailable, rc, xportName(g.xport));
	if (g.xport != XPORT_XL) {
		bounce_tailCheck();
	}
}


/* One SET_PLANE; returns the transport rc and the call latency in ticks. */
static int kms_setPlane(const fw_plane_t *p, uint64_t *lat)
{
	uint32_t w[FW_PLANE_WORDS];
	uint32_t o[FW_PLANE_WORDS];
	uint64_t t0;
	int rc;

	memcpy(w, p, sizeof(w));

	if ((g.xport != XPORT_XL) && ((w[12] | w[13] | w[14]) != 0u)) {
		/* planes[2], planes[3] or transform set: the old-server paths cannot carry it. */
		return -EINVAL;
	}

	dsb(); /* pixel stores (Normal-NC) complete before the firmware can latch the plane */
	t0 = cnt_now();
	switch (g.xport) {
		case XPORT_XL:
			rc = mb_xl(TAG_SET_PLANE, w, FW_PLANE_WORDS, o);
			break;
		case XPORT_V48:
			rc = vcmbox_call(TAG_SET_PLANE, 48u, w, VCMBOX_MAX_WORDS, o, VCMBOX_MAX_WORDS);
			break;
		default:
			rc = mb_raw60(TAG_SET_PLANE, w, o);
			break;
	}
	if (lat != NULL) {
		*lat = cnt_now() - t0;
	}
	/* Record the attempt even on an error return: the firmware may have acted
	 * on it, and unsetting a plane that was never set is harmless. */
	if ((p->plane_id >= g.dispIdx * PLANES_PER_DISPLAY) && (p->plane_id < (g.dispIdx + 1u) * PLANES_PER_DISPLAY)) {
		if (p->vc_image_type != VC_IMAGE_MIN) {
			g.touchedPlanes |= 1u << (p->plane_id - g.dispIdx * PLANES_PER_DISPLAY);
			g.dirty = 1;
		}
	}
	return rc;
}


/* Unset a plane: only display + plane_id, everything else zero
 * (vc4_plane_set_blank, vc4_firmware_kms.c:385-395). */
static int kms_unsetPlane(uint8_t planeId)
{
	fw_plane_t p;
	int rc;

	memset(&p, 0, sizeof(p));
	p.display = (uint8_t)g.dispId;
	p.plane_id = planeId;
	rc = kms_setPlane(&p, NULL);
	if (rc == 0) {
		g.touchedPlanes &= ~(1u << (planeId % PLANES_PER_DISPLAY));
	}
	return rc;
}


static int fb_blank(int on)
{
	uint32_t st = 0u;
	int rc;

	if (g.numDisplays > 1u) {
		(void)mb1(TAG_FB_SET_DISPLAY_NUM, g.dispIdx, NULL);
	}
	rc = mb1(TAG_FB_BLANK, (on != 0) ? 1u : 0u, &st);
	if (g.numDisplays > 1u) {
		(void)mb1(TAG_FB_SET_DISPLAY_NUM, 0u, NULL);
	}
	if (rc == 0) {
		g.fbBlanked = (on != 0);
		g.dirty |= g.fbBlanked;
	}
	return rc;
}


/* ------------------------------------------------------------------------ */
/* HVS witness (read-only)                                                   */
/* ------------------------------------------------------------------------ */

static inline uint32_t hvs_rd(uint32_t off)
{
	return g.hvs[off / 4u];
}


static inline uint32_t dl_rd(uint32_t idx)
{
	return g.hvs[(HVS_DLIST_OFF / 4u) + (idx % HVS_DLIST_WORDS)];
}


/* 6-bit per-channel frame counters (vc4_hvs.c:812-826, VC4_GEN_5). */
static uint32_t hvs_frcnt(unsigned int ch)
{
	switch (ch) {
		case 0: return (hvs_rd(SCALER_DISPSTAT1) >> 20) & 0x3fu;
		case 1: return (hvs_rd(SCALER_DISPSTAT1) >> 14) & 0x3fu;
		default: return (hvs_rd(SCALER_DISPSTAT2) >> 14) & 0x3fu;
	}
}


static int hvs_chanActive(unsigned int ch)
{
	uint32_t mode = DISPSTAT_MODE(hvs_rd(SCALER_DISPSTATX(ch)));
	return (mode == 1u) || (mode == 2u);
}


typedef struct {
	int found;
	unsigned int ch;
	uint32_t idx;   /* dlist word index */
	uint32_t word;  /* the word as the firmware wrote it */
	uint32_t entry; /* entry number within the channel's list */
	uint32_t entries[HVS_CHANNELS];
} hvs_hit_t;


/* Walk every active channel's list (the algorithm of vc4_hvs_debugfs_dlist,
 * vc4_hvs.c:264-276) and look for a word whose low 30 bits fall in [lo, hi).
 * The low-30-bit compare matches a pointer in any of the four 1 GiB VideoCore
 * bus aliases; the full word is reported so the alias the firmware chose is
 * visible. Bounded by the dlist size. */
static void hvs_find(uint32_t lo, uint32_t hi, hvs_hit_t *hit)
{
	unsigned int ch;

	memset(hit, 0, sizeof(*hit));
	if (g.hvs == NULL) {
		return;
	}
	for (ch = 0; ch < HVS_CHANNELS; ch++) {
		uint32_t j = hvs_rd(SCALER_DISPLISTX(ch)) % HVS_DLIST_WORDS;
		uint32_t next = j, steps, entry = 0u;

		if (!hvs_chanActive(ch)) {
			continue;
		}
		for (steps = 0; steps < HVS_DLIST_WORDS; steps++, j = (j + 1u) % HVS_DLIST_WORDS) {
			uint32_t w = dl_rd(j);
			if (j == next) {
				if ((w & CTL0_END) != 0u) {
					break;
				}
				if (CTL0_SIZE(w) == 0u) {
					break; /* malformed; stop rather than loop */
				}
				next = (j + CTL0_SIZE(w)) % HVS_DLIST_WORDS;
				entry++;
				continue;
			}
			if ((hit->found == 0) && ((w & 0x3fffffffu) >= lo) && ((w & 0x3fffffffu) < hi)) {
				hit->found = 1;
				hit->ch = ch;
				hit->idx = j;
				hit->word = w;
				hit->entry = entry;
			}
		}
		hit->entries[ch] = entry;
	}
}


static void hvs_findPa(uint64_t pa, uint32_t len, hvs_hit_t *hit)
{
	uint32_t lo = (uint32_t)(pa & 0x3fffffffu);
	hvs_find(lo, lo + len, hit);
}


/* Compact register + display-list dump for later analysis (Stage B / E11). */
static void hvs_dump(const char *why)
{
	unsigned int ch;

	if (g.hvs == NULL) {
		kp("hvs why=%s unmapped", why);
		return;
	}
	kp("hvs why=%s dispctrl=0x%08x dispstat=0x%08x displist=%u,%u,%u", why, hvs_rd(SCALER_DISPCTRL),
		hvs_rd(SCALER_DISPSTAT), hvs_rd(SCALER_DISPLISTX(0)), hvs_rd(SCALER_DISPLISTX(1)),
		hvs_rd(SCALER_DISPLISTX(2)));
	for (ch = 0; ch < HVS_CHANNELS; ch++) {
		uint32_t j = hvs_rd(SCALER_DISPLISTX(ch)) % HVS_DLIST_WORDS;
		uint32_t next = j, steps, entry = 0u, printed = 0u;
		char line[256];
		int n = 0;

		kp("hvs ch=%u ctrl=0x%08x stat=0x%08x mode=%u frcnt=%u", ch, hvs_rd(SCALER_DISPCTRLX(ch)),
			hvs_rd(SCALER_DISPSTATX(ch)), DISPSTAT_MODE(hvs_rd(SCALER_DISPSTATX(ch))), hvs_frcnt(ch));
		if (!hvs_chanActive(ch)) {
			continue;
		}
		line[0] = '\0';
		for (steps = 0; (steps < HVS_DLIST_WORDS) && (printed < 96u); steps++, j = (j + 1u) % HVS_DLIST_WORDS) {
			uint32_t w = dl_rd(j);
			if (j == next) {
				if (n > 0) {
					kp("dlist ch=%u e=%u %s", ch, entry - 1u, line);
					n = 0;
					line[0] = '\0';
				}
				if ((w & CTL0_END) != 0u) {
					kp("dlist ch=%u end@%u entries=%u", ch, j, entry);
					break;
				}
				if (CTL0_SIZE(w) == 0u) {
					kp("dlist ch=%u malformed@%u w=0x%08x", ch, j, w);
					break;
				}
				next = (j + CTL0_SIZE(w)) % HVS_DLIST_WORDS;
				entry++;
			}
			if (n < (int)sizeof(line) - 12) {
				n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%08x", (n == 0) ? "" : " ", w);
			}
			printed++;
		}
		if (n > 0) {
			kp("dlist ch=%u e=%u %s", ch, (entry > 0u) ? entry - 1u : 0u, line);
		}
	}
}


/* The HVS channel scanning the firmware fb (the HDMI output we care about);
 * falls back to the first running channel. */
static int hvs_fbChannel(void)
{
	hvs_hit_t hit;
	unsigned int ch;

	if (g.hvs == NULL) {
		return -1;
	}
	if (g.fbPa != 0u) {
		hvs_findPa(g.fbPa, g.fbPitch * (g.fbVirtH ? g.fbVirtH : g.fbH), &hit);
		if (hit.found) {
			return (int)hit.ch;
		}
	}
	for (ch = 0; ch < HVS_CHANNELS; ch++) {
		if (DISPSTAT_MODE(hvs_rd(SCALER_DISPSTATX(ch))) == 2u) {
			return (int)ch;
		}
	}
	return -1;
}


static int fbInDlist(void)
{
	hvs_hit_t hit;

	if ((g.hvs == NULL) || (g.fbPa == 0u)) {
		return -1;
	}
	hvs_findPa(g.fbPa, g.fbPitch * (g.fbVirtH ? g.fbVirtH : g.fbH), &hit);
	return hit.found;
}


/* ------------------------------------------------------------------------ */
/* SMI vblank                                                                */
/* ------------------------------------------------------------------------ */

#define VB_RING 8192u

static struct {
	volatile uint32_t count;    /* interrupts we claimed */
	volatile uint32_t disp[2];  /* per-display events (new firmware protocol) */
	volatile uint32_t oldStyle; /* events without the SMI_NEW signature */
	volatile uint32_t spurious; /* line up but no SMICS interrupt bits */
	volatile uint32_t head;
	volatile uint64_t ts[VB_RING]; /* cntvct at each event of our display (or any, old style) */
	volatile uint32_t lastCs, lastDsw0;
	handle_t cond, lock, irq;
	int registered;
	uint32_t stormBase; /* count + spurious at registration */
	uint64_t stormT0;
} vb;


/* Claim one firmware vblank notification. Shared by the ISR and the polling
 * phase so both acknowledge exactly as the Linux fkms handler does. Returns 1
 * if the event belonged to our display. */
static inline int smi_ack(uint64_t now)
{
	volatile uint32_t *smi = g.smi;
	uint32_t cs = smi[SMICS / 4u];
	uint32_t d0, d1;
	int ours = 0;

	vb.lastCs = cs;
	if ((cs & SMICS_INTS) == 0u) {
		return -1;
	}
	smi[SMICS / 4u] = 0u;
	d0 = smi[SMIDSW0 / 4u];
	vb.lastDsw0 = d0;
	if ((d0 & 0xffff0000u) != SMI_NEW) {
		vb.oldStyle++;
		ours = 1;
	}
	else {
		if ((d0 & 1u) != 0u) {
			smi[SMIDSW0 / 4u] = SMI_NEW;
			vb.disp[0]++;
			ours |= (g.dispIdx == 0u);
		}
		d1 = smi[SMIDSW1 / 4u];
		if ((d1 & 1u) != 0u) {
			smi[SMIDSW1 / 4u] = SMI_NEW;
			vb.disp[1]++;
			ours |= (g.dispIdx == 1u);
		}
	}
	if (ours != 0) {
		vb.ts[vb.head % VB_RING] = now;
		vb.head++;
	}
	return ours;
}


/* Runs in interrupt context (EL1, our address space): MMIO + counters only. */
static int smi_isr(unsigned int n, void *arg)
{
	(void)n;
	(void)arg;
	if (smi_ack(cnt_now()) < 0) {
		/* Line up without the firmware's bits. Nothing else in Phoenix uses the
		 * SMI, so clear its control/status anyway in case some other SMI
		 * condition is holding the level line. */
		g.smi[SMICS / 4u] = 0u;
		vb.spurious++;
		return -1; /* not ours: do not wake the waiter */
	}
	vb.count++;
	return 1;
}


static int vb_register(void)
{
	int rc;

	if (vb.registered) {
		return 0;
	}
	if (g.smi == NULL) {
		return -ENODEV;
	}
	if ((mutexCreate(&vb.lock) != 0) || (condCreate(&vb.cond) != 0)) {
		return -ENOMEM;
	}
	g.smi[SMICS / 4u] = 0u; /* as the Linux driver does before requesting the IRQ */
	rc = interrupt(SMI_IRQ_PHOENIX, smi_isr, NULL, vb.cond, &vb.irq);
	if (rc < 0) {
		kp("vblank irq_register_failed irq=%u rc=%d", SMI_IRQ_PHOENIX, rc);
		return rc;
	}
	vb.registered = 1;
	vb.stormBase = vb.count + vb.spurious;
	vb.stormT0 = cnt_now();
	return 0;
}


static void vb_unregister(void)
{
	if (vb.registered) {
		(void)resourceDestroy(vb.irq);
		vb.registered = 0;
	}
}


/* Storm guard: a level interrupt the ISR cannot silence would pin the CPU it
 * is routed to. Counts claimed AND spurious entries since registration; a
 * sane vblank is <= 2 x 85 Hz. */
static int vb_stormCheck(void)
{
	uint64_t us;
	uint32_t d;

	if (!vb.registered) {
		return 0;
	}
	us = cnt_us(cnt_now() - vb.stormT0);
	d = (vb.count + vb.spurious) - vb.stormBase;
	if ((us > 0u) && (((uint64_t)d * 1000000ULL) / us > 2000u)) {
		vb_unregister();
		kp("vblank storm entries=%u in_us=%llu spurious=%u -> handler removed", d, (unsigned long long)us,
			vb.spurious);
		return 1;
	}
	return 0;
}


/* Wait for the next vblank of our display. Sources, in order of preference:
 * the SMI IRQ, the HVS frame counter (polled), a 60 Hz timer. Returns the
 * source used, or -1 on stop/timeout. */
enum { VS_IRQ = 0, VS_HVS, VS_TIMER };

static const char *vsName(int s)
{
	return (s == VS_IRQ) ? "irq" : (s == VS_HVS) ? "hvs_frcnt" : "timer";
}

static int vs_wait(int src, int hvsCh, uint32_t *seen)
{
	uint64_t t0 = cnt_now();
	uint64_t limit = cnt_hz / 10u; /* 100 ms: several frames at any sane refresh */

	if (src == VS_IRQ) {
		mutexLock(vb.lock);
		while ((vb.head == *seen) && (g_stop == 0) && ((cnt_now() - t0) < limit)) {
			/* Short timeout: the ISR does not take the mutex, so a wakeup can land
			 * between the check and the wait; a 2 ms re-check bounds that. */
			(void)condWait(vb.cond, vb.lock, 2000);
		}
		mutexUnlock(vb.lock);
		if (vb.head == *seen) {
			return -1;
		}
		*seen = vb.head;
		return src;
	}
	if ((src == VS_HVS) && (hvsCh >= 0)) {
		uint32_t f0 = hvs_frcnt((unsigned int)hvsCh);
		while ((hvs_frcnt((unsigned int)hvsCh) == f0) && (g_stop == 0) && ((cnt_now() - t0) < limit)) {
			usleep(200);
		}
		return (hvs_frcnt((unsigned int)hvsCh) != f0) ? src : -1;
	}
	usleep(16667);
	return VS_TIMER;
}


/* Pick the best vsync source that actually delivers. */
static int vs_pick(int hvsCh)
{
	uint32_t seen = 0u;

	if (vb_register() == 0) {
		seen = vb.head;
		if (vs_wait(VS_IRQ, hvsCh, &seen) == VS_IRQ) {
			return VS_IRQ;
		}
	}
	if ((hvsCh >= 0) && (vs_wait(VS_HVS, hvsCh, &seen) == VS_HVS)) {
		return VS_HVS;
	}
	return VS_TIMER;
}


/* ------------------------------------------------------------------------ */
/* Scan-out buffers                                                          */
/* ------------------------------------------------------------------------ */

typedef struct {
	volatile uint32_t *px;
	size_t len;
	uint64_t pa;
	uint32_t w, h, pitch;
	int physmem; /* mapped from a known PA rather than allocated */
} sbuf_t;


static void sbuf_free(sbuf_t *b)
{
	if (b->px != NULL) {
		(void)munmap((void *)b->px, b->len);
		b->px = NULL;
	}
}


/* Physically contiguous, Normal-NC (MAP_UNCACHED): CPU stores go straight to
 * DRAM and the HVS reads DRAM, so no cache maintenance is needed. The kernel
 * cleans+invalidates any stale lines when it maps a page uncached (kernel
 * 5d8645f6, docs/done/2026-09-04-uncached-page-stale-cache-rootcause.md).
 * MAP_CONTIGUOUS memory is not zeroed - every buffer is fully drawn. */
static int sbuf_alloc(sbuf_t *b, uint32_t w, uint32_t h)
{
	uint64_t paLast;
	void *va;

	memset(b, 0, sizeof(*b));
	b->w = w;
	b->h = h;
	b->pitch = w * 4u;
	b->len = PAGE_ROUND((size_t)b->pitch * h);
	va = mmap(NULL, b->len, PROT_READ | PROT_WRITE, MAP_CONTIGUOUS | MAP_UNCACHED | MAP_ANONYMOUS, -1, 0);
	if (va == MAP_FAILED) {
		return -ENOMEM;
	}
	b->px = va;
	b->px[0] = 0u;
	b->px[(b->len - 4u) / 4u] = 0u;
	b->pa = (uint64_t)va2pa(va);
	paLast = (uint64_t)va2pa((uint8_t *)va + b->len - _PAGE_SIZE);
	if ((b->pa == (uint64_t)(addr_t)-1) || (paLast != b->pa + b->len - _PAGE_SIZE)) {
		kp("buf va2pa_bad pa=0x%llx pa_last=0x%llx len=%zu", (unsigned long long)b->pa,
			(unsigned long long)paLast, b->len);
		sbuf_free(b);
		return -EFAULT;
	}
	return 0;
}


/* Allocate, preferring a buffer that ends at or below maxEnd (the buddy
 * allocator has no placement control, so take up to 24 chunks and hand the
 * rejects back). Falls back to the last chunk if none qualifies; the caller
 * reports where it landed. */
static int sbuf_allocPref(sbuf_t *b, uint32_t w, uint32_t h, uint64_t maxEnd)
{
	enum { NREJ = 24 };
	static sbuf_t rej[NREJ];
	int n, i, found = 0;

	for (n = 0; n < NREJ; n++) {
		if (sbuf_alloc(&rej[n], w, h) != 0) {
			break;
		}
		if (rej[n].pa + (uint64_t)rej[n].pitch * h <= maxEnd) {
			*b = rej[n];
			memset(&rej[n], 0, sizeof(rej[n]));
			found = 1;
			break;
		}
	}
	if (!found) {
		if (n == 0) {
			return -ENOMEM;
		}
		*b = rej[n - 1]; /* none qualified: keep the last one, caller reports it */
		memset(&rej[n - 1], 0, sizeof(rej[n - 1]));
	}
	for (i = 0; i < NREJ; i++) {
		sbuf_free(&rej[i]);
	}
	return 0;
}


/* Map a KNOWN scan-out-capable region (the firmware fb's second stacked
 * buffer) by physical address, as rpi4-fb maps the fb itself. */
static int sbuf_physmem(sbuf_t *b, uint64_t pa, uint32_t w, uint32_t h, uint32_t pitch)
{
	void *va;

	memset(b, 0, sizeof(*b));
	b->w = w;
	b->h = h;
	b->pitch = pitch;
	b->len = PAGE_ROUND((size_t)pitch * h + (pa & (_PAGE_SIZE - 1u)));
	va = mmap(NULL, b->len, PROT_READ | PROT_WRITE, MAP_UNCACHED | MAP_ANONYMOUS | MAP_PHYSMEM, -1,
		(off_t)(pa & ~(uint64_t)(_PAGE_SIZE - 1u)));
	if (va == MAP_FAILED) {
		return -ENOMEM;
	}
	b->px = (volatile uint32_t *)((uint8_t *)va + (pa & (_PAGE_SIZE - 1u)));
	b->pa = pa;
	b->physmem = 1;
	return 0;
}


static void sbuf_unmapPhys(sbuf_t *b)
{
	if (b->px != NULL) {
		void *base = (void *)((uintptr_t)b->px & ~(uintptr_t)(_PAGE_SIZE - 1u));
		(void)munmap(base, b->len);
		b->px = NULL;
	}
}


/* The 32-bit address handed to the firmware in planes[0]. Never truncates:
 * a PA that does not fit the chosen convention is refused (the project has
 * lost weeks to silent 64->32-bit and missing-alias bugs). */
static int busAddr(const sbuf_t *b, int conv, uint32_t *out)
{
	uint64_t end = b->pa + (uint64_t)b->pitch * b->h;

	if (conv == BUS_C0) {
		/* soc dma-ranges: bus 0xC0000000 -> CPU 0x0, 1 GiB (bench DTB soc node) */
		if (end > GIB) {
			return -ERANGE;
		}
		*out = 0xc0000000u | (uint32_t)b->pa;
		return 0;
	}
	/* raw CPU PA: what the Linux fkms driver passes (vc4 node at the DT root
	 * has no dma-ranges, CMA confined below 768 MiB). Must fit 32 bits. */
	if (end > 0x100000000ULL) {
		return -ERANGE;
	}
	*out = (uint32_t)b->pa;
	return 0;
}


static const char *busName(int conv)
{
	return (conv == BUS_C0) ? "c0" : "00";
}


static inline uint32_t rgb(uint32_t r, uint32_t g_, uint32_t b)
{
	return (r << 16) | (g_ << 8) | b;
}


/* Primary test image: 8 colour bars, a grey ramp, a 64 px grid, and a
 * 240x240 checker marker at the left (variant 0) or right (variant 1), so the
 * two flip buffers are distinguishable in a snapshot. */
static void draw_primary(sbuf_t *b, int variant)
{
	static const uint32_t bars[8] = {
		0xffffffu, 0xffff00u, 0x00ffffu, 0x00ff00u, 0xff00ffu, 0xff0000u, 0x0000ffu, 0x202020u
	};
	uint32_t x, y, stride = b->pitch / 4u;
	uint32_t mx0 = (variant == 0) ? 80u : ((b->w > 320u) ? b->w - 320u : 0u);
	uint32_t my0 = (b->h > 480u) ? b->h / 2u - 120u : 0u;

	for (y = 0; y < b->h; y++) {
		volatile uint32_t *row = b->px + (size_t)y * stride;
		for (x = 0; x < b->w; x++) {
			uint32_t c;
			if ((x >= mx0) && (x < mx0 + 240u) && (y >= my0) && (y < my0 + 240u)) {
				c = ((((x - mx0) / 30u) + ((y - my0) / 30u)) & 1u) ? 0xffffffu : 0x000000u;
			}
			else if (((x % 64u) == 0u) || ((y % 64u) == 0u)) {
				c = 0x808080u;
			}
			else if (y < (b->h * 2u) / 3u) {
				c = bars[(x * 8u) / b->w];
			}
			else {
				uint32_t v = (x * 255u) / b->w;
				c = rgb(v, v, v);
			}
			row[x] = c;
		}
	}
	dsb();
}


static void draw_box(sbuf_t *b, uint32_t fill, uint32_t border)
{
	uint32_t x, y, stride = b->pitch / 4u;

	for (y = 0; y < b->h; y++) {
		for (x = 0; x < b->w; x++) {
			int edge = (x < 8u) || (y < 8u) || (x >= b->w - 8u) || (y >= b->h - 8u);
			b->px[(size_t)y * stride + x] = edge ? border : fill;
		}
	}
	dsb();
}


/* Two-colour checkerboard, 120 px cells: the colour pair identifies which
 * range trial a snapshot shows (listed in the E3 doc). */
static void draw_checker(sbuf_t *b, uint32_t c0, uint32_t c1)
{
	uint32_t x, y, stride = b->pitch / 4u;

	for (y = 0; y < b->h; y++) {
		volatile uint32_t *row = b->px + (size_t)y * stride;
		for (x = 0; x < b->w; x++) {
			row[x] = (((x / 120u) + (y / 120u)) & 1u) ? c1 : c0;
		}
	}
	dsb();
}


/* A plane showing a whole buffer at (dx, dy), unscaled. */
static void plane_fill(fw_plane_t *p, uint8_t planeIdx, int8_t layer, const sbuf_t *b, uint32_t bus,
	int16_t dx, int16_t dy)
{
	memset(p, 0, sizeof(*p));
	p->display = (uint8_t)g.dispId;
	p->plane_id = (uint8_t)(planeIdx + g.dispIdx * PLANES_PER_DISPLAY);
	p->vc_image_type = VC_IMAGE_XRGB8888;
	p->layer = layer;
	p->width = (uint16_t)b->w;
	p->height = (uint16_t)b->h;
	p->pitch = (uint16_t)b->pitch;
	p->vpitch = (uint16_t)b->h;
	p->src_x = 0u;
	p->src_y = 0u;
	p->src_w = b->w << 16;
	p->src_h = b->h << 16;
	p->dst_x = dx;
	p->dst_y = dy;
	p->dst_w = (uint16_t)b->w;
	p->dst_h = (uint16_t)b->h;
	p->alpha = 0xffu;
	p->num_planes = 1u;
	p->planes[0] = bus;
}


/* Layer for a plane that must cover the firmware fb: fkms puts the fb at -127
 * by default (vc4_firmware_kms.c:891), but take the firmware's own answer
 * (GET_LAYER) when it has one. */
static int8_t layerAboveFb(void)
{
	if (g.haveFbLayer && (g.fbLayer >= 0) && (g.fbLayer < 120)) {
		return (int8_t)(g.fbLayer + 1);
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Setup / restore                                                           */
/* ------------------------------------------------------------------------ */

static void *mapDevice(uint64_t pa, size_t len, int prot)
{
	void *va = mmap(NULL, PAGE_ROUND(len), prot, MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS, -1,
		(off_t)pa);
	return (va == MAP_FAILED) ? NULL : va;
}


static void display_init(void)
{
	uint32_t o[9], in[2];
	platformctl_t pctl;

	memset(o, 0, sizeof(o));
	g.numDisplays = 1u;
	if (mb1(TAG_FB_GET_NUM_DISP, 0u, &o[0]) == 0) {
		g.numDisplays = o[0];
	}
	if (g.dispIdx >= g.numDisplays) {
		g.dispIdx = 0u;
	}
	g.dispId = g.dispIdx;
	if (mb1(TAG_FB_GET_DISPLAY_ID, g.dispIdx, &o[0]) == 0) {
		g.dispId = o[0];
	}

	memset(o, 0, sizeof(o));
	in[0] = g.dispId & 0xffu; /* set_timings.display is the first byte */
	if ((mb(TAG_GET_DISPLAY_TIMING, 9u, in, 1u, o) == 0) && (o[1] != 0u)) {
		g.w = o[2] & 0xffffu;
		g.h = o[4] >> 16;
		g.refresh = o[7] & 0xffffu;
		g.haveTiming = 1;
	}

	memset(&pctl, 0, sizeof(pctl));
	pctl.action = pctl_get;
	pctl.type = pctl_graphmode;
	if ((platformctl(&pctl) == 0) && (pctl.task.graphmode.framebuffer != 0u)) {
		g.fbPa = pctl.task.graphmode.framebuffer;
		g.fbW = pctl.task.graphmode.width;
		g.fbH = pctl.task.graphmode.height;
		g.fbPitch = pctl.task.graphmode.pitch;
	}
	memset(o, 0, sizeof(o));
	if (mb(TAG_FB_GET_VIRT_WH, 2u, o, 2u, o) == 0) {
		g.fbVirtH = o[1];
	}
	memset(o, 0, sizeof(o));
	if (mb(TAG_FB_GET_VIRT_OFFSET, 2u, o, 2u, o) == 0) {
		g.fbYoff = o[1];
	}
	memset(o, 0, sizeof(o));
	if (mb(TAG_FB_GET_LAYER, 1u, o, 1u, o) == 0) {
		g.fbLayer = (int32_t)o[0];
		g.haveFbLayer = 1;
	}
	if (!g.haveTiming || (g.w == 0u) || (g.h == 0u)) {
		g.w = g.fbW;
		g.h = g.fbH;
	}
}


static int common_init(int needHvs, int needSmi)
{
	uint64_t f;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
	cnt_hz = (f != 0u) ? f : 54000000u;

	signal(SIGINT, onSignal);
	signal(SIGTERM, onSignal);

	if (needHvs) {
		g.hvs = mapDevice(HVS_BASE, HVS_SIZE, PROT_READ);
		if (g.hvs == NULL) {
			kp("map hvs_failed pa=0x%lx", HVS_BASE);
		}
	}
	if (needSmi) {
		g.smi = mapDevice(SMI_BASE, _PAGE_SIZE, PROT_READ | PROT_WRITE);
		g.gicd = mapDevice(GICD_BASE, _PAGE_SIZE, PROT_READ);
		if (g.smi == NULL) {
			kp("map smi_failed pa=0x%lx", SMI_BASE);
		}
	}
	display_init();
	xport_init();
	g.fbInDlist0 = fbInDlist();
	kp("display idx=%u id=%u num=%u mode=%ux%u@%u timing=%d fb_pa=0x%llx fb=%ux%u pitch=%u virt_h=%u yoff=%u "
	   "fb_layer=%d(%s) fb_in_dlist=%d hvs_ch=%d bus=%s",
		g.dispIdx, g.dispId, g.numDisplays, g.w, g.h, g.refresh, g.haveTiming, (unsigned long long)g.fbPa, g.fbW,
		g.fbH, g.fbPitch, g.fbVirtH, g.fbYoff, (int)g.fbLayer, g.haveFbLayer ? "fw" : "unknown", g.fbInDlist0,
		hvs_fbChannel(), busName(g.bus));
	return ((g.w == 0u) || (g.h == 0u)) ? -1 : 0;
}


/* Put the display back the way we found it. Idempotent; runs from atexit
 * and after every mode. */
static void restore_all(void)
{
	unsigned int i;
	int rc, fbBack;

	if ((g.restoring != 0) || (g.dirty == 0)) {
		return;
	}
	g.restoring = 1;
	for (i = 0; i < PLANES_PER_DISPLAY; i++) {
		if ((g.touchedPlanes & (1u << i)) != 0u) {
			rc = kms_unsetPlane((uint8_t)(i + g.dispIdx * PLANES_PER_DISPLAY));
			kp("restore unset plane=%u rc=%d", i + g.dispIdx * PLANES_PER_DISPLAY, rc);
		}
	}
	if (g.fbBlanked) {
		rc = fb_blank(0);
		kp("restore fb_unblank rc=%d", rc);
	}
	usleep(50 * 1000); /* a few frames for the firmware to rebuild its list */
	fbBack = fbInDlist();
	if ((fbBack == 0) && (g.fbInDlist0 == 1)) {
		/* Last resort: re-assert the framebuffer through the classic tags. */
		uint32_t in[2] = { 0u, g.fbYoff };
		(void)fb_blank(0);
		(void)mb(TAG_FB_SET_VIRT_OFFSET, 2u, in, 2u, NULL);
		usleep(50 * 1000);
		fbBack = fbInDlist();
		kp("restore reassert_fb fb_in_dlist=%d", fbBack);
	}
	kp("restore done fb_in_dlist=%d fb_in_dlist_at_start=%d", fbBack, g.fbInDlist0);
	g.dirty = 0;
	g.restoring = 0;
}


/* ------------------------------------------------------------------------ */
/* Modes                                                                     */
/* ------------------------------------------------------------------------ */

static const char *boardMem(uint32_t rev)
{
	static const char *const m[] = { "256M", "512M", "1G", "2G", "4G", "8G" };
	uint32_t code = (rev >> 20) & 7u;

	if ((rev & (1u << 23)) == 0u) {
		return "old-style";
	}
	return (code < 6u) ? m[code] : "?";
}


static int mode_info(void)
{
	uint32_t o[9], in[2], i;

	memset(o, 0, sizeof(o));
	(void)mb1(TAG_GET_FIRMWARE_REV, 0u, &o[0]);
	(void)mb1(TAG_GET_BOARD_REV, 0u, &o[1]);
	kp("info firmware_rev=0x%08x (%u) board_rev=0x%08x ram=%s", o[0], o[0], o[1], boardMem(o[1]));
	memset(o, 0, sizeof(o));
	(void)mb(TAG_GET_ARM_MEMORY, 2u, o, 2u, o);
	kp("info arm_mem base=0x%08x size=0x%08x", o[0], o[1]);
	memset(o, 0, sizeof(o));
	(void)mb(TAG_GET_VC_MEMORY, 2u, o, 2u, o);
	kp("info vc_mem base=0x%08x size=0x%08x", o[0], o[1]);

	memset(o, 0, sizeof(o));
	in[0] = 0u;
	if (mb(TAG_GET_DISPLAY_CFG, 2u, in, 1u, o) == 0) {
		kp("info display_cfg max_pixel_clock0=%u max_pixel_clock1=%u", o[0], o[1]);
	}
	else {
		kp("info display_cfg rc=fail");
	}

	for (i = 0; i < g.numDisplays && i < 8u; i++) {
		uint32_t id = i;
		(void)mb1(TAG_FB_GET_DISPLAY_ID, i, &id);
		memset(o, 0, sizeof(o));
		in[0] = id & 0xffu;
		if (mb(TAG_GET_DISPLAY_TIMING, 9u, in, 1u, o) == 0) {
			kp("info timing idx=%u id=%u vic=%u clock_khz=%u h=%u/%u/%u/%u v=%u/%u/%u/%u vrefresh=%u flags=0x%x",
				i, id, o[0] >> 16, o[1], o[2] & 0xffffu, o[2] >> 16, o[3] & 0xffffu, o[3] >> 16, o[4] >> 16,
				o[5] & 0xffffu, o[5] >> 16, o[6] & 0xffffu, o[7] & 0xffffu, o[8]);
		}
		else {
			kp("info timing idx=%u id=%u rc=fail", i, id);
		}
	}

	memset(o, 0, sizeof(o));
	(void)mb(TAG_FB_GET_PHYS_WH, 2u, o, 2u, o);
	kp("info fb phys=%ux%u", o[0], o[1]);
	memset(o, 0, sizeof(o));
	(void)mb(TAG_FB_GET_VIRT_WH, 2u, o, 2u, o);
	kp("info fb virt=%ux%u", o[0], o[1]);
	memset(o, 0, sizeof(o));
	(void)mb1(TAG_FB_GET_DEPTH, 0u, &o[0]);
	(void)mb1(TAG_FB_GET_PIXEL_ORDER, 0u, &o[1]);
	(void)mb1(TAG_FB_GET_PITCH, 0u, &o[2]);
	kp("info fb depth=%u pixel_order=%u pitch=%u", o[0], o[1], o[2]);

	if (g.smi != NULL) {
		kp("info smi cs=0x%08x dsw0=0x%08x dsw1=0x%08x", g.smi[SMICS / 4u], g.smi[SMIDSW0 / 4u],
			g.smi[SMIDSW1 / 4u]);
	}
	if (g.gicd != NULL) {
		uint32_t r = SMI_IRQ_PHOENIX / 32u, bit = 1u << (SMI_IRQ_PHOENIX % 32u);
		kp("info gic irq=%u enabled=%d pending=%d active=%d", SMI_IRQ_PHOENIX,
			(g.gicd[GICD_ISENABLER(r) / 4u] & bit) != 0u, (g.gicd[GICD_ISPENDR(r) / 4u] & bit) != 0u,
			(g.gicd[GICD_ISACTIVER(r) / 4u] & bit) != 0u);
	}
	hvs_dump("info");
	{
		hvs_hit_t hit;
		if (g.fbPa != 0u) {
			hvs_findPa(g.fbPa, g.fbPitch * (g.fbVirtH ? g.fbVirtH : g.fbH), &hit);
			kp("info fb_ptr found=%d ch=%u idx=%u entry=%u word=0x%08x alias=0x%x expect_pa=0x%llx", hit.found,
				hit.ch, hit.idx, hit.entry, hit.word, hit.word >> 30,
				(unsigned long long)(g.fbPa + (uint64_t)g.fbYoff * g.fbPitch));
		}
	}
	return 0;
}


static uint32_t isqrt64(uint64_t v)
{
	uint64_t r = 0u, bit = 1ULL << 62;

	while (bit > v) {
		bit >>= 2;
	}
	while (bit != 0u) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		}
		else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return (uint32_t)r;
}


/* Interval statistics over the ISR timestamp ring. */
static void vb_stats(uint32_t from, uint32_t to, uint32_t *meanUs, uint32_t *jitUs, uint32_t *minUs,
	uint32_t *maxUs)
{
	uint64_t sum = 0u, sq = 0u;
	uint32_t i, n = 0u, mn = 0xffffffffu, mx = 0u;

	if ((to - from) > VB_RING) {
		from = to - VB_RING;
	}
	for (i = from + 1u; i < to; i++) {
		uint32_t d = (uint32_t)cnt_us(vb.ts[i % VB_RING] - vb.ts[(i - 1u) % VB_RING]);
		sum += d;
		sq += (uint64_t)d * d;
		mn = (d < mn) ? d : mn;
		mx = (d > mx) ? d : mx;
		n++;
	}
	if (n == 0u) {
		*meanUs = *jitUs = *minUs = *maxUs = 0u;
		return;
	}
	*meanUs = (uint32_t)(sum / n);
	{
		uint64_t mean = sum / n, ex2 = sq / n;
		*jitUs = isqrt64((ex2 > mean * mean) ? ex2 - mean * mean : 0u);
	}
	*minUs = mn;
	*maxUs = mx;
}


static int mode_vblank(unsigned int secs)
{
	uint32_t f0 = 0u, f1, hvsFrames, pollEvents = 0u, gicSeen = 0u, meanUs, jitUs, minUs, maxUs;
	uint32_t i, head0, count0, frLast = 0u, frAcc = 0u;
	uint64_t t0, t, lastTick, pollStart;
	int hvsCh = hvs_fbChannel(), irqOk = 0;
	uint32_t r = SMI_IRQ_PHOENIX / 32u, bit = 1u << (SMI_IRQ_PHOENIX % 32u);

	if (g.smi == NULL) {
		kp("vblank error=no_smi_mapping");
		return -1;
	}
	kp("vblank start secs=%u hvs_ch=%d smi_cs=0x%08x dsw0=0x%08x dsw1=0x%08x", secs, hvsCh, g.smi[SMICS / 4u],
		g.smi[SMIDSW0 / 4u], g.smi[SMIDSW1 / 4u]);

	/* Phase A: 1 s of polling with no handler registered. Separates "the
	 * firmware never raises the SMI bits" from "the IRQ does not reach us". */
	if (hvsCh >= 0) {
		f0 = hvs_frcnt((unsigned int)hvsCh);
	}
	head0 = vb.head;
	pollStart = t0 = cnt_now();
	while (((t = cnt_now()) - t0) < cnt_hz) {
		if ((g.gicd != NULL) && ((g.gicd[GICD_ISPENDR(r) / 4u] & bit) != 0u)) {
			gicSeen++;
		}
		if (smi_ack(t) >= 0) {
			pollEvents++;
		}
	}
	f1 = (hvsCh >= 0) ? hvs_frcnt((unsigned int)hvsCh) : 0u;
	hvsFrames = (f1 - f0) & 0x3fu;
	vb_stats(head0, vb.head, &meanUs, &jitUs, &minUs, &maxUs);
	kp("vblank poll ms=%llu smi_events=%u mean_us=%u jitter_us=%u gic_pending_samples=%u hvs_frames=%u "
	   "last_cs=0x%08x last_dsw0=0x%08x disp0=%u disp1=%u old_style=%u",
		(unsigned long long)(cnt_us(cnt_now() - pollStart) / 1000u), pollEvents, meanUs, jitUs, gicSeen, hvsFrames,
		vb.lastCs, vb.lastDsw0, vb.disp[0], vb.disp[1], vb.oldStyle);

	/* Phase B: interrupt-driven for `secs`. */
	if (vb_register() == 0) {
		head0 = vb.head;
		count0 = vb.count;
		t0 = lastTick = cnt_now();
		frLast = (hvsCh >= 0) ? hvs_frcnt((unsigned int)hvsCh) : 0u;
		while ((g_stop == 0) && (((t = cnt_now()) - t0) < (uint64_t)secs * cnt_hz)) {
			uint32_t seen = vb.head;
			(void)vs_wait(VS_IRQ, hvsCh, &seen);
			if (hvsCh >= 0) {
				uint32_t fr = hvs_frcnt((unsigned int)hvsCh);
				frAcc += (fr - frLast) & 0x3fu;
				frLast = fr;
			}
			if ((cnt_now() - lastTick) >= 2u * cnt_hz) {
				kp("tick t_s=%llu irq=%u hvs_frames=%u spurious=%u", (unsigned long long)(cnt_us(cnt_now() - t0) / 1000000u),
					vb.count - count0, frAcc, vb.spurious);
				if (vb_stormCheck()) {
					break;
				}
				lastTick = cnt_now();
			}
		}
		t = cnt_now() - t0;
		vb_stats(head0, vb.head, &meanUs, &jitUs, &minUs, &maxUs);
		irqOk = (vb.count - count0) > 0u;
		kp("vblank irq secs=%llu count=%u hz=%u.%02u mean_us=%u jitter_us=%u min_us=%u max_us=%u disp0=%u disp1=%u "
		   "old_style=%u spurious=%u hvs_frames=%u hvs_hz=%u",
			(unsigned long long)(cnt_us(t) / 1000000u), vb.count - count0,
			(uint32_t)(((uint64_t)(vb.count - count0) * 100u * cnt_hz / (t ? t : 1u)) / 100u),
			(uint32_t)(((uint64_t)(vb.count - count0) * 100u * cnt_hz / (t ? t : 1u)) % 100u), meanUs, jitUs, minUs,
			maxUs, vb.disp[0], vb.disp[1], vb.oldStyle, vb.spurious, frAcc,
			(uint32_t)((uint64_t)frAcc * cnt_hz / (t ? t : 1u)));
	}

	/* Phase C: the firmware's blocking wait-for-vsync tag, 30 calls. Refresh
	 * timing that needs neither the SMI IRQ nor the HVS. Blocks /dev/vcmbox
	 * for one frame per call. */
	{
		uint32_t ok = 0u, lat[30], z = 0u;
		uint64_t c0 = cnt_now(), c1;
		for (i = 0; (i < 30u) && (g_stop == 0); i++) {
			uint64_t a = cnt_now();
			if (mb(TAG_FB_SET_VSYNC, 1u, &z, 1u, NULL) == 0) {
				ok++;
			}
			lat[i] = (uint32_t)cnt_us(cnt_now() - a);
		}
		c1 = cnt_now() - c0;
		kp("vblank mbox_vsync n=%u ok=%u total_us=%llu per_call_us_p50=%u max=%u hz_est=%u", i, ok,
			(unsigned long long)cnt_us(c1), pctl(lat, i, 50), pctl(lat, i, 100),
			(uint32_t)((uint64_t)i * cnt_hz / (c1 ? c1 : 1u)));
	}
	kp("vblank verdict irq=%s poll=%s hvs=%s", irqOk ? "yes" : "no", (pollEvents > 0u) ? "yes" : "no",
		(hvsFrames > 0u) ? "yes" : "no");
	vb_unregister();
	return 0;
}


#define MAX_SAMPLES 16384u
static uint32_t s_lat[MAX_SAMPLES], s_lat2[MAX_SAMPLES], s_latch[MAX_SAMPLES];


/* How long after SET_PLANE returns does the new pointer appear in the
 * hardware display list? Polls up to 25 ms. Returns us, or UINT32_MAX. */
static uint32_t latchTime(const sbuf_t *b)
{
	uint64_t t0 = cnt_now();
	hvs_hit_t hit;

	if (g.hvs == NULL) {
		return 0xffffffffu;
	}
	do {
		hvs_findPa(b->pa, 64u, &hit);
		if (hit.found) {
			return (uint32_t)cnt_us(cnt_now() - t0);
		}
	} while (cnt_us(cnt_now() - t0) < 25000u);
	return 0xffffffffu;
}


static int mode_plane(unsigned int secs, int maxRate)
{
	sbuf_t buf[2], ov;
	fw_plane_t pp, po;
	uint32_t bus[2] = { 0u, 0u }, busOv = 0u, nLat = 0u, nLat2 = 0u, nLatch = 0u, latchMiss = 0u, errors = 0u;
	uint32_t flips = 0u, missed = 0u, seen, lastHead, vbStart, frAcc = 0u, frLast = 0u;
	int8_t primLayer = g.blankFb ? -127 : 0, ovLayer;
	uint64_t lat, t0, t, lastTick;
	int i, src, hvsCh = hvs_fbChannel(), rc0, rc1;
	int16_t ox = 0;
	hvs_hit_t hp, ho;
	const char *tag = maxRate ? "flipmax" : "plane";

	if (!g.blankFb) {
		primLayer = layerAboveFb();
	}
	ovLayer = (int8_t)(primLayer + 1);
	memset(buf, 0, sizeof(buf));
	memset(&ov, 0, sizeof(ov));
	for (i = 0; i < 2; i++) {
		if (sbuf_allocPref(&buf[i], g.w, g.h, GIB) != 0) {
			kp("%s error=alloc_failed w=%u h=%u", tag, g.w, g.h);
			goto out;
		}
		if (busAddr(&buf[i], g.bus, &bus[i]) != 0) {
			kp("%s error=pa_out_of_range buf=%d pa=0x%llx bus=%s (retry with the other -b or run `range`)", tag, i,
				(unsigned long long)buf[i].pa, busName(g.bus));
			goto out;
		}
		draw_primary(&buf[i], i);
	}
	if (!maxRate) {
		if ((sbuf_allocPref(&ov, 128u, 128u, GIB) != 0) || (busAddr(&ov, g.bus, &busOv) != 0)) {
			kp("%s error=overlay_alloc_or_range pa=0x%llx", tag, (unsigned long long)ov.pa);
			goto out;
		}
		draw_box(&ov, 0xff00ffu, 0xffffffu);
	}
	kp("%s buffers a_pa=0x%llx b_pa=0x%llx ov_pa=0x%llx a_bus=0x%08x b_bus=0x%08x len=%zu", tag,
		(unsigned long long)buf[0].pa, (unsigned long long)buf[1].pa, (unsigned long long)ov.pa, bus[0], bus[1],
		buf[0].len);

	if (g.blankFb) {
		kp("%s fb_blank rc=%d", tag, fb_blank(1));
	}

	/* First commit, then ask the hardware whether it took it. */
	plane_fill(&pp, 0u, primLayer, &buf[0], bus[0], 0, 0);
	rc0 = kms_setPlane(&pp, &lat);
	rc1 = 0;
	if (!maxRate) {
		plane_fill(&po, 1u, ovLayer, &ov, busOv, 0, (int16_t)(g.h / 4u));
		rc1 = kms_setPlane(&po, NULL);
	}
	usleep(100 * 1000);
	hvs_findPa(buf[0].pa, 64u, &hp);
	memset(&ho, 0, sizeof(ho));
	if (!maxRate) {
		hvs_findPa(ov.pa, 64u, &ho);
	}
	kp("%s first primary_rc=%d overlay_rc=%d first_lat_us=%llu primary_in_dlist=%d primary_word=0x%08x ch=%u "
	   "overlay_in_dlist=%d overlay_word=0x%08x fb_in_dlist=%d layer=%d transport=%s tail=%s",
		tag, rc0, rc1, (unsigned long long)cnt_us(lat), hp.found, hp.word, hp.ch, ho.found, ho.word, fbInDlist(),
		(int)primLayer, xportName(g.xport), g.tail);
	hvs_dump(tag);
	if (rc0 != 0) {
		errors++;
	}

	if (maxRate) {
		(void)vb_register(); /* count vblanks alongside the flips; failure is fine */
		src = -1;
	}
	else {
		src = vs_pick(hvsCh);
	}
	kp("%s start secs=%u vsync=%s transport=%s", tag, secs, maxRate ? "off" : vsName(src), xportName(g.xport));

	/* Two phases. Latch phase (first LATCH_FLIPS flips, not in the rates):
	 * after every flip, time how long until the new pointer is in the
	 * hardware list - this blocks for up to a frame, so it would pollute the
	 * flip-rate numbers. Steady phase: flip on every vblank, nothing else. */
	{
		enum { LATCH_FLIPS = 48 };
		uint32_t consecFail = 0u, total = 0u;

		for (i = 0; (i < LATCH_FLIPS) && (!maxRate) && (g_stop == 0); i++, total++) {
			int cur = (int)(total & 1u) ^ 1;
			uint32_t lt;

			if ((i % 16) == 0) {
				kp("tick latch_phase i=%d/%d", i, (int)LATCH_FLIPS);
			}
			if (vs_wait(src, hvsCh, &seen) < 0) {
				continue;
			}
			pp.planes[0] = bus[cur];
			if (kms_setPlane(&pp, NULL) != 0) {
				errors++;
				continue;
			}
			lt = latchTime(&buf[cur]);
			if (lt == 0xffffffffu) {
				latchMiss++;
			}
			else if (nLatch < MAX_SAMPLES) {
				s_latch[nLatch++] = lt;
			}
		}
		if (!maxRate) {
			uint32_t n3 = nLatch, l50 = pctl(s_latch, n3, 50), lmax = pctl(s_latch, n3, 100);
			kp("%s latch samples=%u notseen=%u latch_us_p50=%u max=%u (SET_PLANE return -> pointer in HVS list)", tag,
				n3, latchMiss, l50, lmax);
		}

		vbStart = vb.count;
		seen = lastHead = vb.head;
		if (hvsCh >= 0) {
			frLast = hvs_frcnt((unsigned int)hvsCh);
		}
		t0 = lastTick = cnt_now();
		while ((g_stop == 0) && (((t = cnt_now()) - t0) < (uint64_t)secs * cnt_hz)) {
			int cur = (int)(total & 1u) ^ 1;
			int doFlip = 1;

			if (!maxRate) {
				if (vs_wait(src, hvsCh, &seen) < 0) {
					missed++;
					doFlip = 0;
					if (++consecFail >= 5u) {
						int old = src;
						src = ((src == VS_IRQ) && (hvsCh >= 0)) ? VS_HVS : VS_TIMER;
						kp("%s vsync_fallback from=%s to=%s", tag, vsName(old), vsName(src));
						consecFail = 0u;
					}
				}
				else {
					consecFail = 0u;
					/* IRQ source: vblanks that passed while the previous flip was still
					 * in flight are frames this loop did not keep up with. */
					if ((src == VS_IRQ) && (flips > 0u) && ((seen - lastHead) > 1u)) {
						missed += seen - lastHead - 1u;
					}
					lastHead = seen;
				}
			}
			if (doFlip) {
				pp.planes[0] = bus[cur];
				if (kms_setPlane(&pp, &lat) != 0) {
					errors++;
				}
				else if (nLat < MAX_SAMPLES) {
					s_lat[nLat++] = (uint32_t)cnt_us(lat);
				}
				if (!maxRate) {
					ox = (int16_t)((ox + 4) % (int)((g.w > 128u) ? g.w - 128u : 1u));
					po.dst_x = ox;
					if (kms_setPlane(&po, &lat) != 0) {
						errors++;
					}
					else if (nLat2 < MAX_SAMPLES) {
						s_lat2[nLat2++] = (uint32_t)cnt_us(lat);
					}
				}
				flips++;
				total++;
			}
			if (hvsCh >= 0) {
				uint32_t fr = hvs_frcnt((unsigned int)hvsCh);
				frAcc += (fr - frLast) & 0x3fu;
				frLast = fr;
			}
			if ((cnt_now() - lastTick) >= 2u * cnt_hz) {
				kp("tick t_s=%llu flips=%u irq=%u hvs_frames=%u missed=%u errors=%u x=%d",
					(unsigned long long)(cnt_us(cnt_now() - t0) / 1000000u), flips, vb.count - vbStart, frAcc, missed,
					errors, (int)ox);
				if (vb_stormCheck() && (src == VS_IRQ)) {
					src = (hvsCh >= 0) ? VS_HVS : VS_TIMER;
				}
				lastTick = cnt_now();
			}
		}
	}
	t = cnt_now() - t0;
	{
		uint32_t n1 = nLat, n2 = nLat2;
		uint32_t p50 = pctl(s_lat, n1, 50), p99 = pctl(s_lat, n1, 99), pmax = pctl(s_lat, n1, 100);
		uint32_t o50 = pctl(s_lat2, n2, 50), o99 = pctl(s_lat2, n2, 99);
		kp("%s result secs=%llu flips=%u flips_per_s=%u vsync=%s vblanks_irq=%u hvs_frames=%u hvs_hz=%u missed=%u "
		   "setplane_us_p50=%u p99=%u max=%u overlay_us_p50=%u p99=%u errors=%u transport=%s tail=%s",
			tag, (unsigned long long)(cnt_us(t) / 1000000u), flips, (uint32_t)((uint64_t)flips * cnt_hz / (t ? t : 1u)),
			maxRate ? "off" : vsName(src), vb.count - vbStart, frAcc, (uint32_t)((uint64_t)frAcc * cnt_hz / (t ? t : 1u)),
			missed, p50, p99, pmax, o50, o99, errors, xportName(g.xport), g.tail);
	}

out:
	restore_all();
	vb_unregister();
	sbuf_free(&buf[0]);
	sbuf_free(&buf[1]);
	sbuf_free(&ov);
	return 0;
}


/* Hold for `secs`, printing a heartbeat so the harness idle timer never
 * cuts the command. */
static void hold(unsigned int secs, const char *what)
{
	unsigned int s;

	for (s = 0; (s < secs) && (g_stop == 0); s++) {
		if ((s % 2u) == 0u) {
			kp("tick hold=%s s=%u/%u", what, s, secs);
		}
		sleep(1);
	}
}


static void stackReport(int step, const char *desc, int rc, const sbuf_t *a, const sbuf_t *b)
{
	hvs_hit_t ha, hb;

	hvs_findPa(a->pa, 64u, &ha);
	hvs_findPa(b->pa, 64u, &hb);
	kp("stack step=%d desc=%s rc=%d green_in_dlist=%d word=0x%08x blue_in_dlist=%d word=0x%08x fb_in_dlist=%d "
	   "entries=%u,%u,%u tail=%s",
		step, desc, rc, ha.found, ha.word, hb.found, hb.word, fbInDlist(), ha.entries[0], ha.entries[1],
		ha.entries[2], g.tail);
}


/* What happens to the firmware fb (fbcon) around our planes, and where does
 * a layer -127 plane land relative to it? Visual + dlist, one step per hold. */
static int mode_stack(unsigned int holdSecs)
{
	sbuf_t green, blue;
	fw_plane_t p1, p0;
	uint32_t bg, bb;
	int rc;

	memset(&green, 0, sizeof(green));
	memset(&blue, 0, sizeof(blue));
	if ((sbuf_allocPref(&green, 256u, 256u, GIB) != 0) || (sbuf_allocPref(&blue, 256u, 256u, GIB) != 0) ||
			(busAddr(&green, g.bus, &bg) != 0) || (busAddr(&blue, g.bus, &bb) != 0)) {
		kp("stack error=alloc_or_range green_pa=0x%llx blue_pa=0x%llx", (unsigned long long)green.pa,
			(unsigned long long)blue.pa);
		goto out;
	}
	draw_box(&green, 0x00c000u, 0xffffffu);
	draw_box(&blue, 0x0000e0u, 0xffff00u);
	kp("stack start hold=%u green_pa=0x%llx blue_pa=0x%llx", holdSecs, (unsigned long long)green.pa,
		(unsigned long long)blue.pa);

	plane_fill(&p1, 1u, 1, &green, bg, 64, 64);
	rc = kms_setPlane(&p1, NULL);
	usleep(100 * 1000);
	stackReport(1, "green_plane1_layer1_at_64_64", rc, &green, &blue);
	hold(holdSecs, "step1");
	if (g_stop) {
		goto out;
	}

	plane_fill(&p0, 0u, -127, &blue, bb, 384, 64);
	rc = kms_setPlane(&p0, NULL);
	usleep(100 * 1000);
	stackReport(2, "blue_plane0_layer-127_at_384_64", rc, &green, &blue);
	hvs_dump("stack2");
	hold(holdSecs, "step2");
	if (g_stop) {
		goto out;
	}

	rc = fb_blank(1);
	usleep(100 * 1000);
	stackReport(3, "fb_blanked", rc, &green, &blue);
	hold(holdSecs, "step3");
	if (g_stop) {
		goto out;
	}

	rc = fb_blank(0);
	usleep(100 * 1000);
	stackReport(4, "fb_unblanked", rc, &green, &blue);
	hold(holdSecs, "step4");
	if (g_stop) {
		goto out;
	}

	rc = kms_unsetPlane(p0.plane_id);
	rc |= kms_unsetPlane(p1.plane_id);
	usleep(100 * 1000);
	stackReport(5, "planes_unset", rc, &green, &blue);
	hvs_dump("stack5");
	hold(holdSecs, "step5");

out:
	restore_all();
	sbuf_free(&green);
	sbuf_free(&blue);
	return 0;
}


/* Show `b` full-screen with each eligible bus convention, one hold each.
 * Colour pairs per (target, convention) are listed in the E3 doc so a
 * snapshot can be attributed to its trial: base 0 = lo/any/<pa>, 2 = hi,
 * 4 = fb1; +0 = c0 convention, +1 = 00 convention. */
static void rangeTry(sbuf_t *b, const char *where, unsigned int holdSecs, int base)
{
	static const uint32_t col[6][2] = {
		{ 0xff0000u, 0xffffffu }, /* lo  c0: red / white      */
		{ 0x0000ffu, 0xffff00u }, /* lo  00: blue / yellow    */
		{ 0x00ff00u, 0xff00ffu }, /* hi  c0: green / magenta  */
		{ 0x00ffffu, 0x000000u }, /* hi  00: cyan / black     */
		{ 0xff8000u, 0x000000u }, /* fb1 c0: orange / black   */
		{ 0xffffffu, 0x8000ffu }, /* fb1 00: white / purple   */
	};
	int conv;

	for (conv = BUS_C0; conv >= BUS_00; conv--) {
		fw_plane_t p;
		uint32_t bus;
		uint64_t lat;
		hvs_hit_t hit;
		int rc, k = base + ((conv == BUS_00) ? 1 : 0);

		if (busAddr(b, conv, &bus) != 0) {
			kp("range try where=%s conv=%s pa=0x%llx skipped=out_of_convention_range", where, busName(conv),
				(unsigned long long)b->pa);
			continue;
		}
		draw_checker(b, col[k][0], col[k][1]);
		plane_fill(&p, 0u, layerAboveFb(), b, bus, 0, 0);
		rc = kms_setPlane(&p, &lat);
		usleep(100 * 1000);
		hvs_findPa(b->pa, 64u, &hit);
		kp("range try where=%s conv=%s pa=0x%llx bus=0x%08x rc=%d lat_us=%llu in_dlist=%d dlist_word=0x%08x "
		   "word_alias=0x%x fb_in_dlist=%d colours=%06x/%06x hold=%u tail=%s",
			where, busName(conv), (unsigned long long)b->pa, bus, rc, (unsigned long long)cnt_us(lat), hit.found,
			hit.word, hit.word >> 30, fbInDlist(), col[k][0], col[k][1], holdSecs, g.tail);
		hold(holdSecs, where);
		(void)kms_unsetPlane(p.plane_id);
		usleep(100 * 1000);
	}
}


static int mode_range(const char *want, unsigned int holdSecs)
{
	enum { MAXCH = 48 };
	static sbuf_t ch[MAXCH];
	int n = 0, sel = -1, i;
	uint64_t paMin = 0u, paMax = 0x100000000ULL;

	if (strcmp(want, "fb1") == 0) {
		/* Control: the firmware fb's own second buffer - known scan-out capable. */
		sbuf_t b;
		if ((g.fbPa == 0u) || (g.fbVirtH < 2u * g.fbH)) {
			kp("range fb1 unavailable fb_pa=0x%llx virt_h=%u h=%u", (unsigned long long)g.fbPa, g.fbVirtH, g.fbH);
			return -1;
		}
		if (sbuf_physmem(&b, g.fbPa + (uint64_t)g.fbPitch * g.fbH, g.fbW, g.fbH, g.fbPitch) != 0) {
			kp("range fb1 map_failed");
			return -1;
		}
		kp("range start where=fb1 pa=0x%llx", (unsigned long long)b.pa);
		rangeTry(&b, "fb1", holdSecs, 4);
		sbuf_unmapPhys(&b);
		restore_all();
		return 0;
	}
	if (strcmp(want, "lo") == 0) {
		paMax = GIB;
	}
	else if (strcmp(want, "hi") == 0) {
		paMin = GIB;
	}
	else if (strcmp(want, "any") != 0) {
		paMin = strtoull(want, NULL, 0);
	}

	/* The buddy allocator gives no placement control: take chunks until one
	 * lands in the window (bounded), then hand the rest back. */
	for (n = 0; (n < MAXCH) && (g_stop == 0); n++) {
		uint64_t end;
		if (sbuf_alloc(&ch[n], g.w, g.h) != 0) {
			kp("range alloc_stop at=%d", n);
			break;
		}
		end = ch[n].pa + (uint64_t)ch[n].pitch * ch[n].h;
		kp("range chunk i=%d pa=0x%llx end=0x%llx", n, (unsigned long long)ch[n].pa, (unsigned long long)end);
		if ((ch[n].pa >= paMin) && (end <= paMax)) {
			sel = n;
			n++;
			break;
		}
	}
	for (i = 0; i < n; i++) {
		if (i != sel) {
			sbuf_free(&ch[i]);
		}
	}
	if (sel < 0) {
		kp("range none_in_window want=%s min=0x%llx max=0x%llx chunks=%d", want, (unsigned long long)paMin,
			(unsigned long long)paMax, n);
		return -1;
	}
	kp("range start where=%s pa=0x%llx chunks_taken=%d", want, (unsigned long long)ch[sel].pa, n);
	rangeTry(&ch[sel], want, holdSecs, (paMin >= GIB) ? 2 : 0);
	restore_all();
	sbuf_free(&ch[sel]);
	return 0;
}


/* How much contiguous scan-out memory is realistic right now? */
static int mode_contig(unsigned int capMiB)
{
	static const unsigned int sizes[] = { 256u, 128u, 64u, 32u, 16u };
	enum { MAXC = 128 };
	static void *va[MAXC];
	unsigned int i, n = 0u, lo = 0u, mid = 0u, hi = 0u;
	meminfo_t mi;

	memset(&mi, 0, sizeof(mi));
	mi.page.mapsz = -1;
	mi.entry.mapsz = -1;
	mi.entry.kmapsz = -1;
	mi.maps.mapsz = -1;
	meminfo(&mi);
	kp("contig meminfo page_free=%u page_alloc=%u page_sz=%u", mi.page.free, mi.page.alloc, mi.page.sz);

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		size_t len = (size_t)sizes[i] * MIB;
		void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_CONTIGUOUS | MAP_UNCACHED | MAP_ANONYMOUS, -1, 0);
		if (p == MAP_FAILED) {
			kp("contig single mib=%u ok=0", sizes[i]);
			continue;
		}
		*(volatile uint32_t *)p = 0u;
		kp("contig single mib=%u ok=1 pa=0x%llx", sizes[i], (unsigned long long)va2pa(p));
		(void)munmap(p, len);
	}

	/* 8 MiB = one 1080p XRGB buffer (8,294,400 B rounds up to 8 MiB). */
	for (n = 0; (n < MAXC) && (n * 8u < capMiB) && (g_stop == 0); n++) {
		uint64_t pa;
		va[n] = mmap(NULL, 8u * MIB, PROT_READ | PROT_WRITE, MAP_CONTIGUOUS | MAP_UNCACHED | MAP_ANONYMOUS, -1, 0);
		if (va[n] == MAP_FAILED) {
			break;
		}
		*(volatile uint32_t *)va[n] = 0u;
		pa = (uint64_t)va2pa(va[n]);
		if (pa + 8u * MIB <= GIB) {
			lo++;
		}
		else if (pa + 8u * MIB <= 0x100000000ULL) {
			mid++;
		}
		else {
			hi++;
		}
		if ((n % 8u) == 0u) {
			kp("contig chunk8 i=%u pa=0x%llx", n, (unsigned long long)pa);
		}
	}
	kp("contig chunk8 total=%u cap_mib=%u below_1g=%u 1g_to_4g=%u above_4g=%u", n, capMiB, lo, mid, hi);
	for (i = 0; i < n; i++) {
		(void)munmap(va[i], 8u * MIB);
	}
	return 0;
}


static int mode_restore(void)
{
	unsigned int i;

	/* After a crash nothing is recorded; unset every plane of the display. */
	for (i = 0; i < PLANES_PER_DISPLAY; i++) {
		int rc = kms_unsetPlane((uint8_t)(i + g.dispIdx * PLANES_PER_DISPLAY));
		kp("restore unset plane=%u rc=%d", i + g.dispIdx * PLANES_PER_DISPLAY, rc);
	}
	g.fbBlanked = 1; /* force the unblank */
	g.dirty = 1;
	restore_all();
	return 0;
}


static void usage(void)
{
	printf("usage: kmsprobe [-t auto|xl|raw60|v48] [-b 00|c0] [-d display_index] [-B] [-P bounce_pa] <mode> [args]\n"
		   "  info                   displays, timings, firmware fb, SMI/GIC state, HVS display lists\n"
		   "  vblank [secs=10]       SMI vblank: 1 s poll, IRQ for secs, 30x mailbox vsync\n"
		   "  stack [hold=8]         firmware fb vs our planes (5 visual steps)\n"
		   "  plane [secs=20]        full-screen primary flipping each vblank + moving overlay\n"
		   "  flipmax [secs=10]      SET_PLANE as fast as possible, vsync off (E4)\n"
		   "  range lo|hi|any|fb1|<pa_min> [hold=10]   scan-out from a chosen physical range (E6)\n"
		   "  contig [cap_mib=512]   largest contiguous allocation + 8 MiB chunks by range\n"
		   "  restore                unset all 8 planes of the display, unblank the fb\n"
		   "  -b 00: planes[] = CPU PA (Linux fkms convention, default); c0: 0xC0000000|PA (soc alias)\n"
		   "  -B: blank the firmware fb and put the primary at layer -127 (as Linux fkms does)\n"
		   "  -P: /dev/vcmbox bounce PA for the raw60/v48 tail check (default: found in /dev/kmsg)\n");
}


int main(int argc, char **argv)
{
	const char *mode;
	int a = 1, rc;

	while ((a < argc) && (argv[a][0] == '-')) {
		if ((strcmp(argv[a], "-t") == 0) && (a + 1 < argc)) {
			const char *t = argv[++a];
			g.xport = (strcmp(t, "xl") == 0) ? XPORT_XL : (strcmp(t, "raw60") == 0) ? XPORT_RAW60 :
				(strcmp(t, "v48") == 0) ? XPORT_V48 : XPORT_AUTO;
		}
		else if ((strcmp(argv[a], "-b") == 0) && (a + 1 < argc)) {
			g.bus = (strcmp(argv[++a], "c0") == 0) ? BUS_C0 : BUS_00;
		}
		else if ((strcmp(argv[a], "-d") == 0) && (a + 1 < argc)) {
			g.dispIdx = (unsigned int)strtoul(argv[++a], NULL, 0);
		}
		else if (strcmp(argv[a], "-B") == 0) {
			g.blankFb = 1;
		}
		else if ((strcmp(argv[a], "-P") == 0) && (a + 1 < argc)) {
			g.bouncePaOpt = strtoull(argv[++a], NULL, 0);
		}
		else {
			usage();
			return 1;
		}
		a++;
	}
	if (a >= argc) {
		usage();
		return 1;
	}
	mode = argv[a++];

	if (common_init(1, 1) != 0) {
		kp("error=no_display_mode");
		return 2;
	}
	atexit(restore_all);

	if (strcmp(mode, "info") == 0) {
		rc = mode_info();
	}
	else if (strcmp(mode, "vblank") == 0) {
		rc = mode_vblank((a < argc) ? (unsigned int)atoi(argv[a]) : 10u);
	}
	else if (strcmp(mode, "stack") == 0) {
		rc = mode_stack((a < argc) ? (unsigned int)atoi(argv[a]) : 8u);
	}
	else if (strcmp(mode, "plane") == 0) {
		rc = mode_plane((a < argc) ? (unsigned int)atoi(argv[a]) : 20u, 0);
	}
	else if (strcmp(mode, "flipmax") == 0) {
		rc = mode_plane((a < argc) ? (unsigned int)atoi(argv[a]) : 10u, 1);
	}
	else if (strcmp(mode, "range") == 0) {
		rc = mode_range((a < argc) ? argv[a] : "lo", (a + 1 < argc) ? (unsigned int)atoi(argv[a + 1]) : 10u);
	}
	else if (strcmp(mode, "contig") == 0) {
		rc = mode_contig((a < argc) ? (unsigned int)atoi(argv[a]) : 512u);
	}
	else if (strcmp(mode, "restore") == 0) {
		rc = mode_restore();
	}
	else {
		usage();
		return 1;
	}
	kp("done mode=%s rc=%d%s", mode, rc, g_stop ? " (interrupted)" : "");
	return (rc == 0) ? 0 : 3;
}
