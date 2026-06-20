/*
 * v3d_phoenix_winsys.c — Phoenix winsys backend for Mesa's v3d gallium driver
 * (GLQuake Path C, Phase 2). The driver talks to the "kernel" only through
 * drmIoctl(fd, DRM_IOCTL_V3D_*, arg); this provides those ioctls on Phoenix using
 * the PROVEN rpi4-v3d-scout primitives (BO=mmap+va2pa, GPU VA via the V3D MMU flat
 * PT, SUBMIT_CL=CT0/CT1 QBA/QEA + FLDONE/FRDONE + L2T flush, GET_PARAM=real
 * V3D-4.2 device info). No DRM, no kernel driver. Synchronous submit (no real
 * fences) -> drmSyncobj* are stubbed elsewhere in the libdrm shim.
 *
 * STATUS: design crystallized from the scout + the confirmed v3d_drm.h struct
 * mapping; pending integration (cross-built libv3d-phoenix.a + a gallium harness)
 * before it can be compiled/tested on HW. The submit/MMU/BO logic mirrors
 * rpi4-v3d-scout.c (HW-proven: render-clear, 4096/4096 px).
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/threads.h>
#include "drm-uapi/v3d_drm.h"   /* Mesa's vendored UAPI — same structs the driver uses */

/* V3D 4.2 MMIO (ARM low-peri), HUB + CORE0 — see rpi4-v3d-scout. */
#define V3D_HUB_BASE        0xfec00000u
#define V3D_MMIO_LEN        0x10000u
#define V3D_CORE0_OFFS      0x4000u
/* MMU (HUB-relative) */
#define MMU_PT_PA_BASE      0x1204u
#define MMU_CTL             0x1200u
#define MMU_CTL_ENABLE      (1u<<0)
#define MMU_CTL_PTI_ABORT   (1u<<19)
#define MMUC_CONTROL        0x1000u
#define MMUC_ENABLE         (1u<<0)
#define MMUC_FLUSH          (1u<<1)    /* flush the MMU PTE cache */
#define MMUC_FLUSHING       (1u<<2)    /* set while the PTE-cache flush is in progress */
#define MMU_CTL_TLB_CLEAR   (1u<<2)    /* clear the MMU TLB */
#define MMU_CTL_TLB_CLEARING (1u<<7)   /* set while the TLB clear is in progress */
#define MMU_ILLEGAL_ADDR    0x1230u
#define MMU_ILLEGAL_ENABLE  (1u<<31)
#define PTE_W               (1u<<29)
#define PTE_V               (1u<<28)
#define PAGE_SHIFT          12u
/* CORE0-relative submit/sync (see scout) */
#define CTL_INT_STS         0x0050u
#define CTL_INT_CLR         0x0058u
#define INT_FRDONE          (1u<<0)
#define INT_FLDONE          (1u<<1)
#define INT_OUTOMEM         (1u<<2)   /* binner exhausted its tile-allocation pool */
#define CLE_CT0QTS          0x015cu
#define CT0QTS_ENABLE       (1u<<1)
#define CLE_CT0QBA          0x0160u
#define CLE_CT1QBA          0x0164u
#define CLE_CT0QEA          0x0168u
#define CLE_CT1QEA          0x016cu
#define CLE_CT0QMA          0x0170u
#define CLE_CT0QMS          0x0174u
#define CTL_L2TCACTL        0x0030u
#define L2TCACTL_L2TFLS     (1u<<0)
#define CTL_SLCACTL         0x0024u    /* slices cache control (V3D 4.x) */
#define SLCACTL_INVAL_ALL   0x0f0f0f0fu /* invalidate TVCCS/TDCCS/UCC(uniform)/ICC(instr) */
#define PTB_BPOA            0x0308u   /* binner pool overflow address (GPU VA) */
#define PTB_BPOS            0x030cu   /* binner pool overflow size (bytes) */
/* Binner tile-allocation overflow pool. When the binner exhausts the per-job
 * tile_alloc memory Mesa supplies via CT0QMA/QMS, it raises INT_OUTOMEM and stalls
 * until handed a fresh pool via PTB_BPOA/BPOS (linux v3d_overflow_mem_work). The
 * pool need grows with tile count: 1024x768 (~192 tiles) never overflowed, but
 * 1920x1080 (~510 tiles) does. We pre-allocate one generous pool at init and arm it
 * on the first OUTOMEM of each job (Quake's per-frame overflow stays well under this). */
#define BINOVF_PAGES        1024u     /* 4 MiB persistent binner-overflow pool */
#define CTL_MISCCFG         0x0018u
#define MISCCFG_OVRTMUOUT   (1u<<0)

