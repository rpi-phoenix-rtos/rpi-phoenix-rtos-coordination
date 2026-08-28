/*
 * hevc-m2 — Raspberry Pi 4 (BCM2711) HEVC/H.265 hardware decoder M2: decode ONE
 * IDR frame with the rpivid/hevc_dec block, no VCHIQ, no V4L2.
 *
 * Faithful port of the single-slice IDR decode path from the Linux driver
 *   external/linux/drivers/media/platform/raspberrypi/hevc_dec/hevc_d_h265.c
 * for the minimal test vector tools/hevc-decode/testdata/idr64.265 (64x64, 8-bit,
 * 4:2:0, single-tile, no-WPP, no-temporal-mvp, no-SAO, I-slice). Field values are
 * host-parsed (testdata/README.md) and hardcoded so the device needs no HEVC parser.
 *
 * Flow (see docs/inprogress/2026-08-28-hevc-m2-register-spec.md):
 *   M0 preamble (clock+map+version) → alloc contiguous/uncached DMA buffers (M1) →
 *   build the phase-1 DMA COMMAND BUFFER of u64 (off | data<<32) entries in
 *   decode_slice() last_slice order → phase-1 kick (CFBASE) → poll ARGON ACTIVE1,
 *   check CFSTATUS==CFNUM → phase-2 program + kick (NUMROWS) → poll ACTIVE2 →
 *   output buffer valid (SAND/COL128 tiled; linear unpack + pixel compare = M4).
 *
 * First-light uses ICTRL polling for ACTIVE1/ACTIVE2 (spec §6) rather than wiring
 * IRQ 130 (M1 proved the IRQ path; the decode itself needs no ISR).
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "libvcmbox.h"
#include "hevc_regs.h"
/* Frame header: default idr64 (flat), override with -DFRAME_HEADER='"detail64_frame.h"'
 * for the real-content vector. Each header defines FRAME_* params, slice_data[], and
 * EXPECTED_Y(x,y)/EXPECTED_C(x,y) (scalar golden for flat, ref-array for real content). */
#ifndef FRAME_HEADER
#define FRAME_HEADER "idr64_frame.h"
#endif
#include FRAME_HEADER

/* VideoCore firmware property tags (mailbox channel 8). */
#define VC_GET_MAX_CLOCK_RATE 0x00030004u
#define VC_SET_CLOCK_STATE    0x00038001u
#define VC_SET_CLOCK_RATE     0x00038002u
#define VC_GET_CLOCK_RATE     0x00030002u
#define RPI_FIRMWARE_HEVC_CLK_ID 11u

static inline uint32_t rd(const volatile void *a) { return *(const volatile uint32_t *)a; }
static inline void wr(volatile void *a, uint32_t v) { *(volatile uint32_t *)a = v; }

/* ---- Contiguous, uncached DMA buffer (M1 idiom). ---- */
typedef struct { void *cpu; addr_t pa; size_t size; } dma_buf_t;

static int dma_alloc(dma_buf_t *b, size_t size)
{
	size_t pg = (size_t)sysconf(_SC_PAGESIZE);
	size = (size + pg - 1u) & ~(pg - 1u);
	b->size = size;
	b->cpu = mmap(NULL, size, PROT_READ | PROT_WRITE,
		MAP_UNCACHED | MAP_CONTIGUOUS | MAP_ANONYMOUS, -1, 0);
	if (b->cpu == MAP_FAILED) { b->cpu = NULL; return -ENOMEM; }
	memset(b->cpu, 0, size);
	b->pa = va2pa(b->cpu);
	return 0;
}

/* round_up_size (hevc_d_video.c:38): x>=256 -> n=ilog2(x); x>=3<<n ? 4<<n : 3<<n. */
static uint32_t round_up_size(uint32_t x)
{
	uint32_t n = 0, t = x;
	if (x < 256u) return 256u;
	while (t > 1u) { t >>= 1; n++; }              /* n = ilog2(x) */
	return (x >= (3u << n)) ? (4u << n) : (3u << n);
}

/* ---- Phase-1 command buffer: each entry = off | (data<<32). ---- */
static uint64_t *g_cmd;
static uint32_t g_cmd_len;
static void p1(uint16_t off, uint32_t data) { g_cmd[g_cmd_len++] = (uint64_t)off | ((uint64_t)data << 32); }

/* CABAC init probabilities (hevc_d_h265.c:467). Row = init_type: 0=I, 1=P (no
 * cabac_init), 2=B (no cabac_init); cabac_init_flag swaps rows 1<->2. */
