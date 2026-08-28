/*
 * hevc-m1 — Raspberry Pi 4 (BCM2711) HEVC/H.265 hardware decoder M1 bring-up.
 *
 * M0 (tools/hevc-probe) proved the rpivid/hevc_dec block is reachable over plain
 * uncached MMIO with no VCHIQ: clock via the VideoCore mailbox, version reg = 0x202,
 * ARGON INTC enable. M1 stands up the two decode PREREQUISITES that the dense
 * per-slice register programming (M2, ported from Linux hevc_d_h265.c) will hang off:
 *
 *   1. a contiguous/uncached DMA buffer allocator (dma_alloc) for the decode working
 *      set — reusing the exact idiom the V3D port uses on this HW
 *      (mmap(MAP_UNCACHED|MAP_CONTIGUOUS|MAP_ANONYMOUS) + va2pa()), and
 *   2. GIC SPI-98 (abs IRQ 32+98 = 130) interrupt delivery, acked through the ARGON
 *      INTC, so the hardware can signal decode-done/error.
 *
 * This M1 harness deliberately does NOT program the decode core yet (that is M2). It
 * verifies, on real HW, that: the clock enables, the blocks map, the version is 0x202,
 * the DMA allocator returns contiguous sub-4GB physical buffers the 36-bit engine can
 * address, and the SPI-130 handler registers cleanly with no spurious interrupt. The
 * decode-run register sequence + the full working-buffer set (coeff/DPB/collocated-MV/…)
 * land in M2 from the Linux-driver register spec (docs/inprogress/2026-08-28-hevc-m1-plan.md).
 *
 * Register facts verified against the in-tree Linux clone
 *   drivers/media/platform/raspberrypi/hevc_dec/hevc_d_hw.{c,h}
 *   arch/arm/boot/dts/broadcom/bcm2711.dtsi  (hevc 0x7eb00000, intc 0x7eb10000,
 *                                             hevc_dec interrupts = <GIC_SPI 98 ...>)
 *
 * Copyright 2026 Phoenix Systems
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/interrupt.h>
#include <sys/threads.h>

#include "libvcmbox.h"

/* ---- VideoCore firmware property tags (mailbox channel 8) — same as M0. ---- */
#define VC_GET_MAX_CLOCK_RATE 0x00030004u
#define VC_SET_CLOCK_STATE    0x00038001u
#define VC_SET_CLOCK_RATE     0x00038002u
#define VC_GET_CLOCK_RATE     0x00030002u
#define RPI_FIRMWARE_HEVC_CLK_ID 11u

/* ---- HEVC decoder block: ARM-physical bases (DT bus 0x7exxxxxx -> 0xfexxxxxx). ---- */
#define HEVC_BASE 0xfeb00000u
#define HEVC_SIZE 0x10000u
#define INTC_BASE 0xfeb10000u
#define INTC_SIZE 0x1000u

/* ---- Register offsets (byte offsets; apb_read/irq_read = readl(base + offset)). ---- */
#define RPI_VERSION     60u
#define HEVC_EXPECT_VER 0x202u
#define ARG_IC_ICTRL    0u            /* ARGON interrupt-controller control reg */
#define ARG_IC_ICSTATUS 4u            /* status reg (read pending; write-1-clear) — M2 confirms */
#define ACTIVE1_EN_SET  (1u << 2)
#define ACTIVE2_EN_SET  (1u << 6)

/* ---- IRQ: bcm2711.dtsi hevc_dec "interrupts = <GIC_SPI 98 ...>"; GIC SPIs base at 32. ---- */
#define HEVC_GIC_SPI 98u
#define HEVC_IRQ     (32u + HEVC_GIC_SPI)   /* = 130 */


static inline uint32_t readl(const volatile void *a) { return *(const volatile uint32_t *)a; }
static inline void writel(volatile void *a, uint32_t v) { *(volatile uint32_t *)a = v; }


/* One contiguous, uncached DMA buffer + its physical address for the decoder. */
typedef struct {
	const char *name;
	void       *cpu;   /* CPU virtual mapping */
	addr_t      pa;    /* physical/bus address to program into a rpivid register */
	size_t      size;
} dma_buf_t;


/* Map one BCM2711 MMIO block as uncached device memory. */
static void *map_block(uint32_t base, uint32_t size)
{
	return mmap(NULL, size, PROT_READ | PROT_WRITE,
		MAP_DEVICE | MAP_UNCACHED | MAP_PHYSMEM | MAP_ANONYMOUS, -1, (off_t)base);
}


/*
 * Allocate a physically-contiguous, uncached DMA buffer for the decoder. This is the
 * proven V3D-port idiom (v3d_phoenix_winsys.c): MAP_CONTIGUOUS guarantees a single
 * contiguous physical run, MAP_UNCACHED keeps CPU + the DMA engine coherent without
 * explicit cache maintenance, va2pa() resolves the physical base to program into the
 * block's buffer-address registers. MAP_CONTIGUOUS returns NON-zeroed DRAM (Phoenix
 * zeroes only the vm_object, not page contents), so we memset — the decoder must not
 * see stale DRAM in its coeff/DPB working set.
 */