/* GPU VA space: bump-allocate page-aligned, starting past the null guard. */
#define GPUVA_BASE          0x100000u
/* MMU flat page table size. Each PTE (4 bytes) maps one 4 KiB page, so one 4 KiB
 * PT page = 1024 PTEs = 4 MiB of GPU VA window. A 256x256 RT fits in one page, but
 * a 1024x768 color+depth target (3 MiB each) overflows it -> PTEs written past the
 * table (OOB) at unmapped VAs -> the GPU store lands nowhere -> all-zero RT. Grow to
 * 32 PT pages = 128 MiB of GPU VA, enough for fullscreen color+depth+tile state plus
 * texture/CL working set (Quake). PT must be physically contiguous (the MMU walks it
 * as a flat array from MMU_PT_PA_BASE). */
#define GPUVA_PT_PAGES      64u    /* 64 * 4 MiB = 256 MiB GPU VA window */
#define GPUVA_PT_ENTRIES    (GPUVA_PT_PAGES * (_PAGE_SIZE / 4u))   /* total PTEs */

struct pbo {            /* Phoenix BO */
	uint32_t handle;
	void    *cpu;       /* mmap'd uncached va */
	uintptr_t pa;       /* physical */
	uint32_t gpuva;     /* assigned V3D virtual address (= drm offset) */
	uint32_t size;
	int      used;      /* slot in use (freed by GEM_CLOSE -> reusable) */
};

/* Freed GPU-VA range, available for reuse. Without this the GPU VA + the BO slot
 * array were monotonic (never reclaimed), so sustained rendering (e.g. Quake demo
 * playback: per-frame CL / tile-state BOs) exhausted both -> the >256th BO indexed
 * past bos[] and the RCL emit wrote through a garbage pointer. Mesa's bufmgr frees
 * BOs (GEM_CLOSE) as its cache evicts, so reclaiming here keeps both bounded. */
struct vahole {
	uint32_t gpuva;
	uint32_t pages;
};

#define MAX_BOS    4096u
#define MAX_HOLES  2048u

static struct {
	volatile uint32_t *hub;   /* V3D regs (HUB base) */
	volatile uint32_t *core0;
	volatile uint32_t *pt;    /* MMU flat page table */
	uintptr_t pt_pa;
	uint32_t next_gpuva;
	uint32_t binovf_gpuva;    /* persistent binner-overflow pool GPU VA (0 = none) */
	uint32_t binovf_bytes;    /* its size in bytes */
	uint32_t scanout_pa;      /* HDMI framebuffer physical addr (0 = unavailable) */
	uint32_t scanout_bytes;   /* its byte size (pitch*height) */
	int      scanout_claimed; /* only one BO may alias the single scanout surface */
	int      next_scanout;    /* one-shot: back the NEXT ioc_create_bo with the scanout surface
	                           * (set by a client that can't pass V3D_CREATE_BO_SCANOUT through its
	                           * own BO-alloc path, e.g. the V3DV present image). Cleared on use. */
	struct pbo bos[MAX_BOS];
	uint32_t nbos;            /* high-water mark of slots ever used */
	struct vahole holes[MAX_HOLES];
	uint32_t nholes;
	int inited;
} W;

static volatile uint32_t *map_dev(uint32_t pa, uint32_t len)
{
	void *p = mmap(NULL, len, PROT_READ|PROT_WRITE,
		MAP_DEVICE|MAP_UNCACHED|MAP_PHYSMEM|MAP_ANONYMOUS, -1, (addr_t)pa);
	return (p==MAP_FAILED) ? NULL : (volatile uint32_t *)p;
}

int v3d_phoenix_powerOn(void);   /* v3d_phoenix_power.c — BCM2711 V3D power-on */
static uint32_t va_alloc(uint32_t pages);   /* defined below; used by the init overflow pool */