static const uint8_t prob_init[3][156] = {
	{
	153, 200, 139, 141, 157, 154, 154, 154, 154, 154, 184, 154, 154,
	154, 184, 63,  154, 154, 154, 154, 154, 154, 154, 154, 154, 154,
	154, 154, 154, 153, 138, 138, 111, 141, 94,  138, 182, 154, 154,
	154, 140, 92,  137, 138, 140, 152, 138, 139, 153, 74,  149, 92,
	139, 107, 122, 152, 140, 179, 166, 182, 140, 227, 122, 197, 110,
	110, 124, 125, 140, 153, 125, 127, 140, 109, 111, 143, 127, 111,
	79,  108, 123, 63,  110, 110, 124, 125, 140, 153, 125, 127, 140,
	109, 111, 143, 127, 111, 79,  108, 123, 63,  91,  171, 134, 141,
	138, 153, 136, 167, 152, 152, 139, 139, 111, 111, 125, 110, 110,
	94,  124, 108, 124, 107, 125, 141, 179, 153, 125, 107, 125, 141,
	179, 153, 125, 107, 125, 141, 179, 153, 125, 140, 139, 182, 182,
	152, 136, 152, 136, 153, 136, 139, 111, 136, 139, 111, 0,   0,
	},
	{
	153, 185, 107, 139, 126, 197, 185, 201, 154, 149, 154, 139, 154,
	154, 154, 152, 110, 122, 95,  79,  63,  31,  31,  153, 153, 168,
	140, 198, 79,  124, 138, 94,  153, 111, 149, 107, 167, 154, 154,
	154, 154, 196, 196, 167, 154, 152, 167, 182, 182, 134, 149, 136,
	153, 121, 136, 137, 169, 194, 166, 167, 154, 167, 137, 182, 125,
	110, 94,  110, 95,  79,  125, 111, 110, 78,  110, 111, 111, 95,
	94,  108, 123, 108, 125, 110, 94,  110, 95,  79,  125, 111, 110,
	78,  110, 111, 111, 95,  94,  108, 123, 108, 121, 140, 61,  154,
	107, 167, 91,  122, 107, 167, 139, 139, 155, 154, 139, 153, 139,
	123, 123, 63,  153, 166, 183, 140, 136, 153, 154, 166, 183, 140,
	136, 153, 154, 166, 183, 140, 136, 153, 154, 170, 153, 123, 123,
	107, 121, 107, 121, 167, 151, 183, 140, 151, 183, 140, 0,   0,
	},
	{
	153, 160, 107, 139, 126, 197, 185, 201, 154, 134, 154, 139, 154,
	154, 183, 152, 154, 137, 95,  79,  63,  31,  31,  153, 153, 168,
	169, 198, 79,  224, 167, 122, 153, 111, 149, 92,  167, 154, 154,
	154, 154, 196, 167, 167, 154, 152, 167, 182, 182, 134, 149, 136,
	153, 121, 136, 122, 169, 208, 166, 167, 154, 152, 167, 182, 125,
	110, 124, 110, 95,  94,  125, 111, 111, 79,  125, 126, 111, 111,
	79,  108, 123, 93,  125, 110, 124, 110, 95,  94,  125, 111, 111,
	79,  125, 126, 111, 111, 79,  108, 123, 93,  121, 140, 61,  154,
	107, 167, 91,  107, 107, 167, 139, 139, 170, 154, 139, 153, 139,
	123, 123, 63,  124, 166, 183, 140, 136, 153, 154, 166, 183, 140,
	136, 153, 154, 166, 183, 140, 136, 153, 154, 170, 153, 138, 138,
	122, 121, 122, 121, 167, 151, 183, 140, 151, 183, 140, 0,   0,
	},
};

/* write_prob (h265.c:514) — init_type: I=0, P=1, B=2 (no cabac_init). q=clamp(qp,0,51). */
static void emit_prob(int slice_qp, int init_type)
{
	int q = slice_qp < 0 ? 0 : (slice_qp > 51 ? 51 : slice_qp);
	const uint8_t *p = prob_init[init_type];
	uint8_t dst[156];
	for (int i = 0; i < 154; i++) {
		int v = p[i];
		int m = (v >> 4) * 5 - 45;
		int n = ((v & 15) << 3) - 16;
		int pre = 2 * (((m * q) >> 4) + n) - 127;
		pre ^= pre >> 31;                       /* abs via sign-smear */
		if (pre > 124) pre = 124 + (pre & 1);
		dst[i] = (uint8_t)pre;
	}
	dst[154] = dst[155] = 0;
	for (int i = 0; i < 156; i += 4)
		p1(RPI_PROBBASE + i, dst[i] | (dst[i+1] << 8) | (dst[i+2] << 16) | ((uint32_t)dst[i+3] << 24));
	p1(RPI_TRANSFER, PROB_BACKUP);
}

/* Build the full phase-1 command buffer for the single IDR I-slice
 * (decode_slice last_slice path, h265.c:1149). Returns cmd_len. */
/* slice_const_arg: 0 => compute the I-slice const from FRAME_SLICE_TYPE; nonzero =>
 * use it verbatim (P/B slice_reg_const). num_msgs/msgs: the slice-message array
 * (0 for I; P/B emit cmd_slice + per-ref descriptors). */
