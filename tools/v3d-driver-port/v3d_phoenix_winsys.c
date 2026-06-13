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
#define PTB_BPOS            0x030cu
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
	W.inited = 1;
	return 0;
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
	cpu = mmap(NULL, pages*_PAGE_SIZE, PROT_READ|PROT_WRITE,
		MAP_UNCACHED|MAP_CONTIGUOUS|MAP_ANONYMOUS, -1, 0);
	if (cpu==MAP_FAILED) { va_free(gpuva, pages); return -ENOMEM; }
	pa = (uintptr_t)va2pa(cpu);
	for (uint32_t i=0;i<pages;i++)
		W.pt[(gpuva>>PAGE_SHIFT)+i] = (uint32_t)((pa>>PAGE_SHIFT)+i)|PTE_W|PTE_V;

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
	c0[CTL_L2TCACTL/4] = L2TCACTL_L2TFLS;
	/* --- bin (CT0); wait FLDONE --- */
	c0[CTL_INT_CLR/4] = INT_FLDONE|INT_FRDONE;
	c0[PTB_BPOS/4] = 0;
	if (s->qma) { c0[CLE_CT0QMA/4]=s->qma; c0[CLE_CT0QMS/4]=s->qms; }
	if (s->qts) { c0[CLE_CT0QTS/4]=CT0QTS_ENABLE|s->qts; }
	c0[CLE_CT0QBA/4]=s->bcl_start; c0[CLE_CT0QEA/4]=s->bcl_end;
	for (spins=8000000u; spins && !(c0[CTL_INT_STS/4]&INT_FLDONE); spins--) {}
	c0[CTL_INT_CLR/4]=INT_FLDONE|INT_FRDONE;
	c0[CTL_L2TCACTL/4]=L2TCACTL_L2TFLS;
	/* --- render (CT1); wait FRDONE --- */
	c0[CLE_CT1QBA/4]=s->rcl_start; c0[CLE_CT1QEA/4]=s->rcl_end;
	for (spins=16000000u; spins && !(c0[CTL_INT_STS/4]&INT_FRDONE); spins--) {}
	/* L2T flush so RT stores reach RAM before CPU readback (scout finding). */
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
	case DRM_V3D_PARAM_SUPPORTS_CSD:      gp->value = 0; return 0;
	case DRM_V3D_PARAM_SUPPORTS_CACHE_FLUSH: gp->value = 1; return 0;
	default: gp->value = 0; return 0;
	}
}

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