static int winsys_init(void)
{
	if (W.inited) return 0;
	/* Power on the V3D ourselves (self-contained; no dependency on a separate
	 * scout process whose concurrent clock-toggle/reset would race our submit and
	 * leave core0 reading 0xdeadbeef). Idempotent. */
	v3d_phoenix_powerOn();
	W.hub = map_dev(V3D_HUB_BASE, V3D_MMIO_LEN);
	if (!W.hub) return -ENOMEM;
	W.core0 = W.hub + (V3D_CORE0_OFFS/4);
	/* MMU page table: GPUVA_PT_PAGES contiguous pages = GPUVA_PT_PAGES*4 MiB GPU VA. */
	W.pt = mmap(NULL, GPUVA_PT_PAGES*_PAGE_SIZE, PROT_READ|PROT_WRITE,
		MAP_UNCACHED|MAP_CONTIGUOUS|MAP_ANONYMOUS, -1, 0);
	if (W.pt==MAP_FAILED) return -ENOMEM;
	W.pt_pa = (uintptr_t)va2pa((void*)W.pt);
	for (uint32_t i=0;i<GPUVA_PT_ENTRIES;i++) W.pt[i]=0;
	/* NOTE: assumes V3D already powered on (rpi4-v3d-scout v3d_powerOn sequence). */
	W.hub[MMU_PT_PA_BASE/4] = (uint32_t)(W.pt_pa>>PAGE_SHIFT);
	W.hub[MMU_CTL/4] = MMU_CTL_ENABLE|MMU_CTL_PTI_ABORT;
	W.hub[MMUC_CONTROL/4] = MMUC_ENABLE;
	W.next_gpuva = GPUVA_BASE;

	/* Pre-allocate the persistent binner-overflow pool (uncached DMA, like the CL/tile
	 * BOs): contiguous pages mapped into the flat MMU at a stable GPU VA, reused every
	 * frame. The binner writes tile lists here on OUTOMEM; the render reads them back. */
	{
		uint32_t gpuva = va_alloc(BINOVF_PAGES);
		void *cpu = (gpuva == 0) ? MAP_FAILED :
			mmap(NULL, BINOVF_PAGES*_PAGE_SIZE, PROT_READ|PROT_WRITE,
				MAP_UNCACHED|MAP_CONTIGUOUS|MAP_ANONYMOUS, -1, 0);
		if (cpu != MAP_FAILED) {
			for (uint32_t i=0;i<BINOVF_PAGES;i++) {
				uintptr_t ppa = (uintptr_t)va2pa((char*)cpu + (size_t)i*_PAGE_SIZE);
				W.pt[(gpuva>>PAGE_SHIFT)+i] = (uint32_t)(ppa>>PAGE_SHIFT)|PTE_W|PTE_V;
			}
			W.binovf_gpuva = gpuva;
			W.binovf_bytes = BINOVF_PAGES*_PAGE_SIZE;
		}
		else {
			fprintf(stderr, "v3d-winsys: WARN no binner-overflow pool — large RTs may stall\n");
		}
	}

	W.inited = 1;
	return 0;
}

/* Tell the winsys where the HDMI scanout framebuffer lives (PA + byte size). Called by
 * the present layer (which owns /dev/fb0 and queries RPI4FB_GETMODE) before any rendering.
 * A render target backed by this PA lets the GPU raster-store straight to the displayed
 * framebuffer — no glReadPixels/blit/fb0 CPU copies (render-to-scanout). Kept out of the
 * Mesa-context winsys build's platformctl path (a stale sysroot platform.h there lacks the
 * graphmode.height field), so the caller passes the values it already has. */
void v3d_phoenix_set_scanout(uint32_t pa, uint32_t bytes);
void v3d_phoenix_set_scanout(uint32_t pa, uint32_t bytes)
{
	W.scanout_pa = pa;
	W.scanout_bytes = bytes;
}

/* One-shot: request that the NEXT BO created be backed by the scanout surface. Lets a client
 * whose BO-alloc path can't set V3D_CREATE_BO_SCANOUT (e.g. V3DV's vkAllocateMemory for a present
 * image) still get a scanout-backed BO: call this immediately before the allocation. */
void v3d_phoenix_set_next_scanout(void);
void v3d_phoenix_set_next_scanout(void)
{
	W.next_scanout = 1;
}

static struct pbo *bo_find(uint32_t handle)
{
	if (handle == 0 || handle > W.nbos) return NULL;
	struct pbo *b = &W.bos[handle - 1];   /* handle == slot index + 1 */
	return (b->used && b->handle == handle) ? b : NULL;
}

/* Allocate a page-aligned GPU VA range: first-fit a freed hole (so reclaimed VA is
 * reused and the window doesn't grow unboundedly), else bump-allocate past the
 * high-water mark. Returns 0 on exhaustion. */