static uint32_t build_command_buffer(addr_t bs_pa, uint32_t dbo, uint32_t bfnum, int slice_qp,
				     uint32_t slice_const_arg, uint32_t num_msgs, const uint16_t *msgs)
{
	const uint32_t ctb = FRAME_LOG2_CTB;             /* log2 CTB size (6 => 64) */
	const uint32_t ctb_w = FRAME_CTB_WIDTH;          /* pic width in CTBs (1) */
	const uint32_t ctb_h = FRAME_CTB_HEIGHT;         /* pic height in CTBs (1) */
	const uint32_t cs = 1u << ctb;

	g_cmd_len = 0;

	/* 1) pre_slice_decode: I-slice emits ZERO slice messages. */

	/* 2) write_bitstream (h265.c:573). addr = bs_pa + data_byte_offset. */
	addr_t addr = bs_pa + dbo;
	uint32_t off = (uint32_t)(addr & 63);
	p1(RPI_BFBASE, RPI_VC_ADDR(addr));
	p1(RPI_BFNUM, bfnum);
	p1(RPI_BFCONTROL, off + RPI_BFCONTROL_STOP);
	p1(RPI_BFCONTROL, off + RPI_BFCONTROL_EMU);      /* V4L2 stream keeps emu-prevention bytes */

	/* 3) write_prob (CABAC). init_type from slice type: I=0; P/B (no cabac_init) = 2-slice_type.
	 * slice_type lives in bits 12+ of slice_const_arg (P/B); I uses the computed I const => 0. */
	int slice_type = slice_const_arg ? (int)((slice_const_arg >> 12) & 0xf) : (int)FRAME_SLICE_TYPE;
	int init_type = (slice_type == 2 /*I*/) ? 0 : (2 - slice_type);
	emit_prob(slice_qp, init_type);

	/* 4) program_slicecmds (h265.c:697): SLICECMDS = num_msgs + (sliceid 0 <<8), then
	 * the message array at 0x4000+4*i. I-slice: num_msgs=0. P/B: cmd_slice + ref descs. */
	p1(RPI_SLICECMDS, num_msgs);
	for (uint32_t i = 0; i < num_msgs; i++)
		p1(RPI_SLICEMSGBASE + 4u * i, msgs[i]);

	/* 5) new_slice_segment: SPS0/SPS1/PPS/SLICESTART (h265.c:613). */
	p1(RPI_SPS0,
	   ((FRAME_LOG2_MIN_CB) << 0) |               /* log2_min_luma_coding_block_size (=min_minus3+3) */
	   (ctb << 4) |
	   ((FRAME_LOG2_MIN_TB) << 8) |               /* log2_min_luma_transform_block_size (=min_minus2+2) */
	   ((FRAME_LOG2_MAX_TB) << 12) |              /* = min_tb + diff_max_min_tb */
	   (8u << 16) |                               /* bit_depth_luma = 8 */
	   (8u << 20) |                               /* bit_depth_chroma = 8 */
	   ((uint32_t)FRAME_MAX_TRAFO_INTRA << 24) |
	   ((uint32_t)FRAME_MAX_TRAFO_INTER << 28));
	p1(RPI_SPS1,
	   (1u << 0) | (1u << 4) | (3u << 8) | (3u << 12) | /* PCM depths/sizes: pcm disabled => minus1+1=1, minus3+3=3 */
	   ((uint32_t)FRAME_CHROMA_FORMAT_IDC << 16) |
	   (0u << 18) |                               /* AMP disabled */
	   (0u << 19) |                               /* PCM disabled */
	   (0u << 20) |                               /* scaling-list disabled */
	   ((uint32_t)FRAME_STRONG_INTRA_SMOOTH << 21));
	p1(RPI_PPS,
	   ((ctb - FRAME_DIFF_CU_QP_DELTA_DEPTH) << 0) |
	   (FRAME_CU_QP_DELTA_ENABLED << 4) |
	   (0u << 5) |                                /* transquant-bypass disabled */
	   (FRAME_TRANSFORM_SKIP << 6) |
	   (FRAME_SIGN_DATA_HIDING << 7) |
	   (((FRAME_PPS_CB_QP_OFFSET + FRAME_SLICE_CB_QP_OFFSET) & 255) << 8) |
	   (((FRAME_PPS_CR_QP_OFFSET + FRAME_SLICE_CR_QP_OFFSET) & 255) << 16) |
	   (FRAME_CONSTRAINED_INTRA_PRED << 24));
	/* no scaling factors (scaling-list disabled) */
	p1(RPI_SLICESTART, 0);                        /* slice_segment_addr 0 -> ctb (0,0) */

	/* 6) new_entry_point (h265.c:911). single tile spanning the whole frame, at (0,0),
	 * do_bte, reset_qp_y, PAUSE_MODE_TILE. The tile end is the last CTB: for one tile
	 * col_bd={0,ctb_w} row_bd={0,ctb_h}, so endx=col_bd[1]-1=ctb_w-1, endy=ctb_h-1
	 * (multi-CTB frames need this — hardcoding 0 only decodes the first CTB). */
	uint32_t endx = ctb_w - 1, endy = ctb_h - 1;
	p1(RPI_TILESTART, 0);                         /* col_bd[0] | row_bd[0]<<16 */
	p1(RPI_TILEEND, endx | (endy << 16));
	p1(RPI_BEGINTILEEND, endx | (endy << 16));    /* do_bte */
	/* write_slice (h265.c:884): slice_const | wlast<<17 | hlast<<24. */
	uint32_t w_last = FRAME_WIDTH & (cs - 1);
	uint32_t h_last = FRAME_HEIGHT & (cs - 1);
	uint32_t slice_const = slice_const_arg ? slice_const_arg :
		((0u << 0) | (0u << 4) | (0u << 8) | (FRAME_SLICE_TYPE << 12)); /* I: merge/refs 0, no SAO */
	p1(RPI_SLICE,
	   slice_const |
	   ((endx + 1 < ctb_w || !w_last ? cs : w_last) << 17) |
	   ((endy + 1 < ctb_h || !h_last ? cs : h_last) << 24));
	p1(RPI_QP, 6u * FRAME_BIT_DEPTH_LUMA_MINUS8 + slice_qp); /* reset_qp_y */
	p1(RPI_MODE, RPI_MODE_TILE |
	   ((endx == ctb_w - 1) ? RPI_MODE_LASTCOL : 0) |
	   ((endy == ctb_h - 1) ? RPI_MODE_LASTROW : 0));
	p1(RPI_CONTROL, 0);                           /* (ctb_col 0)|(ctb_row 0<<16) */

	/* 7) last_slice: final RPI_STATUS end marker. */
	p1(RPI_STATUS, 1u | ((ctb_w - 1) << 5) | ((ctb_h - 1) << 18));

	return g_cmd_len;
}