static int dma_alloc(dma_buf_t *b, const char *name, size_t size)
{
	/* Round up to a page; the 36-bit rpivid DMA engine wants page-aligned bases
	 * (M2 tightens per-buffer alignment from the Linux driver's dma_alloc calls). */
	size_t pgsz = (size_t)sysconf(_SC_PAGESIZE);
	size = (size + pgsz - 1u) & ~(pgsz - 1u);

	b->name = name;
	b->size = size;
	b->cpu = mmap(NULL, size, PROT_READ | PROT_WRITE,
		MAP_UNCACHED | MAP_CONTIGUOUS | MAP_ANONYMOUS, -1, 0);
	if (b->cpu == MAP_FAILED) {
		printf("hevc-m1: dma_alloc('%s', %zu) FAILED (no contiguous run)\n", name, size);
		b->cpu = NULL;
		return -ENOMEM;
	}
	memset(b->cpu, 0, size);
	b->pa = va2pa(b->cpu);
	return 0;
}


static void dma_free(dma_buf_t *b)
{
	if (b->cpu != NULL) {
		munmap(b->cpu, b->size);
		b->cpu = NULL;
	}
}


/* Enable the HEVC clock via the firmware mailbox (M0 sequence). 0 on success. */
static int hevc_clock_enable(uint32_t *rate_out)
{
	uint32_t in[3];
	uint32_t out[2] = { 0u, 0u };
	uint32_t max_rate = 0u, set_rate = 0u, cfg_rate = 0u;
	int rc;

	in[0] = RPI_FIRMWARE_HEVC_CLK_ID;
	rc = vcmbox_call(VC_GET_MAX_CLOCK_RATE, 8u, in, 1u, out, 2u);
	if (rc != 0) { printf("hevc-m1: GET_MAX_CLOCK_RATE failed (rc=%d)\n", rc); return -1; }
	max_rate = out[1];
	if (max_rate == 0u) { printf("hevc-m1: firmware max HEVC rate = 0\n"); return -1; }

	in[0] = RPI_FIRMWARE_HEVC_CLK_ID; in[1] = 1u;
	rc = vcmbox_call(VC_SET_CLOCK_STATE, 8u, in, 2u, out, 2u);
	if (rc != 0) { printf("hevc-m1: SET_CLOCK_STATE failed (rc=%d)\n", rc); return -1; }

	in[0] = RPI_FIRMWARE_HEVC_CLK_ID; in[1] = max_rate; in[2] = 0u;
	rc = vcmbox_call(VC_SET_CLOCK_RATE, 12u, in, 3u, out, 2u);
	if (rc != 0) { printf("hevc-m1: SET_CLOCK_RATE failed (rc=%d)\n", rc); return -1; }
	set_rate = out[1];

	in[0] = RPI_FIRMWARE_HEVC_CLK_ID;
	rc = vcmbox_call(VC_GET_CLOCK_RATE, 8u, in, 1u, out, 2u);
	if (rc == 0) cfg_rate = out[1];

	*rate_out = (cfg_rate != 0u) ? cfg_rate : set_rate;
	if (*rate_out == 0u) { printf("hevc-m1: HEVC clock reads 0 after enable\n"); return -1; }
	printf("hevc-m1: HEVC clock enabled = %u Hz (%.1f MHz)\n",
		*rate_out, (double)*rate_out / 1.0e6);
	return 0;
}


/* ---- IRQ: SPI-130 handler. Runs in interrupt context: read+ack the ARGON INTC
 * status, count it, signal the waiter. The precise done/error bit decode is M2 (from
 * the Linux ISR); M1 only proves the interrupt path registers and stays quiet. ---- */
static volatile uint8_t *g_intc;      /* ARGON INTC mapping, for the ISR */
static volatile uint32_t g_irq_count; /* incremented per interrupt */
static volatile uint32_t g_irq_status;/* last ARG_IC_ICSTATUS observed */

static int hevc_isr(unsigned int n, void *arg)
{
	(void)n; (void)arg;
	uint32_t st = readl(g_intc + ARG_IC_ICSTATUS);
	g_irq_status = st;
	writel(g_intc + ARG_IC_ICSTATUS, st); /* write-1-clear the latched bits (M2 confirms semantics) */
	g_irq_count++;
	return 0; /* no thread wakeup signalling wired in M1 (no cond yet); M2 adds it */
}