static uint32_t va_alloc(uint32_t pages)
{
	for (uint32_t i = 0; i < W.nholes; i++) {
		if (W.holes[i].pages >= pages) {
			uint32_t va = W.holes[i].gpuva;
			if (W.holes[i].pages == pages) {
				W.holes[i] = W.holes[--W.nholes];   /* remove */
			}
			else {
				W.holes[i].gpuva += pages * _PAGE_SIZE;   /* shrink */
				W.holes[i].pages -= pages;
			}
			return va;
		}
	}
	if ((W.next_gpuva >> PAGE_SHIFT) + pages > GPUVA_PT_ENTRIES)
		return 0;   /* window exhausted */
	uint32_t va = W.next_gpuva;
	W.next_gpuva += pages * _PAGE_SIZE;
	return va;
}

static void va_free(uint32_t gpuva, uint32_t pages)
{
	for (uint32_t i = 0; i < pages; i++)
		W.pt[(gpuva >> PAGE_SHIFT) + i] = 0;   /* unmap (PT cleared; TLB flushed per submit) */
	if (W.nholes < MAX_HOLES)
		W.holes[W.nholes++] = (struct vahole){ gpuva, pages };
	/* else: VA leaks (bounded); the next-bump path still serves new allocs. */
}

static int ioc_create_bo(struct drm_v3d_create_bo *c)
{
	uint32_t pages = (c->size + _PAGE_SIZE - 1)/_PAGE_SIZE;
	uint32_t slot, gpuva;
	void *cpu;
	uintptr_t pa;

	/* Reclaim a freed slot if any, else extend the high-water mark. */
	for (slot = 0; slot < W.nbos; slot++)
		if (!W.bos[slot].used) break;
	if (slot == W.nbos) {
		if (W.nbos >= MAX_BOS) {
			fprintf(stderr, "v3d-winsys: BO table full (%u)\n", (unsigned)MAX_BOS);
			return -ENOMEM;
		}
		W.nbos++;
	}

	gpuva = va_alloc(pages);
	if (gpuva == 0) {
		fprintf(stderr, "v3d-winsys: GPU VA exhausted (need %u pages; PT window = %u MiB). "
			"Grow GPUVA_PT_PAGES or check for a BO leak.\n", pages, (GPUVA_PT_PAGES*4u));
		return -ENOMEM;
	}
	/* A SCANOUT BO (flags bit 1 = V3D_CREATE_BO_SCANOUT, set by Mesa for the
	 * full-screen render target) is backed by the HDMI framebuffer's physical pages
	 * instead of fresh DRAM: the GPU raster-stores straight to the displayed surface
	 * (render-to-scanout), eliminating the per-frame glReadPixels/blit/fb0 CPU copies.
	 * The scanout PA is physically contiguous, so map it pa+i. */
	if (((c->flags & 0x2u) || W.next_scanout) && W.scanout_pa && !W.scanout_claimed) {
		W.next_scanout = 0;
		/* Back the visible rows with the scanout framebuffer's physical pages so the GPU
		 * stores straight to screen. The RT BO is a little larger than the fb (V3D stores
		 * tile-aligned rows — 1088 for a 1080 RT — plus driver padding); those extra rows
		 * are never displayed, so map them to fresh scratch DRAM (allocated as the BO's CPU
		 * view) instead of past the end of the fb. */
		uint32_t scanout_pages = W.scanout_bytes / _PAGE_SIZE;
		if (scanout_pages > pages) scanout_pages = pages;
		cpu = mmap(NULL, pages*_PAGE_SIZE, PROT_READ|PROT_WRITE,
			MAP_CONTIGUOUS|MAP_UNCACHED|MAP_ANONYMOUS, -1, 0);
		if (cpu==MAP_FAILED) { va_free(gpuva, pages); return -ENOMEM; }
		pa = W.scanout_pa;
		for (uint32_t i=0;i<pages;i++) {
			uint32_t pfn = (i < scanout_pages)
				? (W.scanout_pa>>PAGE_SHIFT)+i
				: (uint32_t)((uintptr_t)va2pa((char*)cpu + (size_t)i*_PAGE_SIZE) >> PAGE_SHIFT);
			W.pt[(gpuva>>PAGE_SHIFT)+i] = pfn|PTE_W|PTE_V;
		}
		W.scanout_claimed = 1;
		fprintf(stderr, "v3d-winsys: RT scanout PA 0x%08x gpuva 0x%x, %u/%u pages to scanout "
			"(%u scratch) — render-to-scanout\n",
			W.scanout_pa, gpuva, scanout_pages, pages, pages-scanout_pages);
	}
	else {
		/* Scanout was requested one-shot but couldn't be honored (already claimed / no PA);
		 * don't leak the request to a later BO. */
		W.next_scanout = 0;
		/* Default: uncached contiguous DMA memory. A cacheable BO (flags bit 0 =
		 * V3D_CREATE_BO_CACHEABLE, set by Mesa for CPU-read-back-only render targets)
		 * drops MAP_UNCACHED so the CPU readback hits cache; Mesa invalidates it
		 * (dc ivac) before each read. Keeps MAP_CONTIGUOUS for the flat V3D MMU. */
		int mapflags = MAP_CONTIGUOUS | MAP_ANONYMOUS;
		if ((c->flags & 0x1u) == 0u)
			mapflags |= MAP_UNCACHED;
		cpu = mmap(NULL, pages*_PAGE_SIZE, PROT_READ|PROT_WRITE, mapflags, -1, 0);
		if (cpu==MAP_FAILED) {
			fprintf(stderr, "v3d-winsys: BO mmap FAILED (%u pages, %u KiB, flags 0x%x)\n",
				pages, pages*_PAGE_SIZE/1024u, c->flags);
			va_free(gpuva, pages); return -ENOMEM;
		}
		pa = (uintptr_t)va2pa(cpu);
		/* Map each page by its ACTUAL physical address rather than assuming pa+i.
		 * MAP_CONTIGUOUS gives contiguous pages for uncached DMA, but a cacheable mapping
		 * may not be physically contiguous — assuming pa+i would map the GPU to the wrong
		 * pages (MMU fault / hang). Per-page va2pa is correct either way. */
		for (uint32_t i=0;i<pages;i++) {
			uintptr_t ppa = (uintptr_t)va2pa((char*)cpu + (size_t)i*_PAGE_SIZE);
			W.pt[(gpuva>>PAGE_SHIFT)+i] = (uint32_t)(ppa>>PAGE_SHIFT)|PTE_W|PTE_V;
		}
	}

	struct pbo *b = &W.bos[slot];
	b->used = 1;
	b->handle = slot + 1;       /* nonzero, stable per slot */
	b->cpu = cpu; b->pa = pa; b->gpuva = gpuva; b->size = pages*_PAGE_SIZE;
	c->handle = b->handle;
	c->offset = gpuva;          /* V3D address-space offset (nonzero) */
	return 0;
}