/* ---- MMIO + clock (M0). ---- */
static void *map_block(uint32_t base, uint32_t size)
{
	return mmap(NULL, size, PROT_READ | PROT_WRITE,
		MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (off_t)base);
}

#define VC_GET_CLOCK_STATE 0x00030001u
static int hevc_clock_enable(uint32_t *rate_out)
{
	uint32_t in[3], out[2] = {0, 0}, max_rate = 0, set_rate = 0, cfg = 0, st = 0, st2 = 0;
	in[0] = RPI_FIRMWARE_HEVC_CLK_ID;
	if (vcmbox_call(VC_GET_MAX_CLOCK_RATE, 8u, in, 1u, out, 2u) != 0) return -1;
	max_rate = out[1]; if (max_rate == 0u) return -1;
	in[0] = RPI_FIRMWARE_HEVC_CLK_ID; in[1] = 1u;
	if (vcmbox_call(VC_SET_CLOCK_STATE, 8u, in, 2u, out, 2u) != 0) return -1;
	st = out[1];  /* bit0 = on/off, bit1 = "clock not present" */
	in[0] = RPI_FIRMWARE_HEVC_CLK_ID; in[1] = max_rate; in[2] = 0u;
	if (vcmbox_call(VC_SET_CLOCK_RATE, 12u, in, 3u, out, 2u) != 0) return -1;
	set_rate = out[1];
	in[0] = RPI_FIRMWARE_HEVC_CLK_ID;
	if (vcmbox_call(VC_GET_CLOCK_STATE, 8u, in, 1u, out, 2u) == 0) st2 = out[1];
	in[0] = RPI_FIRMWARE_HEVC_CLK_ID;
	if (vcmbox_call(VC_GET_CLOCK_RATE, 8u, in, 1u, out, 2u) == 0) cfg = out[1];
	printf("hevc-m2: clock SET_STATE->0x%x GET_STATE->0x%x (on=%d present=%d) rate set=%u cfg=%u\n",
		st, st2, (st2 & 1u), !(st2 & 2u), set_rate, cfg);
	*rate_out = cfg ? cfg : set_rate;
	return (*rate_out == 0u) ? -1 : 0;
}

/* Poll the ARGON INTC for an ACTIVE bit; ack (write ictrl & ~SET_ZERO_MASK). */
static int poll_active(volatile uint8_t *intc, uint32_t bit, int timeout_ms)
{
	for (int i = 0; i < timeout_ms * 100; i++) {
		uint32_t ic = rd(intc + ARG_IC_ICTRL);
		if (ic & bit) { wr(intc + ARG_IC_ICTRL, ic & ~SET_ZERO_MASK); return 0; }
		{ struct timespec ts = { 0, 10000 }; nanosleep(&ts, NULL); } /* 10us */
	}
	return -1;
}

/* rpi4-fb GETMODE ABI (video/rpi4-fb/rpi4-fb.h). */
typedef struct { uint16_t width, height, bpp, pitch; uint64_t smemlen, framebuffer; } fbmode_t;
#define FB_GETMODE _IOR('g', 1, fbmode_t)

static inline uint8_t clip8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v); }

/* Best-effort: unpack the SAND/COL128 decode, convert NV12->RGBA (BT.601 limited
 * range), and blit the frame centered onto /dev/fb0 (R8G8B8A8, the proven scanout
 * format). No-op if fb0 is unavailable — the headless pixel check already ran. */
/* Blit one decoded frame (SAND/COL128 -> NV12 -> RGBA, BT.601) centered into a
 * mapped R8G8B8A8 framebuffer. */
