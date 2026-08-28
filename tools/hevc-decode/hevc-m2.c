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
#ifdef PLAY_TOOL
/* Runtime file player: geometry + per-frame params come from the bitstream at run
 * time (hevc_parse.*), so we include only the FIXED subset constants, not a
 * build-time frame header with a baked slice_data[]/golden. */
#include "hevc_parse.h"
#include "play_subset.h"
#include <stdlib.h>
#else
/* Frame header: default idr64 (flat), override with -DFRAME_HEADER='"detail64_frame.h"'
 * for the real-content vector. Each header defines FRAME_* params, slice_data[], and
 * EXPECTED_Y(x,y)/EXPECTED_C(x,y) (scalar golden for flat, ref-array for real content). */
#ifndef FRAME_HEADER
#define FRAME_HEADER "idr64_frame.h"
#endif
#include FRAME_HEADER
#endif

/* VideoCore firmware property tags (mailbox channel 8). */
#define VC_GET_MAX_CLOCK_RATE 0x00030004u
#define VC_SET_CLOCK_STATE    0x00038001u
#define VC_SET_CLOCK_RATE     0x00038002u
#define VC_GET_CLOCK_RATE     0x00030002u
#define RPI_FIRMWARE_HEVC_CLK_ID 11u

static inline uint32_t rd(const volatile void *a) { return *(const volatile uint32_t *)a; }
static inline void wr(volatile void *a, uint32_t v) { *(volatile uint32_t *)a = v; }

/* Full system barrier before a DMA doorbell. The command buffer + bitstream are
 * written through a Normal-Non-Cacheable CPU mapping; the rpivid block fetches
 * them as a NON-COHERENT SYSTEM (outer) DMA master. __sync_synchronize() emits
 * only `dmb ish` (inner-shareable), which does NOT order those NC writes against a
 * system-domain device before the doorbell store launches the fetch. `dsb sy`
 * waits for all prior accesses to complete to the endpoint. Without it, a heavy
 * Normal-NC store burst just before a frame (e.g. fb_blit's ~500K framebuffer
 * stores during video playback) leaves the decode inputs still in the store/
 * write-combine buffers when the block starts reading -> stale input -> an
 * intermittent wrong frame that then poisons the ping-pong reference chain. */
static inline void hevc_dma_fence(void) { __asm__ volatile("dsb sy" ::: "memory"); }

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

/* Runtime frame geometry. The only stream parameters that vary within the x265
 * subset are the resolution + its CTB grid; everything else the register values
 * encode is fixed (see play_subset.h). The build-time modes set these from their
 * FRAME_* macros at main() entry (behaviour identical to the old direct-macro
 * code); PLAY_TOOL sets them from the parsed SPS. */
static uint32_t g_frame_w, g_frame_h, g_ctb_w, g_ctb_h;

/* Build the full phase-1 command buffer for the single IDR I-slice
 * (decode_slice last_slice path, h265.c:1149). Returns cmd_len. */
/* slice_const_arg: 0 => compute the I-slice const from FRAME_SLICE_TYPE; nonzero =>
 * use it verbatim (P/B slice_reg_const). num_msgs/msgs: the slice-message array
 * (0 for I; P/B emit cmd_slice + per-ref descriptors). */