/* DRM core GEM_CLOSE: free the BO so its slot + GPU VA are reclaimed. */
static int ioc_close_bo(struct drm_gem_close *gc)
{
	struct pbo *b = bo_find(gc->handle);
	if (b == NULL) return 0;   /* already gone / never ours */
	va_free(b->gpuva, b->size / _PAGE_SIZE);
	if (b->cpu != NULL) munmap(b->cpu, b->size);
	b->used = 0;
	b->cpu = NULL;
	b->handle = 0;
	return 0;
}

/* Translate a GPU VA back to the CPU (uncached) pointer of the BO that covers it, or NULL
 * if no live BO maps it. Used only by the render-timeout instrumentation below. */
static void *gpuva_to_cpu(uint32_t gpuva)
{
	for (uint32_t i = 0; i < W.nbos; i++) {
		struct pbo *b = &W.bos[i];
		if (b->used && b->cpu != NULL && gpuva >= b->gpuva && gpuva < b->gpuva + b->size)
			return (char *)b->cpu + (gpuva - b->gpuva);
	}
	return NULL;
}

/* GFXH-1897 (Broadcom erratum, see linux v3d_gem.c v3d_clean_caches): a new L2TCACTL
 * flush must not be issued while a previous L2T flush is still in progress, or the new
 * flush malfunctions — the consumer then reads a stale/transient view. Our submit issues
 * L2T flushes back-to-back (bin pre-flush -> render pre-flush -> readback clean) with no
 * wait, so when the render-side flush lands while the bin/binner flush is still busy the
 * render fetches a stale tile-list and wedges (ct1ca parks past rcl_end, FRDONE never
 * fires). Identical demo content stalls ~50% of boots = exactly this flush-completion race.
 * Spin until the L2TFLS busy bit clears before each L2TCACTL write. */
static inline void l2t_flush_wait(volatile uint32_t *c0)
{
	uint32_t spins;
	for (spins = 1000000u; spins && (c0[CTL_L2TCACTL/4] & L2TCACTL_L2TFLS); spins--) {}
}