static void fb_blit(uint8_t *fb, uint32_t pitch, uint32_t fbw, uint32_t fbh,
		    const uint8_t *yb, const uint8_t *cbb, uint32_t W, uint32_t H,
		    uint32_t luma_stride, uint32_t chroma_stride)
{
	uint32_t x0 = (fbw > W) ? (fbw - W) / 2u : 0;
	uint32_t y0 = (fbh > H) ? (fbh - H) / 2u : 0;
	for (uint32_t y = 0; y < H && (y0 + y) < fbh; y++) {
		for (uint32_t x = 0; x < W && (x0 + x) < fbw; x++) {
			int Y = yb[(x / 128u) * luma_stride + y * 128u + (x % 128u)];
			uint32_t cxb = (x & ~1u), cy = y / 2u;   /* NV12: Cb at even col, Cr next */
			int U = cbb[(cxb / 128u) * chroma_stride + cy * 128u + (cxb % 128u)];
			int V = cbb[(((cxb + 1) / 128u)) * chroma_stride + cy * 128u + ((cxb + 1) % 128u)];
			int C = Y - 16, D = U - 128, E = V - 128;
			uint8_t *px = fb + (uint64_t)(y0 + y) * pitch + (uint64_t)(x0 + x) * 4u;
			px[0] = clip8((298 * C + 409 * E + 128) >> 8);          /* R */
			px[1] = clip8((298 * C - 100 * D - 208 * E + 128) >> 8);/* G */
			px[2] = clip8((298 * C + 516 * D + 128) >> 8);          /* B */
			px[3] = 0xff;                                           /* A */
		}
	}
}

/* Map /dev/fb0 (R8G8B8A8). Returns the mapping (and fills *m) or NULL. */
static uint8_t *fb_open(fbmode_t *m, int *fd_out)
{
	int fd = open("/dev/fb0", O_RDWR);
	if (fd < 0) { printf("hevc-m2: /dev/fb0 unavailable — skipping HDMI display\n"); return NULL; }
	memset(m, 0, sizeof(*m));
	if (ioctl(fd, FB_GETMODE, m) != 0 || m->framebuffer == 0 || m->bpp != 32) {
		printf("hevc-m2: fb0 GETMODE failed — skipping display\n"); close(fd); return NULL;
	}
	uint8_t *fb = mmap(NULL, m->smemlen, PROT_READ | PROT_WRITE,
		MAP_PHYSMEM | MAP_UNCACHED | MAP_ANONYMOUS, -1, (off_t)m->framebuffer);
	if (fb == MAP_FAILED) { printf("hevc-m2: fb mmap failed\n"); close(fd); return NULL; }
	*fd_out = fd;
	return fb;
}

/* One-shot display of a single decoded frame (single-frame path; unused in clip mode). */
static void __attribute__((unused)) hevc_show(const uint8_t *yb, const uint8_t *cbb,
		      uint32_t W, uint32_t H, uint32_t luma_stride, uint32_t chroma_stride)
{
	fbmode_t m; int fd;
	uint8_t *fb = fb_open(&m, &fd);
	if (!fb) return;
	fb_blit(fb, m.pitch, m.width, m.height, yb, cbb, W, H, luma_stride, chroma_stride);
	printf("hevc-m2: displayed %ux%u decoded frame on fb0 (%ux%u)\n", W, H, m.width, m.height);
	munmap(fb, m.smemlen);
	close(fd);
}

/* Decode one all-intra frame (independent IDR I-slice) into luma/chroma using the
 * pre-mapped block + pre-allocated buffers. Copies the slice NAL (data_byte_offset +
 * bfnum bytes) into bs, builds + runs both HW phases. Returns 0 on success. */