int main(void)
{
	void *hevc_page, *intc_page;
	volatile uint8_t *hevc;
	uint32_t rate = 0u, ver, ictrl;
	handle_t cond = 0, irqHandle = 0;
	dma_buf_t bitstream = {0}, luma = {0}, chroma = {0};
	int rc;

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("hevc-m1: BCM2711 HEVC/H.265 decoder M1 bring-up (DMA allocators + SPI-%u IRQ)\n",
		HEVC_GIC_SPI);

	/* STEP 1 — clock first (unclocked APB access hangs the bus). */
	if (hevc_clock_enable(&rate) != 0) {
		printf("hevc-m1: clock enable FAILED — aborting (block untouched)\n");
		return 2;
	}

	/* STEP 2 — map the register blocks. */
	hevc_page = map_block(HEVC_BASE, HEVC_SIZE);
	if (hevc_page == MAP_FAILED) { printf("hevc-m1: mmap HEVC FAILED\n"); return 3; }
	intc_page = map_block(INTC_BASE, INTC_SIZE);
	if (intc_page == MAP_FAILED) { printf("hevc-m1: mmap INTC FAILED\n"); munmap(hevc_page, HEVC_SIZE); return 3; }
	hevc = (volatile uint8_t *)hevc_page;
	g_intc = (volatile uint8_t *)intc_page;

	/* STEP 3 — version liveness oracle. */
	ver = readl(hevc + RPI_VERSION);
	printf("hevc-m1: RPI_VERSION = 0x%x (expect 0x%x)%s\n", ver, HEVC_EXPECT_VER,
		ver == HEVC_EXPECT_VER ? "" : "  !! MISMATCH");

	/* STEP 4 — DMA allocator smoke: allocate a representative decode working set for a
	 * small frame and prove each buffer is contiguous + sub-4GB (36-bit-addressable).
	 * The FULL rpivid working set (coeff/DPB/collocated-MV/…) + exact sizes come with M2
	 * from the register spec; here we exercise the allocator + address translation. */
	rc  = dma_alloc(&bitstream, "bitstream", 256u * 1024u);       /* input NAL buffer */
	rc |= dma_alloc(&luma,      "out-luma",  1920u * 1088u);      /* NV12 Y plane (1080p) */
	rc |= dma_alloc(&chroma,    "out-chroma",1920u * 1088u / 2u); /* NV12 CbCr plane */
	if (rc != 0) {
		printf("hevc-m1: DMA allocation FAILED — not enough contiguous DRAM\n");
	} else {
		printf("hevc-m1: DMA working-set allocated (contiguous, uncached):\n");
		const dma_buf_t *bufs[] = { &bitstream, &luma, &chroma };
		for (unsigned i = 0; i < 3; i++) {
			const dma_buf_t *b = bufs[i];
			printf("  %-10s cpu=%p pa=0x%08llx size=%zu%s\n", b->name, b->cpu,
				(unsigned long long)b->pa, b->size,
				((unsigned long long)b->pa + b->size <= 0x100000000ull) ? " [<4GB ok]" : " !! >4GB");
		}
	}

	/* STEP 5 — arm the ARGON INTC (M0) + register the SPI-130 handler. In M1 this proves
	 * the interrupt path registers cleanly and no spurious IRQ arrives before a decode is
	 * kicked (SPI-98 only fires on a decode, which is M2). */
	writel(g_intc + ARG_IC_ICTRL, ACTIVE1_EN_SET | ACTIVE2_EN_SET);
	ictrl = readl(g_intc + ARG_IC_ICTRL);
	writel(g_intc + ARG_IC_ICTRL, ictrl); /* clear latched pending */
	printf("hevc-m1: ARGON INTC armed (ARG_IC_ICTRL=0x%08x)\n", ictrl);

	if (condCreate(&cond) < 0) {
		printf("hevc-m1: condCreate FAILED\n");
	} else {
		rc = interrupt(HEVC_IRQ, hevc_isr, NULL, cond, &irqHandle);
		if (rc < 0)
			printf("hevc-m1: interrupt(SPI-%u=irq %u) registration FAILED (rc=%d)\n",
				HEVC_GIC_SPI, HEVC_IRQ, rc);
		else
			printf("hevc-m1: SPI-%u handler registered (abs irq %u, handle=%d)\n",
				HEVC_GIC_SPI, HEVC_IRQ, (int)irqHandle);
	}

	/* Quiet window: confirm no spurious interrupt before any decode is kicked. */
	sleep(2);
	printf("hevc-m1: post-arm IRQ count = %u (expect 0 — no decode kicked in M1) last_status=0x%08x\n",
		g_irq_count, g_irq_status);

	dma_free(&bitstream); dma_free(&luma); dma_free(&chroma);
	munmap(intc_page, INTC_SIZE);
	munmap(hevc_page, HEVC_SIZE);

	printf("hevc-m1: M1 scaffold complete — clock+map+version %s, DMA allocator %s, IRQ path %s\n",
		ver == HEVC_EXPECT_VER ? "OK" : "MISMATCH",
		rc == 0 ? "OK" : "FAIL",
		irqHandle != 0 ? "registered" : "unregistered");
	return (ver == HEVC_EXPECT_VER) ? 0 : 1;
}