static int ioc_submit_cl(struct drm_v3d_submit_cl *s)
{
	volatile uint32_t *c0 = W.core0;
	volatile uint32_t *h = W.hub;
	uint32_t spins;
	/* Flush the MMU PTE cache + TLB before the job. ioc_create_bo writes fresh PTEs but
	 * never invalidated the MMU's cached translations, so a job whose CL/RT BOs were just
	 * mapped at new GPU VAs (every Quake frame allocates fresh BOs) is fetched through a
	 * stale TLB -> the render thread reads an unmapped/wrong VA and hangs at RCL packet 0.
	 * (The cube reuses one persistent job at stable VAs, so it never hit this.) Mirrors the
	 * linux v3d_mmu_flush_all sequence: MMUC flush, then MMU_CTL TLB clear, each spin-waited. */
	h[MMUC_CONTROL/4] = MMUC_FLUSH | MMUC_ENABLE;
	for (spins = 1000000u; spins && (h[MMUC_CONTROL/4] & MMUC_FLUSHING); spins--) {}
	h[MMU_CTL/4] |= MMU_CTL_TLB_CLEAR;
	for (spins = 1000000u; spins && (h[MMU_CTL/4] & MMU_CTL_TLB_CLEARING); spins--) {}
	c0[CTL_MISCCFG/4] = MISCCFG_OVRTMUOUT;
	/* Invalidate the V3D caches before the job: SLCACTL slices (TVCCS/TDCCS/UCC=uniform
	 * cache/ICC=instruction cache) + an L2T flush — matches the scout's v3d_invalidateCaches.
	 * The SLCACTL slices invalidation is essential for multi-frame rendering: without it the
	 * GPU serves stale uniforms from its uniform cache, so per-frame matrix/uniform changes
	 * never render (every frame looks like frame 0). */
	c0[CTL_SLCACTL/4] = SLCACTL_INVAL_ALL;
	l2t_flush_wait(c0);                       /* GFXH-1897: prior flush must be idle first */
	c0[CTL_L2TCACTL/4] = L2TCACTL_L2TFLS;
	/* --- bin (CT0); wait FLDONE --- */
	c0[CTL_INT_CLR/4] = INT_FLDONE|INT_FRDONE;
	c0[PTB_BPOS/4] = 0;
	if (s->qma) { c0[CLE_CT0QMA/4]=s->qma; c0[CLE_CT0QMS/4]=s->qms; }
	if (s->qts) { c0[CLE_CT0QTS/4]=CT0QTS_ENABLE|s->qts; }
	c0[CLE_CT0QBA/4]=s->bcl_start; c0[CLE_CT0QEA/4]=s->bcl_end;
	/* Wait for bin done, servicing binner OUT-OF-MEMORY: when the tile_alloc pool
	 * (CT0QMA/QMS) exhausts, the binner raises INT_OUTOMEM and stalls until handed a
	 * fresh pool via PTB_BPOA/BPOS. Arm our persistent overflow pool once per job (OOM
	 * is edge-signaled, so clear INT_OUTOMEM after servicing). Without this, large RTs
	 * (1080p ~510 tiles) hang the binner -> FLDONE never fires -> ~2.5 s spin timeout. */
	{
		int ovf_armed = 0;
		uint32_t sts;
		for (spins=8000000u; spins; spins--) {
			sts = c0[CTL_INT_STS/4];
			if (sts & INT_FLDONE) break;
			if ((sts & INT_OUTOMEM) && !ovf_armed && W.binovf_gpuva) {
				c0[PTB_BPOA/4] = W.binovf_gpuva;
				c0[PTB_BPOS/4] = W.binovf_bytes;
				c0[CTL_INT_CLR/4] = INT_OUTOMEM;
				ovf_armed = 1;
			}
		}
		if (spins == 0) {
			fprintf(stderr, "v3d-winsys: BIN TIMEOUT int_sts=0x%08x ct0cs=0x%08x "
				"ct0ca=0x%08x[%x..%x] gmp=0x%08x gmpvio=0x%08x mmu_ill=0x%08x ovf_armed=%d\n",
				c0[CTL_INT_STS/4], c0[0x0100/4], c0[0x0110/4], s->bcl_start, s->bcl_end,
				c0[0x0800/4], c0[0x0808/4], W.hub[MMU_ILLEGAL_ADDR/4], ovf_armed);
		}
	}
	c0[CTL_INT_CLR/4]=INT_FLDONE|INT_FRDONE;
	l2t_flush_wait(c0);                       /* GFXH-1897: bin/binner flush must complete before this one */
	c0[CTL_L2TCACTL/4]=L2TCACTL_L2TFLS;
	/* --- render (CT1); wait FRDONE --- */
	c0[CLE_CT1QBA/4]=s->rcl_start; c0[CLE_CT1QEA/4]=s->rcl_end;
	for (spins=16000000u; spins && !(c0[CTL_INT_STS/4]&INT_FRDONE); spins--) {}
	if (spins == 0) {
		uint32_t ca1 = c0[0x0114/4];
		fprintf(stderr, "v3d-winsys: RENDER TIMEOUT int_sts=0x%08x ct1cs=0x%08x "
			"ct1ca=0x%08x[%x..%x] ct1ea=0x%08x gmp=0x%08x gmpvio=0x%08x mmu_ill=0x%08x\n",
			c0[CTL_INT_STS/4], c0[0x0104/4], ca1, s->rcl_start, s->rcl_end,
			c0[0x010c/4], c0[0x0800/4], c0[0x0808/4], W.hub[MMU_ILLEGAL_ADDR/4]);
		/* re-read CT1CA to see if it is advancing (slow) or wedged (stall) */
		(void)ca1;
		fprintf(stderr, "v3d-winsys: RENDER ct1ca recheck=0x%08x\n", c0[0x0114/4]);
		/* TODO(quake-render-stall): diagnostic dump for the UNRESOLVED residual render stall
		 * (GFXH-1897 wait below mitigated the early CL-fetch wedge but a later shader-stage
		 * stall remains, int_sts=QPU bits). Remove once the residual is root-caused + fixed.
		 * INSTRUMENTATION (first 3 stalls): ct1ca runs tens of KB PAST rcl_end, i.e. into the
		 * binner's tile-list region (CT1 branched into a per-tile sub-list and wedged). Dump the
		 * actual RAM at the wedge point + the main RCL tail (via the uncached BO mapping) to
		 * discriminate: valid CL bytes = GPU-side stale fetch; zeros/garbage = binner output
		 * incomplete at render time (bin->render handoff race); "no BO" = stale/freed tile-list
		 * pointer (use-after-free). */
		{
			static int dbgn = 0;
			if (dbgn++ < 3) {
				uint32_t caw = ca1 & ~0xfu;
				uint32_t *rp = (uint32_t *)gpuva_to_cpu((s->rcl_end - 16u) & ~0xfu);
				uint32_t *cp = (uint32_t *)gpuva_to_cpu(caw);
				fprintf(stderr, "v3d-winsys: RCLTAIL gpuva=0x%08x cpu=%p:", (s->rcl_end-16u)&~0xfu, (void*)rp);
				if (rp) { for (int i = 0; i < 8; i++) fprintf(stderr, " %08x", rp[i]); }
				else { fprintf(stderr, " (no BO)"); }
				fprintf(stderr, "\n");
				fprintf(stderr, "v3d-winsys: WEDGEAT gpuva=0x%08x cpu=%p:", caw, (void*)cp);
				if (cp) { for (int i = 0; i < 8; i++) fprintf(stderr, " %08x", cp[i]); }
				else { fprintf(stderr, " (no BO covers ct1ca -> unmapped/freed tile-list pointer)"); }
				fprintf(stderr, "\n");
			}
		}
	}
	/* L2T flush so RT stores reach RAM before CPU readback (scout finding). */
	l2t_flush_wait(c0);                       /* GFXH-1897: render flush must complete first */
	c0[CTL_L2TCACTL/4]=L2TCACTL_L2TFLS|(2u<<1); /* FLM_CLEAN */
	return 0;
}