static int decode_one(volatile uint8_t *hevc, volatile uint8_t *intc,
		      dma_buf_t *cmd, dma_buf_t *bs, dma_buf_t *pu, dma_buf_t *coeff,
		      dma_buf_t *luma, dma_buf_t *chroma, const uint8_t *slice, uint32_t dbo, uint32_t bfnum,
		      int slice_qp, uint32_t slice_const, uint32_t num_msgs, const uint16_t *msgs,
		      const uint32_t (*refpa)[2], uint32_t currpoc,
		      uint32_t pu_stride, uint32_t coeff_stride, uint32_t luma_stride,
		      uint32_t chroma_stride, int verbose)
{
	memcpy(bs->cpu, slice, dbo + bfnum);
	g_cmd = (uint64_t *)cmd->cpu;
	uint32_t clen = build_command_buffer(bs->pa, dbo, bfnum, slice_qp, slice_const, num_msgs, msgs);

	/* Barrier: the bitstream + command buffer are written through the uncached
	 * CPU mapping; ensure those writes have drained to DRAM before the block's DMA
	 * reads them at the CFBASE kick below. Without this the block can read a stale
	 * command buffer -> non-deterministic phase-1/phase-2 stalls. (The original
	 * inline code got away without it because printf()s between build and kick
	 * provided incidental drain delay; the refactor removed them.) */
	__sync_synchronize();

	wr(hevc + RPI_PUWBASE, RPI_VC_ADDR(pu->pa));
	wr(hevc + RPI_PUWSTRIDE, RPI_VC_LEN(pu_stride));
	wr(hevc + RPI_COEFFWBASE, RPI_VC_ADDR(coeff->pa));
	wr(hevc + RPI_COEFFWSTRIDE, RPI_VC_LEN(coeff_stride));
	wr(hevc + RPI_CFNUM, clen);
	wr(hevc + RPI_CFBASE, RPI_VC_ADDR(cmd->pa));       /* STARTS PHASE 1 */
	if (poll_active(intc, ACTIVE1_INT_SET, 500) != 0) { if (verbose) printf("hevc-m2: PHASE 1 TIMEOUT\n"); return -5; }
	uint32_t cfstatus = rd(hevc + RPI_CFSTATUS), cfnum = rd(hevc + RPI_CFNUM);
	if (verbose) printf("hevc-m2: phase 1 done: CFSTATUS=%u CFNUM=%u %s\n", cfstatus, cfnum,
		cfstatus == cfnum ? "[OK]" : "[MISMATCH]");
	if (cfstatus != cfnum) {
		uint32_t st = rd(hevc + RPI_STATUS);
		printf("hevc-m2: CF-MISMATCH CFSTATUS=%u CFNUM=%u RPI_STATUS=0x%x (PU_EXH=%d COEFF_EXH=%d)\n",
			cfstatus, cfnum, st, !!(st & RPI_STATUS_PU_EXHAUSTED), !!(st & RPI_STATUS_COEFF_EXHAUSTED));
		return -1;
	}

	wr(hevc + RPI_PURBASE, RPI_VC_ADDR(pu->pa));
	wr(hevc + RPI_PURSTRIDE, RPI_VC_LEN(pu_stride));
	wr(hevc + RPI_COEFFRBASE, RPI_VC_ADDR(coeff->pa));
	wr(hevc + RPI_COEFFRSTRIDE, RPI_VC_LEN(coeff_stride));
	wr(hevc + RPI_OUTYBASE, RPI_VC_ADDR(luma->pa));
	wr(hevc + RPI_OUTCBASE, RPI_VC_ADDR(chroma->pa));
	wr(hevc + RPI_OUTYSTRIDE, RPI_VC_LEN(luma_stride));
	wr(hevc + RPI_OUTCSTRIDE, RPI_VC_LEN(chroma_stride));
	for (uint32_t i = 0; i < 16; i++) {              /* refpa!=NULL: DPB refs; else all = current frame */
		uint32_t roff = i * RPI_REFREGS_SIZE;
		uint32_t yp = refpa ? refpa[i][0] : (uint32_t)luma->pa;
		uint32_t cp = refpa ? refpa[i][1] : (uint32_t)chroma->pa;
		wr(hevc + RPI_REFBASE + roff + RPI_REF_YBASE, RPI_VC_ADDR(yp));
		wr(hevc + RPI_REFBASE + roff + RPI_REF_YSTRIDE, RPI_VC_LEN(luma_stride));
		wr(hevc + RPI_REFBASE + roff + RPI_REF_CBASE, RPI_VC_ADDR(cp));
		wr(hevc + RPI_REFBASE + roff + RPI_REF_CSTRIDE, RPI_VC_LEN(chroma_stride));
	}
	wr(hevc + RPI_CONFIG2, FRAME_CONFIG2);
	wr(hevc + RPI_FRAMESIZE, (FRAME_HEIGHT << 16) | FRAME_WIDTH);
	wr(hevc + RPI_CURRPOC, currpoc);
	wr(hevc + RPI_COLSTRIDE, RPI_VC_LEN(luma_stride));
	wr(hevc + RPI_MVSTRIDE, RPI_VC_LEN(luma_stride));
	wr(hevc + RPI_MVBASE, 0);
	wr(hevc + RPI_COLBASE, 0);
	wr(hevc + RPI_NUMROWS, FRAME_CTB_HEIGHT);         /* STARTS PHASE 2 */
	if (poll_active(intc, ACTIVE2_INT_SET, 1500) != 0) {
		if (verbose) {
			uint32_t ic = rd(intc + ARG_IC_ICTRL), st = rd(hevc + RPI_STATUS);
			const uint8_t *y = luma->cpu; uint32_t nz = 0;
			for (uint32_t i = 0; i < 4096; i++) if (y[i]) nz++;
			printf("hevc-m2: PHASE 2 TIMEOUT ICTRL=0x%08x STATUS=0x%x out_nz=%u pu_pa=0x%08llx coeff_pa=0x%08llx luma_pa=0x%08llx\n",
				ic, st, nz, (unsigned long long)pu->pa, (unsigned long long)coeff->pa, (unsigned long long)luma->pa);
		}
		return -6;
	}
	return 0;
}