static uint32_t build_command_buffer(addr_t bs_pa, uint32_t dbo, uint32_t bfnum, int slice_qp,
				     uint32_t slice_const_arg, uint32_t num_msgs, const uint16_t *msgs)
{
	const uint32_t ctb = FRAME_LOG2_CTB;             /* log2 CTB size (6 => 64) */
	const uint32_t ctb_w = g_ctb_w;                  /* pic width in CTBs (runtime) */
	const uint32_t ctb_h = g_ctb_h;                  /* pic height in CTBs (runtime) */
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
	uint32_t w_last = g_frame_w & (cs - 1);
	uint32_t h_last = g_frame_h & (cs - 1);
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
	/* Drain any prior Normal-NC store burst (notably fb_blit's ~500K framebuffer
	 * writes during playback) to the endpoint BEFORE writing this frame's inputs,
	 * so a backed-up store/write-combine buffer can't interleave with the bitstream
	 * memcpy / command-buffer build below. */
	hevc_dma_fence();
	memcpy(bs->cpu, slice, dbo + bfnum);
	g_cmd = (uint64_t *)cmd->cpu;
	uint32_t clen = build_command_buffer(bs->pa, dbo, bfnum, slice_qp, slice_const, num_msgs, msgs);

	/* Order the NC command-buffer/bitstream writes above so the block observes them
	 * before the CFBASE doorbell fetches them (see hevc_dma_fence: dsb sy, not the
	 * inner-shareable dmb ish that __sync_synchronize would emit). */
	hevc_dma_fence();

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
	wr(hevc + RPI_FRAMESIZE, (g_frame_h << 16) | g_frame_w);
	wr(hevc + RPI_CURRPOC, currpoc);
	/* Collocated-MV strides MUST be 0 when temporal-MVP is off (driver: colmv_stride
	 * stays 0 unless setup_colmv runs, phase2_claimed writes 0). A non-zero stride with
	 * base 0 engages the colMV path on an inter frame -> first MV txn targets addr 0 ->
	 * AXI hang (I-frames are inert since they have no MVs). This was the P phase-2 hang. */
	wr(hevc + RPI_COLSTRIDE, 0);
	wr(hevc + RPI_MVSTRIDE, 0);
	wr(hevc + RPI_MVBASE, 0);
	wr(hevc + RPI_COLBASE, 0);
	hevc_dma_fence();                                 /* same ordering before the phase-2 doorbell */
	wr(hevc + RPI_NUMROWS, g_ctb_h);                  /* STARTS PHASE 2 */
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

/* Bring the HEVC block up: enable clock, map MMIO + ARGON INTC, check the version
 * register, enable + clear the INTC. Fills hevc_out and intc_out; returns 0 or a
 * nonzero exit code. Shared by the build-time modes and the runtime player. */
static int hevc_hw_up(volatile uint8_t **hevc_out, volatile uint8_t **intc_out)
{
	uint32_t rate = 0, ver;
	if (hevc_clock_enable(&rate) != 0) { printf("hevc-m2: clock enable FAILED\n"); return 2; }
	printf("hevc-m2: HEVC clock %u Hz\n", rate);
	void *hevc_p = map_block(HEVC_BASE, HEVC_SIZE);
	if (hevc_p == MAP_FAILED) { printf("hevc-m2: map HEVC FAILED\n"); return 3; }
	void *intc_p = map_block(INTC_BASE, INTC_SIZE);
	if (intc_p == MAP_FAILED) { printf("hevc-m2: map INTC FAILED\n"); return 3; }
	volatile uint8_t *hevc = hevc_p, *intc = intc_p;
	ver = rd(hevc + RPI_VERSION);
	printf("hevc-m2: RPI_VERSION = 0x%x\n", ver);
	if (ver != HEVC_EXPECT_VER) { printf("hevc-m2: version mismatch — abort\n"); return 1; }
	/* ARGON INTC: enable + clear pending (hw_setup). */
	wr(intc + ARG_IC_ICTRL, ACTIVE1_EN_SET | ACTIVE2_EN_SET);
	wr(intc + ARG_IC_ICTRL, rd(intc + ARG_IC_ICTRL));
	*hevc_out = hevc; *intc_out = intc;
	return 0;
}

#ifndef PLAY_TOOL
int main(void)
{
	volatile uint8_t *hevc, *intc;
	dma_buf_t cmd = {0}, bs = {0}, pu = {0}, coeff = {0}, luma = {0}, chroma = {0};

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("hevc-m2: single-IDR %ux%u hardware decode\n", FRAME_WIDTH, FRAME_HEIGHT);
	g_frame_w = FRAME_WIDTH; g_frame_h = FRAME_HEIGHT;
	g_ctb_w = FRAME_CTB_WIDTH; g_ctb_h = FRAME_CTB_HEIGHT;

	int hrc = hevc_hw_up(&hevc, &intc);
	if (hrc) return hrc;

	/* Buffers (allocated once; reused across frames in clip mode). */
	uint32_t wh = FRAME_WIDTH * FRAME_HEIGHT;
	uint32_t pu_size = round_up_size(wh / 4u), coeff_size = round_up_size(wh);
	uint32_t luma_stride = ((FRAME_HEIGHT + 15u) & ~15u) * 128u;   /* NV12MT_COL128 */
	uint32_t chroma_stride = luma_stride / 2u;
	uint32_t cols = ((FRAME_WIDTH + 127u) & ~127u) / 128u;
#if defined(CLIP_NFRAMES) || defined(IPPP_TEST) || defined(IPB_TEST)
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
#elif defined(IPB_TEST)
	/* Minimal bidirectional B: decode I,P (anchors) then a B referencing BOTH the
	 * past anchor (L0) and the future anchor (L1). With temporal-MVP off the phase-2
	 * setup is identical to P; only phase-1 differs (B slice_const 0x0113 + a 7-msg
	 * stream carrying L0 then L1 ref descriptors). Verify every frame bit-exact. */
	{
	dma_buf_t cl = {0}, cc = {0}, bl = {0}, bc = {0};   /* P anchor + B scratch */
	if (dma_alloc(&cl, luma_stride * cols + 4096u) || dma_alloc(&cc, chroma_stride * cols + 4096u) ||
	    dma_alloc(&bl, luma_stride * cols + 4096u) || dma_alloc(&bc, chroma_stride * cols + 4096u)) {
		printf("hevc-m2: DMA alloc (IPB) FAILED\n"); return 4;
	}
	/* POC-keyed anchor slots: [0]=I (luma/chroma), [1]=P (cl/cc). */
	struct { uint32_t poc; dma_buf_t *l, *c; } dpb[2] = {
		{ ipb_frames[0].poc, &luma, &chroma },
		{ ipb_frames[1].poc, &cl,   &cc },
	};
	uint32_t nbad = 0;
	for (int f = 0; f < 3; f++) {
		const struct ipb_frame *fr = &ipb_frames[f];
		dma_buf_t *ol = &bl, *oc = &bc;                 /* B decodes to scratch */
		if (fr->stype == 2) { ol = &luma; oc = &chroma; }
		else if (fr->stype == 1) { ol = &cl; oc = &cc; }
		uint32_t refpa[16][2];
		for (int i = 0; i < 16; i++) { refpa[i][0] = (uint32_t)ol->pa; refpa[i][1] = (uint32_t)oc->pa; }
		uint32_t slice_const = 0, nmsg = 0;
		uint16_t msgs[7];
		if (fr->stype == 1) {                            /* P: ref[0] = L0 anchor */
			for (int k = 0; k < 2; k++) if (dpb[k].poc == fr->l0_poc) {
				refpa[0][0] = (uint32_t)dpb[k].l->pa; refpa[0][1] = (uint32_t)dpb[k].c->pa; }
			slice_const = 0x1013u; nmsg = 5;
			msgs[0] = 0x5C06; msgs[1] = 0x0000; msgs[2] = (uint16_t)fr->l0_poc;
			msgs[3] = 0x0200; msgs[4] = 0x0000;
		} else if (fr->stype == 0) {                     /* B: slot0=L0(past), slot1=L1(future) */
			for (int k = 0; k < 2; k++) {
				if (dpb[k].poc == fr->l0_poc) { refpa[0][0] = (uint32_t)dpb[k].l->pa; refpa[0][1] = (uint32_t)dpb[k].c->pa; }
				if (dpb[k].poc == fr->l1_poc) { refpa[1][0] = (uint32_t)dpb[k].l->pa; refpa[1][1] = (uint32_t)dpb[k].c->pa; }
			}
			slice_const = 0x0113u; nmsg = 7;
			msgs[0] = 0x5847; msgs[1] = 0x0000; msgs[2] = (uint16_t)fr->l0_poc;
			msgs[3] = 0x0001; msgs[4] = (uint16_t)fr->l1_poc; msgs[5] = 0x0200; msgs[6] = 0x0000;
		}
		int rc = decode_one(hevc, intc, &cmd, &bs, &pu, &coeff, ol, oc,
			clip_blob + fr->off, fr->dbo, fr->bfnum, fr->qp,
			slice_const, nmsg, (fr->stype == 2) ? NULL : msgs,
			(fr->stype == 2) ? NULL : refpa, fr->poc,
			pu_stride, coeff_stride, luma_stride, chroma_stride, 1);
		const char *tn = fr->stype == 2 ? "I" : fr->stype == 1 ? "P" : "B";
		if (rc != 0) { printf("hevc-m2: IPB frame %d %s POC %u decode FAILED rc=%d\n", f, tn, fr->poc, rc); return 7; }
		const uint8_t *yb = ol->cpu, *g = ipb_golden_y + (size_t)f * FRAME_WIDTH * FRAME_HEIGHT;
		uint32_t bad = 0;
		for (uint32_t y = 0; y < FRAME_HEIGHT; y++)
			for (uint32_t x = 0; x < FRAME_WIDTH; x++)
				if (yb[(x / 128u) * luma_stride + y * 128u + (x % 128u)] != g[y * FRAME_WIDTH + x]) bad++;
		printf("hevc-m2: IPB frame %d %s POC %u -> %s (%u bad px)\n", f, tn, fr->poc,
			bad ? "MISMATCH" : "bit-exact", bad);
		nbad += bad;
	}
	printf("hevc-m2: IPB-TEST %s\n", nbad == 0 ?
		"B-FRAME BIT-EXACT — bidirectional inter works" : "B/anchors differ from golden");
	return nbad ? 7 : 0;
	}
#elif defined(IPPP_TEST)
	/* Rolling-DPB inter sequence: I + P... each P references the previous decoded
	 * frame. Ping-pong two buffers (cur/prev); verify every frame vs the golden. */
	{
	dma_buf_t bl = {0}, bc = {0};   /* second frame buffer (ping-pong with luma/chroma) */
	if (dma_alloc(&bl, luma_stride * cols + 4096u) || dma_alloc(&bc, chroma_stride * cols + 4096u)) {
		printf("hevc-m2: DMA alloc (ppong) FAILED\n"); return 4;
	}
	uint32_t ok __attribute__((unused)) = 0, decoded __attribute__((unused)) = 0;
#ifdef IPPP_STRESS
	/* Statistical validation of the phase-1->phase-2 drain fix: loop the golden
	 * verify many times with NO fb/sleep (amplifies the race) and tally full-pass
	 * iterations + per-frame failures. A correct fix -> 0 failures over hundreds
	 * of iterations (pristine baseline ~65% full-pass). */
	#ifndef STRESS_ITERS
	#define STRESS_ITERS 300
	#endif
	uint8_t *fb __attribute__((unused)) = NULL;
	const int passes = STRESS_ITERS;
	uint32_t full_pass = 0, frame_fail[IPPP_NFRAMES] = {0};
#else
	fbmode_t fbm; int fbfd = -1; uint8_t *fb = fb_open(&fbm, &fbfd);   /* display inter video */
#ifdef IPPP_HAVE_GOLDEN
	const int passes = 1;                    /* verify once */
#else
	const int passes = 20;                   /* replay so a periodic HDMI snapshot lands mid-play */
#endif
#endif
	for (int loop = 0; loop < passes; loop++) {
	uint32_t iter_bad __attribute__((unused)) = 0;
	for (int f = 0; f < IPPP_NFRAMES; f++) {
		const struct ippp_frame *fr = &ippp_frames[f];
		dma_buf_t *cl = (f & 1) ? &bl : &luma,   *cc = (f & 1) ? &bc : &chroma;
		dma_buf_t *pl = (f & 1) ? &luma : &bl,   *pc = (f & 1) ? &chroma : &bc;
		uint16_t msgs[5] = { 0x5C06, 0x0000, (uint16_t)fr->ref_poc, 0x0200, 0x0000 };
		uint32_t refpa[16][2];
		for (int i = 0; i < 16; i++) { refpa[i][0] = (uint32_t)cl->pa; refpa[i][1] = (uint32_t)cc->pa; }
		refpa[0][0] = (uint32_t)pl->pa; refpa[0][1] = (uint32_t)pc->pa;   /* ref = previous frame */
		int rc = decode_one(hevc, intc, &cmd, &bs, &pu, &coeff, cl, cc,
			clip_blob + fr->off, fr->dbo, fr->bfnum, fr->qp,
			fr->is_p ? 0x1013u : 0u, fr->is_p ? 5u : 0u, fr->is_p ? msgs : NULL,
			fr->is_p ? refpa : NULL, fr->poc,
			pu_stride, coeff_stride, luma_stride, chroma_stride, 0);
		if (rc != 0) {
#ifdef IPPP_STRESS
			frame_fail[f]++; iter_bad++;
#else
			printf("hevc-m2: frame %d (%s POC %u) decode FAILED rc=%d\n",
				f, fr->is_p ? "P" : "I", fr->poc, rc);
#endif
			continue;
		}
		decoded++;
#ifndef IPPP_STRESS
		if (fb) fb_blit(fb, fbm.pitch, fbm.width, fbm.height, cl->cpu, cc->cpu,
				FRAME_WIDTH, FRAME_HEIGHT, luma_stride, chroma_stride);
#ifndef NO_FRAME_SLEEP
		{ struct timespec ts = { 0, 40000000 }; nanosleep(&ts, NULL); }   /* ~25 fps */
#endif
#elif defined(STRESS_SLEEP)
		/* Isolate the inter-frame idle: replicate the display loop's gap WITHOUT
		 * fb0, to test whether idle duration alone (e.g. HEVC clock gating between
		 * frames) is the trigger. -DSTRESS_SLEEP_MS=N sweeps the gap. */
		#ifndef STRESS_SLEEP_MS
		#define STRESS_SLEEP_MS 40
		#endif
		{ struct timespec ts = { STRESS_SLEEP_MS / 1000, (long)(STRESS_SLEEP_MS % 1000) * 1000000L };
		  nanosleep(&ts, NULL); }
#endif
#ifdef IPPP_HAVE_GOLDEN
		const uint8_t *yb = cl->cpu, *g = ippp_golden_y + (size_t)f * FRAME_WIDTH * FRAME_HEIGHT;
		uint32_t bad = 0;
		for (uint32_t y = 0; y < FRAME_HEIGHT; y++)
			for (uint32_t x = 0; x < FRAME_WIDTH; x++)
				if (yb[(x / 128u) * luma_stride + y * 128u + (x % 128u)] != g[y * FRAME_WIDTH + x]) bad++;
#ifdef IPPP_STRESS
		if (bad) { frame_fail[f]++; iter_bad++; }
#else
		printf("hevc-m2: frame %d %s POC %u -> %s (%u bad px)\n", f, fr->is_p ? "P" : "I", fr->poc,
			bad ? "MISMATCH" : "bit-exact", bad);
		if (!bad) ok++;
#endif
#endif
	}
#ifdef IPPP_STRESS
	if (iter_bad == 0) full_pass++;
#endif
	}
#ifdef IPPP_STRESS
	printf("hevc-m2: IPPP-STRESS %u/%d iters fully bit-exact; per-frame fails:", full_pass, passes);
	for (int f = 0; f < IPPP_NFRAMES; f++) printf(" f%d=%u", f, frame_fail[f]);
	printf("\n");
	return full_pass == (uint32_t)passes ? 0 : 7;
#else
	if (fb) { munmap(fb, fbm.smemlen); close(fbfd); }
#ifdef IPPP_HAVE_GOLDEN
	printf("hevc-m2: IPPP-TEST %u/%d frames bit-exact %s\n", ok, IPPP_NFRAMES,
		ok == IPPP_NFRAMES ? "— rolling-DPB inter sequence WORKS" : "");
	return ok == IPPP_NFRAMES ? 0 : 7;
#else
	printf("hevc-m2: IPPP-PLAY decoded+displayed %u frames (%d-frame inter clip%s)\n",
		decoded, IPPP_NFRAMES, fb ? " on HDMI" : " headless");
	return decoded ? 0 : 7;
#endif
#endif
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
#else  /* PLAY_TOOL: runtime .265 file player (M3) */

/* Slurp a whole elementary stream into a malloc'd buffer. */
static uint8_t *slurp_265(const char *path, uint32_t *len_out)
{
	FILE *f = fopen(path, "rb");
	if (!f) { printf("hevc-play: cannot open %s\n", path); return NULL; }
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0) { printf("hevc-play: empty/unreadable %s\n", path); fclose(f); return NULL; }
	uint8_t *b = malloc((size_t)n);
	if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) {
		printf("hevc-play: read failed %s\n", path); free(b); fclose(f); return NULL;
	}
	fclose(f);
	*len_out = (uint32_t)n;
	return b;
}

/* Decode one slice NAL into (cl,cc); for P, ref[0] = the previous decoded frame
 * (pl,pc). Returns decode_one's rc. */
/* Decode one I/P/B slice into (ol,oc). For P: l0 = the single L0 reference. For
 * B: l0 = past (L0[0]), l1 = future (L1[0]). slice_const + cmd_slice are built
 * from the parsed fields (driver slice_reg_const / cmd_slice formulas), so the
 * correct max_num_merge_cand / mvd_l1_zero from the stream are used, not
 * assumed. Non-weighted, single ref per list (the player's subset). */
static int play_frame(volatile uint8_t *hevc, volatile uint8_t *intc,
		      dma_buf_t *cmd, dma_buf_t *bs, dma_buf_t *pu, dma_buf_t *coeff,
		      const hevc_nal_t *nal, const hevc_slice_t *s,
		      dma_buf_t *ol, dma_buf_t *oc,
		      dma_buf_t *l0l, dma_buf_t *l0c, dma_buf_t *l1l, dma_buf_t *l1c,
		      uint32_t l0_poc, uint32_t l1_poc,
		      uint32_t pu_stride, uint32_t coeff_stride,
		      uint32_t luma_stride, uint32_t chroma_stride)
{
	uint32_t refpa[16][2];
	for (int i = 0; i < 16; i++) { refpa[i][0] = (uint32_t)ol->pa; refpa[i][1] = (uint32_t)oc->pa; }
	uint32_t slice_const = 0, nmsg = 0, mmc = s->max_num_merge_cand;
	uint16_t msgs[7];
	if (s->slice_type == 1) {                /* P: 1 L0 ref */
		refpa[0][0] = (uint32_t)l0l->pa; refpa[0][1] = (uint32_t)l0c->pa;
		slice_const = mmc | (1u << 4) | (1u << 12);                 /* mmc | L0=1 | type P */
		nmsg = 5;
		msgs[0] = (uint16_t)(2u | (1u << 2) | (1u << 10) | (mmc << 11) | (1u << 14)); /* cmd_slice P */
		msgs[1] = 0x0000; msgs[2] = (uint16_t)l0_poc; msgs[3] = 0x0200; msgs[4] = 0x0000;
	} else if (s->slice_type == 0) {         /* B: 1 L0 (past) + 1 L1 (future) */
		refpa[0][0] = (uint32_t)l0l->pa; refpa[0][1] = (uint32_t)l0c->pa;
		refpa[1][0] = (uint32_t)l1l->pa; refpa[1][1] = (uint32_t)l1c->pa;
		slice_const = mmc | (1u << 4) | (1u << 8) | ((uint32_t)s->mvd_l1_zero_flag << 16); /* type B=0 */
		nmsg = 7;
		msgs[0] = (uint16_t)(3u | (1u << 2) | (1u << 6) | (mmc << 11) | (1u << 14)); /* cmd_slice B, no_backward=0 */
		msgs[1] = 0x0000; msgs[2] = (uint16_t)l0_poc;
		msgs[3] = 0x0001; msgs[4] = (uint16_t)l1_poc; msgs[5] = 0x0200; msgs[6] = 0x0000;
	}
	return decode_one(hevc, intc, cmd, bs, pu, coeff, ol, oc,
		nal->data, s->data_byte_offset, s->bfnum, s->slice_qp,
		slice_const, nmsg, (s->slice_type == 2) ? NULL : msgs,
		(s->slice_type == 2) ? NULL : refpa, s->poc,
		pu_stride, coeff_stride, luma_stride, chroma_stride, 0);
}

static int is_slice_nal(int t)
{
	return t == HEVC_NAL_TRAIL_N || t == HEVC_NAL_TRAIL_R ||
	       t == HEVC_NAL_IDR_W_RADL || t == HEVC_NAL_IDR_N_LP;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 2) { printf("usage: hevc-play <file.265>\n"); return 2; }

	uint32_t fsz = 0;
	uint8_t *file = slurp_265(argv[1], &fsz);
	if (!file) return 2;
	printf("hevc-play: %s (%u bytes)\n", argv[1], fsz);

	/* Pass 1: parse SPS/PPS, count slice NALs, find the largest (bitstream buf size). */
	hevc_sps_t sps; int have_sps = 0;
	hevc_pps_t pps; int have_pps = 0;
	hevc_nal_iter_t it = { file, fsz, 0, 0 };
	hevc_nal_t nal;
	uint32_t max_nal = 0, nslices = 0;
	while (hevc_nal_next(&it, &nal)) {
		if (nal.type == HEVC_NAL_SPS && !have_sps) {
			if (hevc_parse_sps(nal.data, nal.len, &sps) < 0) {
				printf("hevc-play: SPS rejected: %s\n", hevc_err()); free(file); return 3;
			}
			have_sps = 1;
		} else if (nal.type == HEVC_NAL_PPS && !have_pps) {
			if (hevc_parse_pps(nal.data, nal.len, &pps) < 0) {
				printf("hevc-play: PPS rejected: %s\n", hevc_err()); free(file); return 3;
			}
			have_pps = 1;
		} else if (is_slice_nal(nal.type)) {
			if (nal.len > max_nal) max_nal = nal.len;
			nslices++;
		}
	}
	if (!have_sps) { printf("hevc-play: no SPS found\n"); free(file); return 3; }
	if (sps.chroma_format_idc != 1 || sps.bit_depth_luma_minus8 || sps.bit_depth_chroma_minus8) {
		printf("hevc-play: only 8-bit 4:2:0 supported\n"); free(file); return 3;
	}
	if (have_pps && (pps.weighted_pred || pps.weighted_bipred)) {
		printf("hevc-play: weighted (bi)pred streams unsupported (non-weighted only)\n"); free(file); return 3;
	}
	if (!nslices) { printf("hevc-play: no coded slices\n"); free(file); return 3; }

	/* Runtime geometry from the SPS. */
	g_frame_w = sps.width; g_frame_h = sps.height;
	g_ctb_w = (sps.width + 63u) / 64u; g_ctb_h = (sps.height + 63u) / 64u;
	printf("hevc-play: %ux%u  %u CTBs (%ux%u)  %u frames%s\n", g_frame_w, g_frame_h,
		g_ctb_w * g_ctb_h, g_ctb_w, g_ctb_h, nslices, sps.temporal_mvp_enabled ? " [tmvp]" : "");

	volatile uint8_t *hevc, *intc;
	int hrc = hevc_hw_up(&hevc, &intc);
	if (hrc) { free(file); return hrc; }

	/* Runtime-sized DMA buffers + a ping-pong output pair for the rolling DPB. The
	 * bitstream buffer is sized to the largest slice NAL (item: any file, not just
	 * our vectors — a fixed cap would silently overflow a high-bitrate frame). */
	uint32_t wh = g_frame_w * g_frame_h;
	uint32_t pu_size = round_up_size(wh / 4u), coeff_size = round_up_size(wh);
	uint32_t luma_stride = ((g_frame_h + 15u) & ~15u) * 128u;
	uint32_t chroma_stride = luma_stride / 2u;
	uint32_t cols = ((g_frame_w + 127u) & ~127u) / 128u;
	size_t bs_size = ((size_t)max_nal + 4096u) & ~(size_t)4095u;
	dma_buf_t cmd = {0}, bs = {0}, pu = {0}, coeff = {0};
	/* Two ping-pong ANCHOR (I/P, reference) slots + one B SCRATCH (non-reference).
	 * ref=1 single-B subset: each new anchor reuses the 2-generations-old slot
	 * (no longer referenced once the intervening B is past). */
	dma_buf_t la = {0}, ca = {0}, lb = {0}, cb = {0}, ls = {0}, cs = {0};
	if (dma_alloc(&cmd, 64u * 1024u) || dma_alloc(&bs, bs_size) ||
	    dma_alloc(&pu, pu_size) || dma_alloc(&coeff, coeff_size) ||
	    dma_alloc(&la, luma_stride * cols + 4096u) || dma_alloc(&ca, chroma_stride * cols + 4096u) ||
	    dma_alloc(&lb, luma_stride * cols + 4096u) || dma_alloc(&cb, chroma_stride * cols + 4096u) ||
	    dma_alloc(&ls, luma_stride * cols + 4096u) || dma_alloc(&cs, chroma_stride * cols + 4096u)) {
		printf("hevc-play: DMA alloc FAILED\n"); free(file); return 4;
	}
	uint32_t pu_stride = (pu_size / g_ctb_h) & ~63u;
	uint32_t coeff_stride = (coeff_size / g_ctb_h) & ~63u;
	printf("hevc-play: buffers bs=%zu pu=%u coeff=%u luma_stride=%u cols=%u\n",
		bs_size, pu_size, coeff_size, luma_stride, cols);

	fbmode_t fbm; int fbfd = -1; uint8_t *fb = fb_open(&fbm, &fbfd);

	/* Play the sequence, looping so a periodic HDMI snapshot lands mid-clip. Each
	 * loop restarts at the IDR (anchor DPB reset), so the rolling refs stay valid.
	 * Anchors (I/P) ping-pong two ref slots; B decodes to scratch (L0=past anchor,
	 * L1=future anchor, decoded just before it). Frames are PRESENTED in display
	 * (POC) order via a depth-2 reorder (emit the lower-POC of two held frames) —
	 * correct for the max-1-consecutive-B subset. */
	const int passes = (nslices >= 8) ? 2 : 8;
	uint32_t shown = 0;
	struct timespec ts25 = { 0, 40000000 };
	int broke = 0;
	for (int loop = 0; loop < passes && !broke; loop++) {
		hevc_nal_iter_t it2 = { file, fsz, 0, 0 };
		hevc_nal_t n2;
		uint32_t frame = 0;
		int parity = 0;
		dma_buf_t *anew_l = NULL, *anew_c = NULL, *aold_l = NULL, *aold_c = NULL;
		uint32_t anew_poc = 0, aold_poc = 0;
		dma_buf_t *held_l = NULL, *held_c = NULL; uint32_t held_poc = 0; int held_valid = 0;
		while (hevc_nal_next(&it2, &n2)) {
			if (!is_slice_nal(n2.type)) continue;
			hevc_slice_t s;
			if (hevc_parse_slice(n2.data, n2.len, n2.type, &sps, have_pps ? &pps : NULL, &s) < 0) {
				printf("hevc-play: frame %u slice rejected: %s\n", frame, hevc_err()); broke = 1; break;
			}
			if (n2.len > bs.size) {
				printf("hevc-play: frame %u NAL %u > bitstream buf %zu\n", frame, n2.len, bs.size); broke = 1; break;
			}
			int is_b = (s.slice_type == 0), is_p = (s.slice_type == 1);
			if (is_p && !anew_l) { printf("hevc-play: P before any I — stream must start with IDR\n"); broke = 1; break; }
			if (is_b && (!anew_l || !aold_l)) { printf("hevc-play: B before two anchors\n"); broke = 1; break; }

			dma_buf_t *ol, *oc;
			if (is_b) { ol = &ls; oc = &cs; }                       /* B -> scratch */
			else if (parity == 0) { ol = &la; oc = &ca; }           /* anchor -> slot A */
			else { ol = &lb; oc = &cb; }                            /* anchor -> slot B */

			int rc = play_frame(hevc, intc, &cmd, &bs, &pu, &coeff, &n2, &s, ol, oc,
				is_b ? aold_l : anew_l, is_b ? aold_c : anew_c,          /* L0 = past */
				is_b ? anew_l : NULL,  is_b ? anew_c : NULL,             /* L1 = future (B) */
				is_b ? aold_poc : anew_poc, is_b ? anew_poc : 0,
				pu_stride, coeff_stride, luma_stride, chroma_stride);
			if (rc != 0) { printf("hevc-play: frame %u (%s POC %u) decode FAILED rc=%d\n",
				frame, is_b ? "B" : is_p ? "P" : "I", s.poc, rc); broke = 1; break; }

			if (!is_b) {   /* anchor: advance the rolling ref window + ping-pong slot */
				aold_l = anew_l; aold_c = anew_c; aold_poc = anew_poc;
				anew_l = ol; anew_c = oc; anew_poc = s.poc; parity ^= 1;
			}

			/* depth-2 display reorder: emit the lower-POC of {held, new}. */
			if (held_valid) {
				dma_buf_t *el, *ec;
				if (s.poc < held_poc) { el = ol; ec = oc; }                  /* new is earlier */
				else { el = held_l; ec = held_c; held_l = ol; held_c = oc; held_poc = s.poc; }
				if (fb) fb_blit(fb, fbm.pitch, fbm.width, fbm.height, el->cpu, ec->cpu,
						g_frame_w, g_frame_h, luma_stride, chroma_stride);
				nanosleep(&ts25, NULL); shown++;
			} else { held_l = ol; held_c = oc; held_poc = s.poc; held_valid = 1; }
			frame++;
		}
		if (!broke && held_valid) {   /* flush the last held frame */
			if (fb) fb_blit(fb, fbm.pitch, fbm.width, fbm.height, held_l->cpu, held_c->cpu,
					g_frame_w, g_frame_h, luma_stride, chroma_stride);
			nanosleep(&ts25, NULL); shown++;
		}
	}
	if (fb) { munmap(fb, fbm.smemlen); close(fbfd); }
	free(file);
	printf("hevc-play: decoded+displayed %u frame-instances (%u unique frames)%s\n",
		shown, nslices, fb ? " on HDMI" : " headless");
	return shown ? 0 : 7;
}
#endif /* PLAY_TOOL */