/* device info from the real Pi4 IDENTs (rpi4-v3d-scout): IDENT0=0x04443356, etc. */
static int ioc_get_param(struct drm_v3d_get_param *gp)
{
	switch (gp->param) {
	case DRM_V3D_PARAM_V3D_UIFCFG:        gp->value = 0x00000045; return 0;
	case DRM_V3D_PARAM_V3D_HUB_IDENT1:    gp->value = 0x000e1124; return 0;
	case DRM_V3D_PARAM_V3D_HUB_IDENT2:    gp->value = 0x00000100; return 0;
	case DRM_V3D_PARAM_V3D_HUB_IDENT3:    gp->value = 0x00000e00; return 0;
	case DRM_V3D_PARAM_V3D_CORE0_IDENT0:  gp->value = 0x04443356; return 0; /* "V3D" */
	case DRM_V3D_PARAM_V3D_CORE0_IDENT1:  gp->value = 0x81001422; return 0;
	case DRM_V3D_PARAM_V3D_CORE0_IDENT2:  gp->value = 0x40078121; return 0;
	case DRM_V3D_PARAM_SUPPORTS_TFU:      gp->value = 1; return 0;
	/* V3DV's device_has_expected_features() (v3dv_device.c) gates physical-device
	 * creation on TFU && CSD && CACHE_FLUSH && CPU_QUEUE && MULTISYNC — all must be 1
	 * or vkEnumeratePhysicalDevices returns VK_ERROR_INITIALIZATION_FAILED ("requires
	 * kernel 6.8+"). The V3D 4.2 HW *has* CSD (compute) + the TFU; CPU_QUEUE is a
	 * kernel-side convenience we don't implement. We advertise all 1 so device-create
	 * succeeds; classic graphics (vkQuake) never dispatches compute or CPU jobs, so the
	 * unimplemented SUBMIT_CSD / CPU-queue paths are not exercised. Revisit if a Tier-2+
	 * client issues vkCmdDispatch / a CPU job. MULTISYNC_EXT MUST be 1: this Mesa's
	 * handle_cl_job unconditionally uses DRM_V3D_SUBMIT_EXTENSION (no legacy sync path);
	 * the winsys ignores the chained extensions (submit is synchronous), so 1 is safe. */
	case DRM_V3D_PARAM_SUPPORTS_CSD:      gp->value = 1; return 0;
	case DRM_V3D_PARAM_SUPPORTS_CACHE_FLUSH: gp->value = 1; return 0;
	case DRM_V3D_PARAM_SUPPORTS_MULTISYNC_EXT: gp->value = 1; return 0;
	case DRM_V3D_PARAM_SUPPORTS_PERFMON:       gp->value = 0; return 0;
	case DRM_V3D_PARAM_SUPPORTS_CPU_QUEUE:     gp->value = 1; return 0;
	default: gp->value = 0; return 0;
	}
}