int main(void)
{
	void *hevc_p, *intc_p;
	volatile uint8_t *hevc, *intc;
	uint32_t rate = 0, ver;
	dma_buf_t cmd = {0}, bs = {0}, pu = {0}, coeff = {0}, luma = {0}, chroma = {0};

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("hevc-m2: single-IDR %ux%u hardware decode\n", FRAME_WIDTH, FRAME_HEIGHT);

	if (hevc_clock_enable(&rate) != 0) { printf("hevc-m2: clock enable FAILED\n"); return 2; }
	printf("hevc-m2: HEVC clock %u Hz\n", rate);
	hevc_p = map_block(HEVC_BASE, HEVC_SIZE); if (hevc_p == MAP_FAILED) { printf("hevc-m2: map HEVC FAILED\n"); return 3; }
	intc_p = map_block(INTC_BASE, INTC_SIZE); if (intc_p == MAP_FAILED) { printf("hevc-m2: map INTC FAILED\n"); return 3; }
	hevc = hevc_p; intc = intc_p;
	ver = rd(hevc + RPI_VERSION);
	printf("hevc-m2: RPI_VERSION = 0x%x\n", ver);
	if (ver != HEVC_EXPECT_VER) { printf("hevc-m2: version mismatch — abort\n"); return 1; }

	/* ARGON INTC: enable + clear pending (hw_setup). */
	wr(intc + ARG_IC_ICTRL, ACTIVE1_EN_SET | ACTIVE2_EN_SET);
	wr(intc + ARG_IC_ICTRL, rd(intc + ARG_IC_ICTRL));

	/* Buffers (allocated once; reused across frames in clip mode). */
	uint32_t wh = FRAME_WIDTH * FRAME_HEIGHT;
	uint32_t pu_size = round_up_size(wh / 4u), coeff_size = round_up_size(wh);
	uint32_t luma_stride = ((FRAME_HEIGHT + 15u) & ~15u) * 128u;   /* NV12MT_COL128 */
	uint32_t chroma_stride = luma_stride / 2u;
	uint32_t cols = ((FRAME_WIDTH + 127u) & ~127u) / 128u;
#ifdef CLIP_NFRAMES
	size_t bs_size = 256u * 1024u;                    /* covers the largest clip frame NAL */
#else
	size_t bs_size = sizeof(slice_data) + 128u;
#endif
	if (dma_alloc(&cmd, 64u * 1024u) || dma_alloc(&bs, bs_size) ||
	    dma_alloc(&pu, pu_size) || dma_alloc(&coeff, coeff_size) ||
	    dma_alloc(&luma, luma_stride * cols + 4096u) || dma_alloc(&chroma, chroma_stride * cols + 4096u)) {
		printf("hevc-m2: DMA alloc FAILED\n"); return 4;
	}
	uint32_t pu_stride = (pu_size / FRAME_CTB_HEIGHT) & ~63u;
	uint32_t coeff_stride = (coeff_size / FRAME_CTB_HEIGHT) & ~63u;
	printf("hevc-m2: buffers pu=%u coeff=%u luma_stride=%u cols=%u\n", pu_size, coeff_size, luma_stride, cols);

#if defined(IP_TEST)
	/* Inter-prediction test: decode frame0 (I) into luma/chroma (kept as the DPB
	 * reference), then frame1 (P) into lumaB/chromaB referencing it; verify P bit-exact. */
	{
	dma_buf_t lumaB = {0}, chromaB = {0};
	if (dma_alloc(&lumaB, luma_stride * cols + 4096u) || dma_alloc(&chromaB, chroma_stride * cols + 4096u)) {
		printf("hevc-m2: DMA alloc (P buffers) FAILED\n"); return 4;
	}
	int r0 = decode_one(hevc, intc, &cmd, &bs, &pu, &coeff, &luma, &chroma,
		slice_data, FRAME_DATA_BYTE_OFFSET, FRAME_DATA_LEN, FRAME_SLICE_QP,
		0, 0, NULL, NULL, 0, pu_stride, coeff_stride, luma_stride, chroma_stride, 1);
	printf("hevc-m2: I frame (POC 0) rc=%d\n", r0);
	if (r0 != 0) return 6;
	/* P ref slots: [0] = the decoded I frame; [1..15] = current (fallback). */
	uint32_t refpa[16][2];
	for (int i = 0; i < 16; i++) { refpa[i][0] = (uint32_t)lumaB.pa; refpa[i][1] = (uint32_t)chromaB.pa; }
	refpa[0][0] = (uint32_t)luma.pa; refpa[0][1] = (uint32_t)chroma.pa;
	int r1 = decode_one(hevc, intc, &cmd, &bs, &pu, &coeff, &lumaB, &chromaB,
		p_slice_nal, P_DBO, P_BFNUM, P_SLICE_QP,
		P_SLICE_CONST, P_NUM_MSGS, p_msgs, refpa, P_CURRPOC,
		pu_stride, coeff_stride, luma_stride, chroma_stride, 1);
	printf("hevc-m2: P frame (POC 1) rc=%d\n", r1);
	if (r1 != 0) return 7;
	const uint8_t *yb = lumaB.cpu;
	uint32_t y_ok = 0, y_bad = 0, y_min = 255, y_max = 0;
	for (uint32_t y = 0; y < FRAME_HEIGHT; y++)
		for (uint32_t x = 0; x < FRAME_WIDTH; x++) {
			uint8_t v = yb[(x / 128u) * luma_stride + y * 128u + (x % 128u)];
			if (v < y_min) y_min = v;
			if (v > y_max) y_max = v;
			if (v == ref_luma_p[y * FRAME_WIDTH + x]) y_ok++; else y_bad++;
		}
	printf("hevc-m2: P luma %u/%u match golden (min %u max %u)\n", y_ok, FRAME_WIDTH * FRAME_HEIGHT, y_min, y_max);
	printf("hevc-m2: IP-TEST %s\n", y_bad == 0 ?
		"P FRAME BIT-EXACT — inter-prediction works" : "P frame differs from golden");
	return y_bad ? 7 : 0;
	}
#elif defined(CLIP_NFRAMES)
	/* All-intra video playback: decode + display each frame in sequence. Two passes so
	 * the periodic HDMI snapshot is very likely to land on a mid-clip frame (= motion). */
	fbmode_t fbm; int fbfd = -1; uint8_t *fb = fb_open(&fbm, &fbfd);
	printf("hevc-m2: playing %d-frame all-intra %ux%u clip%s\n", CLIP_NFRAMES,
		FRAME_WIDTH, FRAME_HEIGHT, fb ? " on HDMI" : " (headless — no fb0)");
	uint32_t shown = 0;
	const int passes = 6;                    /* replay so a periodic HDMI snapshot lands mid-clip */
	for (int loop = 0; loop < passes; loop++) {
		for (int f = 0; f < CLIP_NFRAMES; f++) {
			int rc = decode_one(hevc, intc, &cmd, &bs, &pu, &coeff, &luma, &chroma,
				clip_data + clip_frames[f].off, FRAME_DATA_BYTE_OFFSET, clip_frames[f].bfnum, clip_frames[f].qp,
				0, 0, NULL, NULL, 0,   /* I-slice: I const, 0 msgs, all-current refs, POC 0 */
				pu_stride, coeff_stride, luma_stride, chroma_stride, 0);
			if (rc != 0) { printf("hevc-m2: frame %d decode failed rc=%d\n", f, rc); continue; }
			if (fb) fb_blit(fb, fbm.pitch, fbm.width, fbm.height, luma.cpu, chroma.cpu,
					FRAME_WIDTH, FRAME_HEIGHT, luma_stride, chroma_stride);
			shown++;
			{ struct timespec ts = { 0, 40000000 }; nanosleep(&ts, NULL); }  /* ~25 fps */
		}
	}
	if (fb) { munmap(fb, fbm.smemlen); close(fbfd); }
	printf("hevc-m2: clip done — decoded+displayed %u/%u frames\n", shown, (unsigned)(passes * CLIP_NFRAMES));
	return shown ? 0 : 7;
#else
	/* Single frame: decode (verbose), verify bit-exact vs golden, display. */
	printf("hevc-m2: buffers cmd_pa=0x%08llx bs_pa=0x%08llx\n",
		(unsigned long long)cmd.pa, (unsigned long long)bs.pa);
	int drc = decode_one(hevc, intc, &cmd, &bs, &pu, &coeff, &luma, &chroma,
		slice_data, FRAME_DATA_BYTE_OFFSET, FRAME_DATA_LEN, FRAME_SLICE_QP, 0, 0, NULL, NULL, 0,
		pu_stride, coeff_stride, luma_stride, chroma_stride, 1);
	if (drc != 0) { printf("hevc-m2: decode FAILED rc=%d\n", drc); return 6; }
	printf("hevc-m2: phase 2 done (ACTIVE2) — frame decoded\n");

	/* M4 verification: unpack SAND/COL128 -> linear and compare to the golden
	 * ffmpeg SW decode. COL128 single-column layout for W<=128: a pixel (x,y)
	 * lives at column-block (x/128)*<col-stride> + y*128 + (x%128); here the frame
	 * is one 128-wide block so luma(x,y) = luma_buf[y*128 + x]. */
	const uint8_t *yb = luma.cpu, *cb = chroma.cpu;
	uint32_t y_ok = 0, y_min = 255, y_max = 0, y_bad = 0;
	for (uint32_t y = 0; y < FRAME_HEIGHT; y++) {
		for (uint32_t x = 0; x < FRAME_WIDTH; x++) {
			uint8_t v = yb[(x / 128u) * luma_stride + y * 128u + (x % 128u)];
			if (v < y_min) y_min = v;
			if (v > y_max) y_max = v;
			if (v == EXPECTED_Y(x, y)) y_ok++; else y_bad++;
		}
	}
	/* NV12 chroma: interleaved CbCr, 4:2:0 -> W/2 x H/2 pairs = W bytes/row, H/2 rows. */
	uint32_t c_ok = 0, c_bad = 0, c_min = 255, c_max = 0;
	for (uint32_t y = 0; y < FRAME_HEIGHT / 2u; y++) {
		for (uint32_t x = 0; x < FRAME_WIDTH; x++) {
			uint8_t v = cb[(x / 128u) * chroma_stride + y * 128u + (x % 128u)];
			if (v < c_min) c_min = v;
			if (v > c_max) c_max = v;
			if (v == EXPECTED_C(x, y)) c_ok++; else c_bad++;
		}
	}
	uint32_t y_tot = FRAME_WIDTH * FRAME_HEIGHT, c_tot = FRAME_WIDTH * (FRAME_HEIGHT / 2u);
	printf("hevc-m2: luma   %u/%u match golden  (min %u max %u)\n", y_ok, y_tot, y_min, y_max);
	printf("hevc-m2: chroma %u/%u match golden  (min %u max %u)\n", c_ok, c_tot, c_min, c_max);

	/* Best-effort HDMI display of the decoded frame (visible end-to-end proof). */
	hevc_show(yb, cb, FRAME_WIDTH, FRAME_HEIGHT, luma_stride, chroma_stride);

	int exact = (y_bad == 0 && c_bad == 0);
	printf("hevc-m2: M4 %s\n", exact ?
		"EXACT MATCH — HW decode == ffmpeg SW decode (bit-exact)" :
		"decoded but pixels differ from golden (see counts above)");
	return exact ? 0 : 7;
#endif
}