/* True once the full-screen render target has been backed by the scanout framebuffer
 * (render-to-scanout). The present layer queries this to skip the CPU readback/blit/fb0
 * copies — the GPU already wrote the displayed surface. */
int v3d_phoenix_scanout_active(void);
int v3d_phoenix_scanout_active(void) { return W.scanout_claimed; }

/* The single entry the libdrm shim's drmIoctl() dispatches into. */
int phoenix_v3d_ioctl(int fd, unsigned long request, void *arg);

int phoenix_v3d_ioctl(int fd, unsigned long request, void *arg)
{
	(void)fd;
	/* Mesa builds requests as DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_*, ...), so the
	 * ioctl NR field is DRM_COMMAND_BASE (0x40) + the bare command. Strip the base
	 * to recover the DRM_V3D_* command our cases are keyed on. (Missing this made
	 * every ioctl fall through to the default -> GET_PARAM returned 0 -> ver=0 ->
	 * v3d_get_device_info failed -> v3d_screen_create NULL.) */
	unsigned cmd = _IOC_NR(request) - DRM_COMMAND_BASE;
	/* GET_PARAM / WAIT_BO return constants and touch no MMIO — serve them WITHOUT
	 * winsys_init() so screen-create (which is GET_PARAM-only, allocates no BO) needs
	 * no V3D power-on / MMU bring-up. The MMIO paths below init lazily. */
	switch (cmd) {
	case DRM_V3D_GET_PARAM:
		return ioc_get_param(arg);
	case DRM_V3D_WAIT_BO:
		return 0;   /* submit is synchronous */
	}
	/* Everything below touches HUB/CORE MMIO + the MMU PT -> requires power-on. */
	if (winsys_init() != 0)
		return -1;
	/* DRM core GEM_CLOSE (NR 0x09, below DRM_COMMAND_BASE so not a DRM_V3D_* cmd):
	 * Mesa's bufmgr issues it to free a BO; reclaim the slot + GPU VA. */
	if (_IOC_NR(request) == _IOC_NR(DRM_IOCTL_GEM_CLOSE))
		return ioc_close_bo(arg);
	switch (cmd) {
	case DRM_V3D_CREATE_BO:
		return ioc_create_bo(arg);
	case DRM_V3D_GET_BO_OFFSET: {
		struct drm_v3d_get_bo_offset *g = arg;
		struct pbo *b = bo_find(g->handle);
		if (!b)
			return -EINVAL;
		g->offset = b->gpuva;
		return 0;
	}
	case DRM_V3D_MMAP_BO: {
		/* Our BOs are already CPU-mapped (uncached); return the va as the offset
		 * and have the libdrm-shim mmap() return it directly. */
		struct drm_v3d_mmap_bo *m = arg;
		struct pbo *b = bo_find(m->handle);
		if (!b)
			return -EINVAL;
		m->offset = (uint64_t)(uintptr_t)b->cpu;
		return 0;
	}
	case DRM_V3D_SUBMIT_CL:
		return ioc_submit_cl(arg);
	default:
		return 0;   /* perfmon/tfu/csd: no-op for now */
	}
}
