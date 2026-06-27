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
#define MMU_CTL_PTI_ABORT   (1u<<19)   /* = PT_INVALID_ABORT */
/* Full MMU fault config (mirror linux v3d_mmu_set_page_table). Our prior config set ABORT
 * without PT_INVALID_ENABLE, so PT-invalid detection was effectively OFF — an illegal/unmapped
 * access was neither aborted nor reported (it could hang). Enable detection + abort + INT for
 * PT-invalid, write-violation and cap-exceeded, and arm a scratch page so an illegal access is
 * redirected to harmless memory instead of hanging. */
#define MMU_CTL_PTI_ENABLE      (1u<<16)
#define MMU_CTL_PTI_INT         (1u<<18)
#define MMU_CTL_WRITEVIO_ABORT  (1u<<11)
#define MMU_CTL_WRITEVIO_INT    (1u<<10)
#define MMU_CTL_CAPEXC_ABORT    (1u<<26)
#define MMU_CTL_CAPEXC_INT      (1u<<25)
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
#define CTL_L2CACTL         0x0020u    /* general L2 cache control (V3D 4.x) */
#define L2CACTL_L2CENA      (1u<<0)    /* enable the L2 cache */
#define L2CACTL_L2CCLR      (1u<<2)    /* clear the L2 cache */
#define CTL_L2TFLSTA        0x0034u    /* L2T flush start address */
#define CTL_L2TFLEND        0x0038u    /* L2T flush end address */
#define CTL_L2TCACTL        0x0030u
#define L2TCACTL_L2TFLS     (1u<<0)
#define L2TCACTL_FLM_CLEAN  (2u<<1)    /* FLM field = CLEAN (write back dirty L2T lines to RAM) */
#define L2TCACTL_TMUWCF     (1u<<8)    /* TMU write-combiner flush (drain partial tiled writes) */
#define CTL_SLCACTL         0x0024u    /* slices cache control (V3D 4.x) */
#define SLCACTL_INVAL_ALL   0x0f0f0f0fu /* invalidate TVCCS/TDCCS/UCC(uniform)/ICC(instr) */
#define PTB_BPCA            0x0300u   /* binner primitive-list current address (advances as the binner runs) */
#define PTB_BPCS            0x0304u   /* binner primitive-list current status */
#define PTB_BPOA            0x0308u   /* binner pool overflow address (GPU VA) */
#define PTB_BPOS            0x030cu   /* binner pool overflow size (bytes) */
#define GMP_STATUS          0x0800u   /* global memory protection status (RD/WR_ACTIVE, VIO) */
#define GMP_CFG             0x0804u   /* GMP config (STOP_REQ for safe AXI drain) */
#define GMP_CFG_STOP_REQ    (1u<<1)   /* request the GMP to quiesce outstanding AXI transactions */
#define GMP_STATUS_RD_WR_CNT 0x7f7f0000u /* RD_COUNT(22:16)|WR_COUNT(30:24) — nonzero = txns in flight */
#define GMP_STATUS_CFG_BUSY (1u<<3)
/* HUB block (W.hub[0]): the AXI config. GFXH-1383 — the V3D AXI master must cap its max burst
 * length (MAX_LEN field, bits 3:0). Linux v3d_reset_by_bridge restores it to MAX_LEN_MASK after
 * a bridge SW_INIT; if our power-on path leaves it unset/wrong the AXI can deadlock under
 * sustained load (GMP RD+WR stuck active, binner ct0ca frozen) — the workload-dependent wedge. */
#define HUB_AXICFG          0x0000u
#define HUB_AXICFG_MAX_LEN  0x0000000fu
/* HUB interrupt status/clear (HUB block, see linux v3d_regs.h V3D_HUB_INT_*). The TFU
 * raises HUB_INT bit1 (TFUC = "conversion complete") when a job finishes. HUB_INT_STS
 * (0x50) is the RAW status latch (the separate HUB_INT_MSK_STS at 0x5c gates the CPU IRQ
 * line), so polling STS works without unmasking — we have no V3D IRQ handler. */
#define HUB_INT_STS         0x0050u
#define HUB_INT_CLR         0x0058u
#define HUB_INT_MSK_STS     0x005cu   /* mask status (diagnostic only; STS is raw) */
#define HUB_INT_TFUC        (1u<<1)   /* TFU conversion complete */
#define HUB_INT_TFUF        (1u<<0)   /* TFU conversion failed */
/* Texture Formatting Unit (HUB block, V3D 4.2 / ver<71 register layout — see linux
 * v3d_regs.h V3D_TFU_*(ver) and v3d_sched.c v3d_tfu_job_run). The TFU is a small
 * fixed-function unit that copies a (raster or tiled) source buffer into a tiled/UIF
 * destination image: program the input/output addresses+strides+the format/tiling
 * config word, then write ICFG (with IOC) to kick. Like the CL submit, the addresses
 * here are GPU virtual addresses translated by the V3D MMU (the TFU sits behind it),
 * so Mesa has already folded each BO's GPU-VA base into iia/ioa — we program them as-is.
 * All offsets are HUB-relative (V3D_WRITE in linux targets hub_regs == our W.hub). */
#define TFU_CS              0x0400u   /* control/status: bit0 BUSY, bit31 TFURST */
#define TFU_CS_BUSY         (1u<<0)
#define TFU_ICFG            0x0408u   /* input config (format/tiling/ttype/opad); write kicks */
#define TFU_ICFG_IOC        (1u<<0)   /* raise the done interrupt when the job completes */
#define TFU_IIA             0x040cu   /* input image address (GPU VA) */
#define TFU_ICA             0x0410u   /* input chroma address (GPU VA; 0 for non-planar) */
#define TFU_IIS             0x0414u   /* input image stride */
#define TFU_IUA             0x0418u   /* input u-plane address (GPU VA; 0 for non-planar) */
#define TFU_IOA             0x041cu   /* output image address (GPU VA) + dest tiling format */
#define TFU_IOS             0x0420u   /* output image size: (height<<16)|width */
#define TFU_COEF0           0x0424u   /* YUV coefficient 0 (bit31 USECOEF gates COEF1..3) */
#define TFU_COEF0_USECOEF   (1u<<31)
#define TFU_COEF1           0x0428u
#define TFU_COEF2           0x042cu
#define TFU_COEF3           0x0430u
/* Binner tile-allocation overflow pool. When the binner exhausts the per-job
 * tile_alloc memory Mesa supplies via CT0QMA/QMS, it raises INT_OUTOMEM and stalls
 * until handed a fresh pool via PTB_BPOA/BPOS (linux v3d_overflow_mem_work). The
 * pool need grows with tile count: 1024x768 (~192 tiles) never overflowed, but
 * 1920x1080 (~510 tiles) does. We pre-allocate one generous pool at init and arm it
 * on the first OUTOMEM of each job (Quake's per-frame overflow stays well under this). */
/* Binner overflow (spill) pool. The binner spills per-tile primitive lists here when Mesa's
 * initial tile_alloc (CT0QMA/QMS) is exhausted; a complex 1080p frame can need many MiB. Linux
 * allocates a FRESH 256 KiB BO per OUTOMEM event, UNBOUNDED (v3d_overflow_mem_work) — our prior
 * 4 MiB fixed pool was too small, so a heavy scene exhausted it and the binner wedged EVERY frame
 * (OUTOMEM|SPILLUSE, ovf_armed=exhausted, ~3 fps). 64 MiB covers far more; the servicer hands the
 * whole remaining pool on the first OUTOMEM so the binner has it all at once. (A truly pathological
 * frame exceeding 64 MiB would still need Linux-style unbounded fresh allocation — logged.) */
#define BINOVF_PAGES        8192u     /* 32 MiB persistent binner-overflow/spill pool (8x the old 4 MiB) */
#define BINOVF_CHUNK_BYTES  (BINOVF_PAGES * 4096u)  /* hand the whole pool on the first OUTOMEM */
#define CTL_MISCCFG         0x0018u
#define MISCCFG_OVRTMUOUT   (1u<<0)
#define MISCCFG_QRMAXCNT_SHIFT 1u    /* QRMAXCNT = MISCCFG bits 3:1 (QPU reserve bin-vs-render split) */
/* Tune the binner-coord vs render-frag QPU split. -1 = leave the firmware default (Linux behaviour
 * on V3D 4.2 — it never writes MISCCFG). 0..7 = write MISCCFG=(QRMAXCNT<<1)|0 ONCE at init: low
 * favours the render (frag) shaders, high favours the binner (coord) shaders. The render-stall
 * residual is a balance point — find a value with 0 bin AND 0 render wedges. */
#define V3D_QRMAXCNT        (2)

/* GPU VA space: bump-allocate page-aligned, starting past the null guard. */
#define GPUVA_BASE          0x100000u
/* MMU flat page table size. Each PTE (4 bytes) maps one 4 KiB page, so one 4 KiB
 * PT page = 1024 PTEs = 4 MiB of GPU VA window. A 256x256 RT fits in one page, but
 * a 1024x768 color+depth target (3 MiB each) overflows it -> PTEs written past the
 * table (OOB) at unmapped VAs -> the GPU store lands nowhere -> all-zero RT. Grow to
 * 32 PT pages = 128 MiB of GPU VA, enough for fullscreen color+depth+tile state plus
 * texture/CL working set (Quake). PT must be physically contiguous (the MMU walks it
 * as a flat array from MMU_PT_PA_BASE). */
/* STEP-1 EXPERIMENT (render-stall hunt, task #13): V3D_VA_NO_RECYCLE makes va_alloc
 * never reuse a freed GPU VA (monotonic bump only). Tests whether the intermittent
 * render wedge is caused by a recycled VA whose GPU cache still holds the prior BO's
 * (stale) content — a fresh never-used VA cannot have a stale cache entry. Needs a larger
 * VA window since per-frame BOs are never reclaimed; sized for a short validation run.
 * HELD OFF (advisor 2026-06-21): a 12-boot A/B cannot resolve a 15-30% base rate, and the
 * "zeroing BOs changed wedge content garbage->zeros but NOT the rate" evidence shows CT1
 * overruns ct1ea regardless of memory content -> the wedge is not a memory/VA/cache effect.
 * Pivoted to STEP-3 first: instrument cold-power-on HW state (clock cfg-vs-measured, PLL,
 * temp) and correlate clean-vs-stalled boots for a deterministic discriminator. */
#define V3D_VA_NO_RECYCLE   0
#if V3D_VA_NO_RECYCLE
#define GPUVA_PT_PAGES      512u   /* 512 * 4 MiB = 2 GiB monotonic VA window (no reclaim) */
#else
#define GPUVA_PT_PAGES      64u    /* 64 * 4 MiB = 256 MiB GPU VA window */
#endif
#define GPUVA_PT_ENTRIES    (GPUVA_PT_PAGES * (_PAGE_SIZE / 4u))   /* total PTEs */

struct pbo {            /* Phoenix BO */
	uint32_t handle;
	void    *cpu;       /* mmap'd uncached va */
	uintptr_t pa;       /* physical */
	uint32_t gpuva;     /* assigned V3D virtual address (= drm offset) */
	uint32_t size;
	int      used;      /* slot in use (freed by GEM_CLOSE -> reusable) */
	int      scanout;   /* this BO aliases the scanout surface (clear W.scanout_claimed on close) */
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
	uint32_t binovf_used;     /* bytes of the pool handed out this job (re-armable overflow) */
	uintptr_t scratch_pa;     /* MMU illegal-access scratch page PA (0 = none); redirects faults */
	uint32_t scanout_pa;      /* HDMI framebuffer physical addr / buffer 0 (0 = unavailable) */
	uint32_t scanout_pa2;     /* multi-buffer: buffer 1 PA (= scanout_pa + pitch*phys_h), else 0 */
	uint32_t scanout_pa3;     /* triple-buffer: buffer 2 PA (= scanout_pa + 2*pitch*phys_h), else 0 */
	uint32_t scanout_phys_h;  /* physical (displayed) height — page-flip pans by buffer*phys_h rows */
	uint32_t scanout_bytes;   /* one buffer's byte size (pitch*phys_h) */
	int      scanout_nbuf;    /* number of page-flip buffers granted: 1 (none), 2 (double), 3 (triple) */
	int      scanout_double;  /* convenience: scanout_nbuf >= 2 (page-flip available) */
	int      scanout_claim_idx; /* multi-buffer: which buffer the next scanout BO is backed by */
	int      scanout_claimed; /* single-buffer: only one BO may alias the single scanout surface */
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
int v3d_phoenix_reset(void);     /* v3d_phoenix_power.c — true reset cycle (hold + power on) */
void v3d_phoenix_logColdState(void);  /* v3d_phoenix_power.c — STEP-3 cold-power-on state probe */

/* Render-timeout counter, exported for the stall-repro harness (rpi4-v3d-stalltest):
 * incremented every time a CT1 render submit hits the spin-timeout. Lets the harness
 * count stalls across many in-boot submits instead of one-boot-per-sample roulette. */
volatile unsigned v3d_phoenix_render_timeouts = 0;
static uint32_t va_alloc(uint32_t pages);   /* defined below; used by the init overflow pool */
static void apply_core_regs(void);          /* defined below; used by winsys_init + reset path */

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
	/* STEP-3 render-stall discriminator: log the firmware-controlled cold-power-on state
	 * (V3D clock cfg-vs-measured, PLL/clock state, temp, throttle, core voltage, PM_GRAFX)
	 * plus the live V3D core identity. Captured ONCE per boot so a multi-boot run can diff a
	 * stalled boot's line against a clean one for a deterministic separator. CORE0_IDENT0 of
	 * a powered-but-mis-clocked core would read back wrong/0xdeadXXXX. */
	v3d_phoenix_logColdState();
	fprintf(stderr, "v3d-coldstate: CORE0_IDENT0=0x%08x HUB_IDENT1=0x%08x cold_HUB_AXICFG=0x%08x "
		"cold_GMP_STATUS=0x%08x cold_MISCCFG=0x%08x (QRMAXCNT=%u)\n",
		W.core0[0x0000/4], W.hub[0x000c/4], W.hub[HUB_AXICFG/4], W.core0[GMP_STATUS/4],
		W.core0[CTL_MISCCFG/4], (W.core0[CTL_MISCCFG/4] >> 1) & 0x7u);
	/* MMU page table: GPUVA_PT_PAGES contiguous pages = GPUVA_PT_PAGES*4 MiB GPU VA. */
	W.pt = mmap(NULL, GPUVA_PT_PAGES*_PAGE_SIZE, PROT_READ|PROT_WRITE,
		MAP_UNCACHED|MAP_CONTIGUOUS|MAP_ANONYMOUS, -1, 0);
	if (W.pt==MAP_FAILED) return -ENOMEM;
	W.pt_pa = (uintptr_t)va2pa((void*)W.pt);
	for (uint32_t i=0;i<GPUVA_PT_ENTRIES;i++) W.pt[i]=0;
	/* MMU illegal-access scratch page (linux v3d mmu_scratch): one harmless page the MMU
	 * redirects faulting accesses to, armed via MMU_ILLEGAL_ADDR in apply_core_regs. Without it
	 * an illegal access has nowhere to land and can stall the bus. */
	{
		void *sp = mmap(NULL, _PAGE_SIZE, PROT_READ|PROT_WRITE,
			MAP_UNCACHED|MAP_CONTIGUOUS|MAP_ANONYMOUS, -1, 0);
		if (sp != MAP_FAILED) {
			memset(sp, 0, _PAGE_SIZE);
			W.scratch_pa = (uintptr_t)va2pa(sp);
		}
	}
	/* NOTE: assumes V3D already powered on (rpi4-v3d-scout v3d_powerOn sequence). */
	/* MMU base/enable, MMUC, + general-L2 clear+enable. The L2C enable mirrors linux
	 * v3d_init_core (L2CACTL = L2CCLR|L2CENA): the QPUs fetch shader instructions/uniforms
	 * through L2C and our init never enabled it (an init-correctness gap regardless; it did
	 * not by itself resolve the intermittent first-frame render stall). Factored into
	 * apply_core_regs() so the in-job reset path can re-establish identical state. */
	apply_core_regs();
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
			/* Zero the pool: Phoenix mmap(MAP_CONTIGUOUS) returns NON-zeroed DRAM (the kernel
			 * zeroes only the vm_object struct, not page contents — unlike Linux's __GFP_ZERO
			 * shmem). The binner writes tile-list next-block pointers into this pool; any slot
			 * it doesn't populate must read 0 (a clean halt), not cold-boot garbage that CT1
			 * would follow as a wild pointer. Root cause of the intermittent render wedge. */
			memset(cpu, 0, BINOVF_PAGES*_PAGE_SIZE);
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

/* Scanout init with double-buffer detection (render-stall complete fix). Given the displayed
 * framebuffer (pa, w, h, pitch), probe the firmware's granted VIRTUAL height: if plo allocated
 * >= 2x physical, a second buffer sits at pa + pitch*h and we can page-flip — the GPU renders +
 * resolves to the OFF-screen buffer and we pan the display to it, so the live (displayed) buffer
 * is never GPU-written (no display contention -> no depth/fragment-pipeline stall). Returns 2 if
 * double-buffer is active, 1 if single (caller falls back to the in-place blit-resolve). */
extern unsigned v3d_phoenix_fb_virtual_height(void);
int v3d_phoenix_scanout_init(uint32_t pa, uint32_t w, uint32_t h, uint32_t pitch);
int v3d_phoenix_scanout_init(uint32_t pa, uint32_t w, uint32_t h, uint32_t pitch)
{
	(void)w;
	W.scanout_pa = pa;
	W.scanout_phys_h = h;
	W.scanout_bytes = pitch * h;
	W.scanout_claim_idx = 0;
	W.scanout_claimed = 0;
	unsigned vh = (h != 0u && pitch != 0u) ? v3d_phoenix_fb_virtual_height() : 0u;
	unsigned nbuf = (h != 0u) ? (vh / h) : 1u;   /* how many stacked buffers the firmware granted */
	if (nbuf > 3u) nbuf = 3u;                     /* we use at most 3 (triple-buffer) */
	if (nbuf < 1u) nbuf = 1u;
	W.scanout_nbuf = (int)nbuf;
	W.scanout_double = (nbuf >= 2u);
	W.scanout_pa2 = (nbuf >= 2u) ? (pa + pitch * h) : 0u;        /* buffer 1 directly below buffer 0 */
	W.scanout_pa3 = (nbuf >= 3u) ? (pa + 2u * pitch * h) : 0u;   /* buffer 2 below buffer 1 */
	fprintf(stderr, "v3d-winsys: scanout init pa=0x%08x %ux%u pitch=%u virt_h=%u -> %d buffer(s) %s "
		"(buf1=0x%08x buf2=0x%08x)\n", pa, w, h, pitch, vh, W.scanout_nbuf,
		(nbuf >= 3u) ? "TRIPLE-BUFFER+page-flip" : (nbuf == 2u) ? "DOUBLE-BUFFER+page-flip" : "single (blit-resolve)",
		W.scanout_pa2, W.scanout_pa3);
	return W.scanout_nbuf;
}

/* Page-flip the display to buffer `buf` (0 or 1). Called after resolving into that buffer. */
extern void v3d_phoenix_fb_flip(unsigned yoff);
void v3d_phoenix_flip(int buf);
void v3d_phoenix_flip(int buf)
{
	if (W.scanout_double)
		v3d_phoenix_fb_flip((buf != 0) ? W.scanout_phys_h : 0u);
}

int v3d_phoenix_scanout_double(void);
int v3d_phoenix_scanout_double(void) { return W.scanout_double; }

int v3d_phoenix_scanout_nbuf(void);
int v3d_phoenix_scanout_nbuf(void) { return W.scanout_nbuf ? W.scanout_nbuf : 1; }

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
#if !V3D_VA_NO_RECYCLE
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
#endif
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
	/* A zero-byte BO request (e.g. vkQuake's empty lightstyles buffer) would compute 0 pages
	 * and mmap(len=0) fails with "BO mmap FAILED (0 pages)". Round up to one page so the
	 * handle/GPU-VA are valid (mirrors Mesa always allocating full pages) — a legitimate
	 * 0-size allocation just gets one unused page rather than a spurious -ENOMEM. */
	uint32_t pages = (c->size + _PAGE_SIZE - 1)/_PAGE_SIZE;
	if (pages == 0)
		pages = 1;
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
	/* VA-collision detector (render-stall probe, task #13): behavior-neutral check for two LIVE
	 * BOs overlapping the same GPU VA — the only aliasing that could overwrite a live RCL/tile-
	 * list with another BO's content. If any PTE in the new range is already VALID, a previous
	 * BO is still mapped there. Logged, not fixed, so a wedge log can be correlated with a real
	 * collision (vs the fdbgs=EZTEST drain-stall reading, which implies NO corruption). */
	for (uint32_t i = 0; i < pages; i++) {
		if (W.pt[(gpuva>>PAGE_SHIFT)+i] & PTE_V) {
			fprintf(stderr, "v3d-winsys: VA COLLISION new handle gpuva=0x%x page %u already "
				"mapped (PTE=0x%08x) — live-BO overlap\n",
				gpuva, i, W.pt[(gpuva>>PAGE_SHIFT)+i]);
			break;
		}
	}
	/* A SCANOUT BO (flags bit 1 = V3D_CREATE_BO_SCANOUT, set by Mesa for the
	 * full-screen render target) is backed by the HDMI framebuffer's physical pages
	 * instead of fresh DRAM: the GPU raster-stores straight to the displayed surface
	 * (render-to-scanout), eliminating the per-frame glReadPixels/blit/fb0 CPU copies.
	 * The scanout PA is physically contiguous, so map it pa+i. */
	/* Select the scanout buffer this BO should alias, or 0 (not a scanout BO). In DOUBLE-BUFFER
	 * mode the first scanout RT is backed by buffer 0, the second by buffer 1 (the renderer
	 * creates two scanout FBOs and page-flips between them); in single-buffer mode only one RT
	 * may claim the (live) fb. */
	uint32_t sel_pa = 0;
	if (((c->flags & 0x2u) || W.next_scanout) && W.scanout_pa) {
		if (W.scanout_double) {
			if (W.scanout_claim_idx < W.scanout_nbuf)
				sel_pa = (W.scanout_claim_idx == 0) ? W.scanout_pa
				       : (W.scanout_claim_idx == 1) ? W.scanout_pa2
				       : W.scanout_pa3;
		}
		else if (!W.scanout_claimed) {
			sel_pa = W.scanout_pa;
		}
	}
	if (sel_pa) {
		W.next_scanout = 0;
		/* Back the visible rows with the scanout buffer's physical pages. The RT BO is a little
		 * larger than the fb (V3D stores tile-aligned rows — 1088 for a 1080 RT — plus driver
		 * padding); those extra rows are never displayed, so map them to fresh scratch DRAM. */
		uint32_t scanout_pages = W.scanout_bytes / _PAGE_SIZE;
		if (scanout_pages > pages) scanout_pages = pages;
		cpu = mmap(NULL, pages*_PAGE_SIZE, PROT_READ|PROT_WRITE,
			MAP_CONTIGUOUS|MAP_UNCACHED|MAP_ANONYMOUS, -1, 0);
		if (cpu==MAP_FAILED) { va_free(gpuva, pages); return -ENOMEM; }
		pa = sel_pa;
		for (uint32_t i=0;i<pages;i++) {
			uint32_t pfn = (i < scanout_pages)
				? (sel_pa>>PAGE_SHIFT)+i
				: (uint32_t)((uintptr_t)va2pa((char*)cpu + (size_t)i*_PAGE_SIZE) >> PAGE_SHIFT);
			W.pt[(gpuva>>PAGE_SHIFT)+i] = pfn|PTE_W|PTE_V;
		}
		fprintf(stderr, "v3d-winsys: RT scanout buf%d PA 0x%08x gpuva 0x%x, %u/%u pages "
			"(%u scratch) — %s\n", W.scanout_double ? W.scanout_claim_idx : 0, sel_pa, gpuva,
			scanout_pages, pages, pages-scanout_pages,
			W.scanout_double ? "double-buffer" : "render-to-scanout");
		if (W.scanout_double) W.scanout_claim_idx++; else W.scanout_claimed = 1;
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
		/* Zero freshly-allocated BO memory. Phoenix mmap(MAP_CONTIGUOUS) returns NON-zeroed
		 * DRAM (kernel zeroes only the vm_object struct, not pages — unlike Linux __GFP_ZERO
		 * shmem). Binner-output BOs (tile_alloc / tile_state / CLs) chain per-tile sub-lists via
		 * pointers written into this memory; an unpopulated slot holding cold-boot garbage makes
		 * CT1 branch to a wild address past rcl_end and wedge (the intermittent render stall).
		 * The huge scanout-backed RT (above) is excluded — it is fully rendered each frame and
		 * claimed once, and zeroing 8 MB of uncached fb memory per frame would tank fps. */
		memset(cpu, 0, pages*_PAGE_SIZE);
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
	b->scanout = (W.scanout_pa != 0 && (pa == W.scanout_pa ||
		(W.scanout_pa2 != 0 && pa == W.scanout_pa2) ||
		(W.scanout_pa3 != 0 && pa == W.scanout_pa3)));   /* this BO aliases a scanout buffer */
	c->handle = b->handle;
	c->offset = gpuva;          /* V3D address-space offset (nonzero) */
	return 0;
}

/* DRM core GEM_CLOSE: free the BO so its slot + GPU VA are reclaimed. */
static int ioc_close_bo(struct drm_gem_close *gc)
{
	struct pbo *b = bo_find(gc->handle);
	if (b == NULL) return 0;   /* already gone / never ours */
	/* If the scanout-backed RT is being freed, release the single-claim so the NEXT full-screen
	 * RT can re-acquire scanout backing. Without this, a freed+realloc'd RT silently fell back to
	 * plain DRAM while the present path still expected render-to-scanout -> a frozen screen. */
	if (b->scanout) {
		if (W.scanout_double) {
			if (W.scanout_claim_idx > 0) W.scanout_claim_idx--;
		}
		else {
			W.scanout_claimed = 0;
		}
		b->scanout = 0;
	}
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

/* Instrument-validation probe (render-stall, task #13): log EVERY live BO whose VA range
 * contains gpuva — handle, base, size, and (gpuva-base). If >1 matches, gpuva_to_cpu's
 * first-match is ambiguous and any "head is zero" reading is a WRONG-BO artifact (overlapping
 * VA ranges in the BO table even with disjoint PTEs) rather than a real RCL-offset bug. */
static void gpuva_describe(const char *label, uint32_t gpuva)
{
	unsigned matches = 0;
	for (uint32_t i = 0; i < W.nbos; i++) {
		struct pbo *b = &W.bos[i];
		if (b->used && b->cpu != NULL && gpuva >= b->gpuva && gpuva < b->gpuva + b->size) {
			fprintf(stderr, "v3d-winsys: %s gpuva=0x%08x -> BO handle=%u base=0x%08x size=%u "
				"off=0x%x cpu=%p\n", label, gpuva, b->handle, b->gpuva, b->size,
				(unsigned)(gpuva - b->gpuva), b->cpu);
			matches++;
		}
	}
	if (matches == 0)
		fprintf(stderr, "v3d-winsys: %s gpuva=0x%08x -> NO live BO covers it\n", label, gpuva);
	else if (matches > 1)
		fprintf(stderr, "v3d-winsys: %s gpuva=0x%08x -> %u OVERLAPPING BOs (gpuva_to_cpu ambiguous!)\n",
			label, gpuva, matches);
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

/* Flush the MMU PTE cache + clear the TLB (mirror linux v3d_mmu_flush_all). ioc_create_bo
 * writes fresh PTEs but never invalidates the MMU's cached translations, so a job whose
 * BOs were just mapped at new GPU VAs is fetched through a stale TLB. Both the CL and the
 * TFU submit paths must do this before kicking work that references freshly-created BOs
 * (every texture upload allocates a staging BO + a destination image BO). */
static void mmu_flush_tlb(volatile uint32_t *h)
{
	uint32_t spins;
	h[MMUC_CONTROL/4] = MMUC_FLUSH | MMUC_ENABLE;
	for (spins = 1000000u; spins && (h[MMUC_CONTROL/4] & MMUC_FLUSHING); spins--) {}
	h[MMU_CTL/4] |= MMU_CTL_TLB_CLEAR;
	for (spins = 1000000u; spins && (h[MMU_CTL/4] & MMU_CTL_TLB_CLEARING); spins--) {}
}

/* Re-apply the per-power-on core registers over the (surviving) MMU page table: MMU base +
 * enable, MMUC enable, and the general-L2 clear+enable. Used at init and after an in-job
 * reset. The PT, BO pool and GPU VAs are unchanged — only the V3D's own register state is
 * re-established. */
static void apply_core_regs(void)
{
	W.hub[MMU_PT_PA_BASE/4] = (uint32_t)(W.pt_pa>>PAGE_SHIFT);
	/* Full MMU fault config (mirror linux v3d_mmu_set_page_table): enable PT-invalid detection
	 * (not just abort), write-violation and cap-exceeded aborts+INTs. Our prior config set ABORT
	 * without the matching ENABLE, leaving detection effectively off so an illegal access could
	 * hang silently. */
	W.hub[MMU_CTL/4] = MMU_CTL_ENABLE | MMU_CTL_PTI_ENABLE | MMU_CTL_PTI_ABORT | MMU_CTL_PTI_INT |
		MMU_CTL_WRITEVIO_ABORT | MMU_CTL_WRITEVIO_INT |
		MMU_CTL_CAPEXC_ABORT | MMU_CTL_CAPEXC_INT;
	/* Arm the illegal-access scratch page: a faulting GPU access is redirected here (harmless
	 * mapped DRAM) instead of stalling the bus — and MMU_ILLEGAL_ADDR then reports the fault. */
	if (W.scratch_pa)
		W.hub[MMU_ILLEGAL_ADDR/4] = (uint32_t)(W.scratch_pa>>PAGE_SHIFT) | MMU_ILLEGAL_ENABLE;
	W.hub[MMUC_CONTROL/4] = MMUC_ENABLE;
	W.core0[CTL_L2CACTL/4] = L2CACTL_L2CCLR | L2CACTL_L2CENA;
	/* Define the L2T flush range as the WHOLE cache (mirrors linux v3d_init_core:
	 * L2TFLSTA=0, L2TFLEND=~0 — "whenever we flush L2T we want the whole thing"). Without
	 * this, every L2TCACTL flush we issue covers an indeterminate cold-boot range, so a
	 * bin->render flush can leave stale tile-list lines and the render fetches a stale
	 * next-block pointer -> CT1 wedges. These core regs aren't otherwise written by init or
	 * the reset path, so their cold-boot value persisted across software resets. */
	W.core0[CTL_L2TFLSTA/4] = 0u;
	W.core0[CTL_L2TFLEND/4] = ~0u;
	/* GFXH-1383: cap the V3D AXI master's max burst length. Our power-on path (mailbox +
	 * PM_V3DRSTN + rpivid_asb) never wrote HUB_AXICFG, so it kept whatever cold-boot/bridge
	 * value it had; an unbounded burst length lets the AXI deadlock under sustained load
	 * (GMP RD+WR stuck active, binner ct0ca frozen mid-CL = the workload-dependent wedge).
	 * Linux restores exactly this after every bridge reset. Written here so init AND the
	 * in-job reset path both establish it. */
	W.hub[HUB_AXICFG/4] = HUB_AXICFG_MAX_LEN;
#if (V3D_QRMAXCNT) >= 0
	/* MISCCFG = QRMAXCNT (QPU bin/render split — 2 zeroes both wedge classes here) | OVRTMUOUT.
	 * OVRTMUOUT (TMU uses the shader's texture-output-type field) IS required by our Mesa build —
	 * dropping it broke texture sampling (solid HUD/particles/fonts, wrong colours). Written once
	 * (init + reset path), so QRMAXCNT is NOT clobbered per submit (that was the binner-wedge bug). */
	W.core0[CTL_MISCCFG/4] = ((uint32_t)(V3D_QRMAXCNT) << MISCCFG_QRMAXCNT_SHIFT) | MISCCFG_OVRTMUOUT;
#endif
}

/* Render-stall MITIGATION + probe: how many times a submit recovered a wedged GPU by
 * resetting + retrying. "A reboot clears it" implies an in-process reset clears it too;
 * if so this turns the fatal first-frame render stall into a sub-second hitch. Exported
 * for visibility. */
volatile unsigned v3d_phoenix_render_recoveries = 0;

/* Reset the V3D and re-establish core register state, then return so the caller can
 * re-submit the same job. Re-runs the BCM2711 power-on (clock toggle + RSTN deassert +
 * ASB bridge re-enable) — the same sequence a reboot performs — then re-applies the core
 * regs over the surviving page table. */
/* Best-effort safe AXI drain: ask the GMP to quiesce any outstanding transaction before reset
 * so a stuck RD/WR (the wedge signature: GMP_STATUS RD+WR active) is drained rather than carried
 * across the reset. Mirrors linux v3d_idle_axi (GMP_CFG_STOP_REQ + wait for RD/WR counts +
 * CFG_BUSY to clear). Bounded spin — on a truly wedged AXI it may not drain, which is fine: the
 * subsequent hold-in-reset clears it. */
static void idle_axi(volatile uint32_t *c0)
{
	uint32_t spins;
	c0[GMP_CFG/4] = GMP_CFG_STOP_REQ;
	for (spins = 1000000u; spins; spins--) {
		if ((c0[GMP_STATUS/4] & (GMP_STATUS_RD_WR_CNT | GMP_STATUS_CFG_BUSY)) == 0u)
			break;
	}
}

static void reset_reinit_core(void)
{
	/* The weak powerOn (re-deassert RSTN only) was empirically unable to clear the binner
	 * hang: ct0ca stayed frozen at the same address across 3 resets, and the next frame's
	 * DIFFERENT CL still showed ct0ca stuck there — the AXI/GMP transaction survived the soft
	 * re-deassert. Do a TRUE reset instead: drain the GMP, hold the core in reset + power back
	 * on (v3d_phoenix_reset = asbStop + assert PM_V3DRSTN + powerOn), then re-establish core
	 * regs incl. the GFXH-1383 HUB_AXICFG burst cap. */
	if (W.core0)
		idle_axi(W.core0);
	(void)v3d_phoenix_reset();
	apply_core_regs();
}

/* Harness-only: force a TRUE V3D reset (hold-in-reset + power back on) then re-establish
 * core register state over the surviving page table. Lets rpi4-v3d-stalltest re-create the
 * cold first-frame-after-power-on condition per iteration, so each loop iteration is an
 * independent trial of the intermittent stall — converting one boot into hundreds of
 * samples. Requires winsys already inited (the harness renders one warm-up frame first). */
void v3d_phoenix_harness_reset(void);
void v3d_phoenix_harness_reset(void)
{
	(void)v3d_phoenix_reset();
	apply_core_regs();
}

/* The wedge is data-dependent (re-submitting the same frame re-hangs across true resets,
 * HW-confirmed), so the mitigation does NOT re-submit: on a wedge it does ONE true reset to
 * clean the core for the next (different) frame and drops the current frame. See the job_failed
 * handling in ioc_submit_cl. */

static int ioc_submit_cl(struct drm_v3d_submit_cl *s)
{
	volatile uint32_t *c0 = W.core0;
	volatile uint32_t *h = W.hub;
	uint32_t spins;
	int job_failed = 0;     /* set if bin or render wedged */
	int attempt = 0;        /* kept for the timeout-dump "attempt" field (always 0 now: no resubmit) */
	W.binovf_used = 0;      /* re-armable binner overflow: reset the per-job hand-out cursor */
	/* Flush the MMU PTE cache + TLB before the job. ioc_create_bo writes fresh PTEs but
	 * never invalidated the MMU's cached translations, so a job whose CL/RT BOs were just
	 * mapped at new GPU VAs (every Quake frame allocates fresh BOs) is fetched through a
	 * stale TLB -> the render thread reads an unmapped/wrong VA and hangs at RCL packet 0.
	 * (The cube reuses one persistent job at stable VAs, so it never hit this.) Mirrors the
	 * linux v3d_mmu_flush_all sequence: MMUC flush, then MMU_CTL TLB clear, each spin-waited. */
	mmu_flush_tlb(h);
	/* DO NOT write CTL_MISCCFG here. It is {QRMAXCNT[3:1], OVRTMUOUT[0]} — writing OVRTMUOUT
	 * (0x1) every submit CLOBBERED QRMAXCNT (the QPU-reserve-max-count that balances QPUs between
	 * the binner's coordinate shaders and the render's fragment shaders) to 0, which intermittently
	 * starved the coordinate shaders -> the binner wedged with "coordinate-shader QPUs pending"
	 * (the residual CT0 wedge). Linux v3d only writes MISCCFG for ver<41 (and only at init); on
	 * V3D 4.2 it never touches it, leaving the firmware's QRMAXCNT default. OVRTMUOUT is moot on
	 * 4.2 (Mesa sets the TMU output type in the shader/texture state). So leave MISCCFG alone. */
	/* Invalidate the V3D caches before the job: SLCACTL slices (TVCCS/TDCCS/UCC=uniform
	 * cache/ICC=instruction cache) + an L2T flush — matches the scout's v3d_invalidateCaches.
	 * The SLCACTL slices invalidation is essential for multi-frame rendering: without it the
	 * GPU serves stale uniforms from its uniform cache, so per-frame matrix/uniform changes
	 * never render (every frame looks like frame 0). */
	c0[CTL_SLCACTL/4] = SLCACTL_INVAL_ALL;
	l2t_flush_wait(c0);                       /* wait-old: prior L2T flush must be idle first */
	c0[CTL_L2TCACTL/4] = L2TCACTL_L2TFLS;
	l2t_flush_wait(c0);                       /* wait-new: flush must complete before the bin reads its CL/vertex data */
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
		uint32_t last_ca = c0[0x0110/4];   /* ct0ca — frozen = binner wedged (fast wedge detect) */
		unsigned frozen = 0;
		for (spins=8000000u; spins; spins--) {
			sts = c0[CTL_INT_STS/4];
			if (sts & INT_FLDONE) break;
			/* Re-armable overflow servicer (was single-shot !ovf_armed, which STARVED the binner
			 * on a 2nd OUTOMEM in one job — linux hands a fresh block per event). Hand successive
			 * chunks of the persistent pool on each OUTOMEM until it is exhausted; a frame needing
			 * more than the whole pool logs that it ran dry (a real wedge cause worth growing the
			 * pool for). OUTOMEM is edge-signalled, so clear it after each hand-out. */
			if ((sts & INT_OUTOMEM) && W.binovf_gpuva) {
				if (W.binovf_used < W.binovf_bytes) {
					uint32_t chunk = W.binovf_bytes - W.binovf_used;
					if (chunk > BINOVF_CHUNK_BYTES) chunk = BINOVF_CHUNK_BYTES;
					c0[PTB_BPOA/4] = W.binovf_gpuva + W.binovf_used;
					c0[PTB_BPOS/4] = chunk;
					c0[CTL_INT_CLR/4] = INT_OUTOMEM;
					W.binovf_used += chunk;
					ovf_armed = 1;
					frozen = 0; last_ca = c0[0x0110/4];   /* binner just re-armed; restart frozen window */
				}
				else if (ovf_armed != 2) {
					ovf_armed = 2;   /* pool exhausted — record once; binner will wedge */
					fprintf(stderr, "v3d-winsys: binner overflow pool EXHAUSTED (%u KiB) — grow BINOVF_PAGES\n",
						W.binovf_bytes / 1024u);
				}
			}
			if ((spins & 0xfffffu) == 0u) {            /* sample ct0ca ~every 1M spins (~160 ms) */
				uint32_t ca = c0[0x0110/4];
				if (ca == last_ca) {
					if (++frozen >= 5u) { spins = 0; break; }   /* frozen ~0.8 s -> wedged */
				}
				else { frozen = 0; last_ca = ca; }
			}
		}
		if (spins == 0) {
			job_failed = 1;
			fprintf(stderr, "v3d-winsys: BIN TIMEOUT int_sts=0x%08x ct0cs=0x%08x "
				"ct0ca=0x%08x[%x..%x] gmp=0x%08x gmpvio=0x%08x mmu_ill=0x%08x ovf_armed=%d (attempt %d)\n",
				c0[CTL_INT_STS/4], c0[0x0100/4], c0[0x0110/4], s->bcl_start, s->bcl_end,
				c0[GMP_STATUS/4], c0[0x0808/4], W.hub[MMU_ILLEGAL_ADDR/4], ovf_armed, attempt);
			/* PTB binner pointers + AXICFG localise the hang: BPCA advancing = binner alive but
			 * downstream stalled; BPCA frozen = binner itself wedged. gmp RD/WR-active with
			 * BPCA frozen = stuck AXI transaction (GFXH-1383). */
			fprintf(stderr, "v3d-winsys: BIN PTB bpca=0x%08x bpcs=0x%08x bpoa=0x%08x bpos=0x%08x "
				"hub_axicfg=0x%08x int_qpu=0x%03x\n",
				c0[PTB_BPCA/4], c0[PTB_BPCS/4], c0[PTB_BPOA/4], c0[PTB_BPOS/4],
				W.hub[HUB_AXICFG/4], (c0[CTL_INT_STS/4] >> 16) & 0xfffu);
			/* Instrument-validation probe (same as the render path): which BO backs bcl_start and
			 * ct0ca, at what offset / ambiguous? + full BCL head dump — head-zero vs wrong-BO. */
			{
				static int bdbg = 0;
				if (bdbg++ < 3) {
					uint32_t ca0 = c0[0x0110/4];
					gpuva_describe("BCLSTART", s->bcl_start);
					gpuva_describe("BINCA", ca0 & ~0xfu);
					uint32_t bw = (s->bcl_end - s->bcl_start + 3u) / 4u;
					if (bw > 40u) bw = 40u;
					uint32_t *bs = (uint32_t *)gpuva_to_cpu(s->bcl_start);
					fprintf(stderr, "v3d-winsys: BCLFULL gpuva=0x%08x (%u words):", s->bcl_start, bw);
					if (bs) { for (uint32_t i = 0; i < bw; i++) fprintf(stderr, " %08x", bs[i]); }
					else { fprintf(stderr, " (no BO)"); }
					fprintf(stderr, "\n");
				}
			}
		}
	}
	/* If the binner wedged, don't kick the render against a bad tile state — reset+retry. */
	if (job_failed)
		goto job_retry;
	c0[CTL_INT_CLR/4]=INT_FLDONE|INT_FRDONE;
	/* Bin->render coherency handoff. The binner (CT0, just done) writes the per-tile sub-lists
	 * into tile_alloc/overflow through the GPU's L2T; CT1's CL executor then FETCHES those
	 * tile-lists. Two things must hold before CT1 starts:
	 *   1. the binner's tile-list output must be flushed to RAM, and that flush must COMPLETE
	 *      (not merely be issued) — else CT1 reads an INCOMPLETE tile-list and parks near
	 *      rcl_end with FRDONE never firing (the data-dependent render wedge: worse on complex
	 *      frames whose larger tile-lists take longer to drain);
	 *   2. the render-side slice caches must be invalidated so a reused GPU VA doesn't serve a
	 *      prior BO's stale lines.
	 * The previous code only ISSUED the L2T flush then kicked CT1 immediately (no wait-new), so
	 * on a heavy frame the flush was still in flight when CT1 began fetching. Correct order:
	 * clean + WAIT, then invalidate, then kick. Linux v3d runs an invalidate before both bin
	 * and render for the same coherency reason. */
	l2t_flush_wait(c0);                       /* wait-old: any prior L2T flush must be idle */
	c0[CTL_L2TCACTL/4]=L2TCACTL_L2TFLS;       /* clean the binner's tile-list output to RAM */
	l2t_flush_wait(c0);                       /* wait-new: it must COMPLETE before CT1 fetches */
	c0[CTL_SLCACTL/4] = SLCACTL_INVAL_ALL;    /* then drop stale render-side slice-cache lines */
	/* --- render (CT1); wait FRDONE --- */
	c0[CLE_CT1QBA/4]=s->rcl_start; c0[CLE_CT1QEA/4]=s->rcl_end;
	/* Wait for FRDONE, detecting a wedge two ways: (a) FAST — ct1ca FROZEN (the confirmed wedge
	 * signature: CT1 stuck at one address) for ~0.8 s while not done; a legitimately slow frame
	 * keeps advancing ct1ca between samples, so this never false-trips and is safe for heavy
	 * frames; (b) BACKSTOP — the absolute spin cap. Frozen-detection shrinks the mitigation hitch
	 * from the full ~2.5 s cap to ~0.8 s. On done: spins != 0; on wedge: spins == 0 (unchanged
	 * downstream handling). */
	{
		uint32_t last_ca = c0[0x0114/4];
		unsigned frozen = 0;
		for (spins = 16000000u; spins; spins--) {
			if (c0[CTL_INT_STS/4] & INT_FRDONE)
				break;                                  /* render done */
			if ((spins & 0xfffffu) == 0u) {             /* sample ct1ca ~every 1M spins (~160 ms) */
				uint32_t ca = c0[0x0114/4];
				if (ca == last_ca) {
					if (++frozen >= 5u) { spins = 0; break; }   /* frozen ~0.8 s -> wedged */
				}
				else { frozen = 0; last_ca = ca; }
			}
		}
	}
	/* Clean-frame RCL structure probe (render-stall A/B, task #13): periodically dump the SAME
	 * locations the wedge dump reads (rcl head + tail) on a SUCCESSFUL render. If a clean RCL is
	 * structurally identical to a wedged one, the RCL BO is NOT corrupted and the wedge is a
	 * pipeline-drain stall (fdbgs=EZTEST), not CL/VA corruption. Read-only + throttled. */
	if (spins != 0) {
		static unsigned ok_n = 0;
		if ((ok_n++ % 256u) == 0u) {
			uint32_t *hp = (uint32_t *)gpuva_to_cpu(s->rcl_start & ~0xfu);
			uint32_t *tp = (uint32_t *)gpuva_to_cpu((s->rcl_end - 16u) & ~0xfu);
			fprintf(stderr, "v3d-winsys: CLEAN RCL head gpuva=0x%08x:", s->rcl_start & ~0xfu);
			if (hp) { for (int i=0;i<8;i++) fprintf(stderr, " %08x", hp[i]); } else fprintf(stderr, " (no BO)");
			fprintf(stderr, "  tail gpuva=0x%08x:", (s->rcl_end-16u)&~0xfu);
			if (tp) { for (int i=0;i<8;i++) fprintf(stderr, " %08x", tp[i]); } else fprintf(stderr, " (no BO)");
			fprintf(stderr, "\n");
		}
	}
	if (spins == 0) {
		uint32_t ca1 = c0[0x0114/4];
		v3d_phoenix_render_timeouts++;   /* stall counter for the repro harness */
		job_failed = 1;
		fprintf(stderr, "v3d-winsys: RENDER TIMEOUT int_sts=0x%08x ct1cs=0x%08x "
			"ct1ca=0x%08x[%x..%x] ct1ea=0x%08x gmp=0x%08x gmpvio=0x%08x mmu_ill=0x%08x\n",
			c0[CTL_INT_STS/4], c0[0x0104/4], ca1, s->rcl_start, s->rcl_end,
			c0[0x010c/4], c0[0x0800/4], c0[0x0808/4], W.hub[MMU_ILLEGAL_ADDR/4]);
		/* V3D error-debug registers (FDBGO/FDBGS/ERRSTAT) localize the wedged render
		 * pipeline stage; + decode the CL opcode byte CT1 is parked on. */
		{
			uint32_t *cp = (uint32_t *)gpuva_to_cpu(ca1 & ~0xfu);
			unsigned op = cp ? (cp[0] & 0xffu) : 0xffffffffu;
			const char *opn = (op==21)?"BRANCH_TO_IMPLICIT":(op==18)?"RETURN_FROM_SUBLIST":
				(op==124)?"TILE_COORDINATES":(op==23)?"SUPERTILE_COORDS":(op==0)?"HALT/zero":"?";
			fprintf(stderr, "v3d-winsys: RENDER DBG fdbgo=0x%08x fdbgs=0x%08x errstat=0x%08x "
				"wedge_op=0x%02x(%s)\n",
				c0[0x0f04/4], c0[0x0f10/4], c0[0x0f20/4], op, opn);
		}
		/* re-read CT1CA to see if it is advancing (slow) or wedged (stall) */
		(void)ca1;
		fprintf(stderr, "v3d-winsys: RENDER ct1ca recheck=0x%08x\n", c0[0x0114/4]);
	}
job_retry:
	/* MITIGATION. The wedge is a HW-marginal fragment/depth-pipeline drain stall (fdbgs shows
	 * DEPTHO_FIFO/INTERPZ stalled with valid work queued) triggered by specific complex
	 * geometry under the render-to-scanout (RASTER+uncached) store — part of the V3D RT
	 * coherency wall this port has hit several ways. It is NOT corruption/aliasing/cold-state
	 * (all ruled out via the instrument-validated wedge dump: valid RCL, single-match BOs).
	 *
	 * Recovery strategy: the wedge is DATA-dependent — re-submitting the SAME frame re-hangs
	 * at the same ct1ca/ct0ca across true resets (HW-confirmed). So do NOT burn another
	 * multi-second spin-timeout re-running known-bad work. Instead do ONE true reset (asbStop +
	 * assert RSTN + power-on + re-apply core regs), which clears the wedged core so the NEXT
	 * (different) frame renders, and DROP this frame. That converts a wedge from a multi-second
	 * re-submit freeze into a single dropped frame — the difference between unplayable and
	 * playable. (Earlier code re-submitted up to SUBMIT_MAX_RETRIES times, stacking timeouts.) */
	if (job_failed) {
		v3d_phoenix_render_recoveries++;
		fprintf(stderr, "v3d-winsys: GPU wedged — true reset + drop this frame "
			"(mitigation; drops=%u). Wedge is HW-marginal depth-pipeline drain stall.\n",
			v3d_phoenix_render_recoveries);
		reset_reinit_core();   /* clean the wedged core so the next (different) frame renders */
		(void)attempt;
	}
	/* L2T flush so RT stores reach RAM before CPU readback (scout finding). */
	l2t_flush_wait(c0);                       /* GFXH-1897: render flush must complete first */
	c0[CTL_L2TCACTL/4]=L2TCACTL_L2TFLS|(2u<<1); /* FLM_CLEAN */
	return 0;
}

/* DRM_V3D_SUBMIT_TFU: run a Texture Formatting Unit job — the hardware buffer->image (and
 * image->image) copy V3DV uses for every TILED/OPTIMAL texture upload, blit and mipmap
 * generation. Previously the winsys's ioctl dispatch handled only SUBMIT_CL and fell through
 * to `default: return 0`, so every TFU job was a silent no-op: the destination image stayed
 * at its alloc-time zero and every sampled texture rendered BLACK (#29). This programs the
 * TFU registers exactly as linux v3d_tfu_job_run does for V3D 4.2 (ver<71) and waits for the
 * unit to finish, bracketed by the same MMU-TLB + cache coherency operations ioc_submit_cl
 * uses (texture BOs were just created by ioc_create_bo, which writes PTEs but doesn't flush
 * the TLB; the source staging buffer must be coherent in RAM before the TFU reads it, and the
 * tiled destination must be flushed to RAM before a later sampling CL job reads it).
 *
 * The submit struct's iia/ica/iua/ioa are already full GPU virtual addresses — Mesa folds
 * each BO's GPU-VA base (the winsys's create_bo c->offset) into them in meta_emit_tfu_job /
 * copy_*_tfu — and the TFU is behind the V3D MMU, so we write them verbatim (no va2pa). */
static int ioc_submit_tfu(struct drm_v3d_submit_tfu *t)
{
	volatile uint32_t *c0 = W.core0;
	volatile uint32_t *h = W.hub;
	uint32_t spins;

	/* --- prologue: make the source coherent + translations fresh ---
	 * Flush the MMU TLB so the just-created source/dest BOs are translated by their new PTEs,
	 * then invalidate the slice caches + flush L2T so the TFU reads the source staging buffer
	 * from RAM rather than a stale cached view (mirror the ioc_submit_cl pre-bin sequence). */
	mmu_flush_tlb(h);
	c0[CTL_SLCACTL/4] = SLCACTL_INVAL_ALL;
	l2t_flush_wait(c0);                        /* prior L2T flush must be idle (GFXH-1897) */
	c0[CTL_L2TCACTL/4] = L2TCACTL_L2TFLS;
	l2t_flush_wait(c0);                        /* and complete before the TFU reads source */

	/* Clear any stale TFU done/fail latch so our post-kick poll sees only this job. */
	h[HUB_INT_CLR/4] = HUB_INT_TFUC | HUB_INT_TFUF;

	/* --- kick: program the TFU regs (ICFG last, with IOC). Verbatim from v3d_tfu_job_run. --- */
	h[TFU_IIA/4]  = t->iia;
	h[TFU_IIS/4]  = t->iis;
	h[TFU_ICA/4]  = t->ica;
	h[TFU_IUA/4]  = t->iua;
	h[TFU_IOA/4]  = t->ioa;
	h[TFU_IOS/4]  = t->ios;
	h[TFU_COEF0/4] = t->coef[0];
	if (t->coef[0] & TFU_COEF0_USECOEF) {      /* YUV: COEF1..3 valid only when USECOEF set */
		h[TFU_COEF1/4] = t->coef[1];
		h[TFU_COEF2/4] = t->coef[2];
		h[TFU_COEF3/4] = t->coef[3];
	}
	h[TFU_ICFG/4] = t->icfg | TFU_ICFG_IOC;    /* this write starts the job */

	/* --- wait for done. Primary signal: HUB_INT TFUC (set on completion) / TFUF (on failure) —
	 * sticky once set (cleared only by us, pre-kick) so it can't be missed, and HUB_INT_STS is the
	 * raw latch so this works without unmasking. Fallback signal: CS BUSY clearing AFTER we have
	 * observed it set at least once — mask-independent, so if STS ever turns out post-mask on this
	 * silicon the job still completes instead of false-timing-out. BUSY alone races the kick (it
	 * reads 0 in the window before the unit asserts busy), hence the "saw_busy" gate. Bounded spin
	 * like the CL paths — a wedged TFU must not hang the process. --- */
	{
		int saw_busy = 0;
		for (spins = 8000000u; spins; spins--) {
			if (h[HUB_INT_STS/4] & (HUB_INT_TFUC | HUB_INT_TFUF))
				break;
			uint32_t cs = h[TFU_CS/4];
			if (cs & TFU_CS_BUSY) saw_busy = 1;
			else if (saw_busy) break;   /* completed (mask-independent fallback) */
		}
	}
	{
		uint32_t isr = h[HUB_INT_STS/4];
		int failed = (spins == 0) || (isr & HUB_INT_TFUF);
		h[HUB_INT_CLR/4] = HUB_INT_TFUC | HUB_INT_TFUF;
		if (failed) {
			fprintf(stderr, "v3d-winsys: TFU TIMEOUT/FAIL hub_int=0x%08x mskts=0x%08x cs=0x%08x "
				"iia=0x%08x ioa=0x%08x ios=0x%08x icfg=0x%08x\n",
				isr, h[HUB_INT_MSK_STS/4], h[TFU_CS/4], t->iia, t->ioa, t->ios, t->icfg);
			/* Don't abort the client — return success so rendering proceeds (a failed
			 * upload leaves the image zero, same as before, just visible in the log). */
		}
		else {
			static unsigned tfu_n = 0;
			tfu_n++;
			/* Discriminator probe (gated; the dest BO is uncached + just L2T-clean-flushed, so a
			 * CPU read sees RAM). On the instrumented boot this separates the three outcomes:
			 *   src=0            -> the upload-to-staging is the bug, not the TFU;
			 *   src!=0, dst=0    -> TFU register programming is wrong (tune on HW);
			 *   src!=0, dst!=0, still black -> the copy works; the bug is the sampler/descriptor
			 *                       path (resolves the "CL fallback also black" mystery — the TFU
			 *                       was necessary but not sufficient).
			 * The marker proves the TFU KICKED; this readback proves whether pixels LANDED. */
			if (tfu_n <= 12u || (tfu_n & 0x3ffu) == 0u) {
				/* Coherency discriminator (striping triage): the dest BO is uncached, and we have
				 * L2T-clean-flushed the TFU output to RAM above (the flush is BEFORE this read).
				 * Read the SOURCE (raster staging) first words, and the DEST (tiled image) at the
				 * START of the buffer and a MIDDLE row, after re-deriving stride from ios width.
				 * - src all-stale/0 -> CPU-staging->TFU input coherency gap (staging cached, not
				 *   cleaned before the TFU read);
				 * - dst start nonzero but mid-row 0/garbage -> TFU only partially wrote the tiled
				 *   image (write/flush gap) -> the BO itself is striped;
				 * - dst BO looks fully populated here but the SAMPLE is striped -> the gap is
				 *   TMU-side (L2T not invalidated before the sampling CL reads it). */
				const uint32_t *src = gpuva_to_cpu(t->iia);
				const uint32_t *dst = gpuva_to_cpu(t->ioa);
				uint32_t w = t->ios & 0xffffu;
				uint32_t hgt = t->ios >> 16;
				int src_nz = src ? (src[0] | src[1] | src[2] | src[3]) != 0u : -1;
				int dst_nz = dst ? (dst[0] | dst[1] | dst[2] | dst[3]) != 0u : -1;
				/* TILED-vs-LINEAR discriminator (the decisive striping test). The source staging
				 * buffer is RASTER: src word k = pixel (x=k%w, y=k/w). For a UIF_NO_XOR dest the
				 * V3D byte layout (verified against mesa v3d_tiling.c) puts dest byte 64 (word 16)
				 * = source pixel (4,0) = src[4]; a LINEAR/raster dest would instead have dest
				 * word 16 = pixel (16,0) = src[16]. So for w>=17:
				 *   dst[16] == src[4]  -> TFU produced correct UIF tiling (gap is elsewhere/read-side)
				 *   dst[16] == src[16] -> TFU wrote LINEAR (a winsys/icfg tiling-config bug)
				 * Bytes 0..63 are the top-left 4x4 microtile in raster order (identical to linear),
				 * so only word>=16 discriminates. Print dst[4],dst[16] alongside src[4],src[16]. */
				uint32_t s4  = (src && w >= 5u)  ? src[4]  : 0u;   /* src pixel (4,0)  */
				uint32_t s16 = (src && w >= 17u) ? src[16] : 0u;   /* src pixel (16,0) */
				uint32_t d4  = dst ? dst[4]  : 0u;                 /* dst word 4  (byte 16) */
				uint32_t d16 = dst ? dst[16] : 0u;                 /* dst word 16 (byte 64) — the test */
				const char *verdict = "n/a";
				if (dst && src && w >= 17u) {
					if (d16 == s4 && d16 != s16)      verdict = "UIF-OK";
					else if (d16 == s16 && d16 != s4) verdict = "LINEAR!";
					else                              verdict = "??";
				}
				/* Also print what Mesa REQUESTED (ioa low bits carry the dest tiling-format field,
				 * icfg the input format/ttype/opad). Combined with the produced-tiling verdict this
				 * is 3-way: Mesa-asked-UIF + produced-UIF + striped -> pure read-side (descriptor/
				 * L2T); Mesa-asked-UIF + produced-LINEAR -> TFU ignored IOA (winsys/HW); Mesa-asked-
				 * LINEAR (ioa format field == LINEARTILE/RASTER) -> Mesa dst_tiling bug upstream.
				 * NOTE: the decisive verdict comes from a texture with distinct (4,0)/(16,0) source
				 * pixels — read the BLUENOISE 64x64 line (random data), NOT conchars (blank top row
				 * -> src[4]==src[16] -> verdict ??). */
				fprintf(stderr, "v3d-winsys: TFU copy iia=0x%08x->ioa=0x%08x icfg=0x%08x %ux%u done (n=%u) "
					"src_nz=%d dst_nz=%d | TILING=%s dst[16]=%08x src(4,0)=%08x src(16,0)=%08x "
					"dst[4]=%08x\n",
					t->iia, t->ioa, t->icfg, w, hgt, tfu_n, src_nz, dst_nz, verdict,
					d16, s4, s16, d4);
			}
		}
	}

	/* --- epilogue: make the TFU-written tiled image visible to the TMU (sampler). The TFU writes
	 * the destination through the TMU's WRITE COMBINER + L2T; a plain L2T clean is NOT enough — the
	 * write combiner holds partial cache-line writes that never reach RAM, so the sampler reads a
	 * partially-stale image (the classic horizontal STRIPING on a freshly-TFU'd texture). Mirror
	 * linux v3d_clean_caches EXACTLY (the CACHE_CLEAN job the kernel runs after a write-producing
	 * job): wait for any in-flight L2T flush (GFXH-1897), then TMU write-combiner flush + WAIT, then
	 * L2T clean + WAIT. The waits are essential — the prior code issued FLM_CLEAN without waiting,
	 * so the sampling CL could start before the clean drained. */
	l2t_flush_wait(c0);                                  /* GFXH-1897: any prior L2T flush idle */
	c0[CTL_L2TCACTL/4] = L2TCACTL_TMUWCF;                /* drain the TMU write combiner... */
	for (spins = 1000000u; spins && (c0[CTL_L2TCACTL/4] & L2TCACTL_TMUWCF); spins--) {}  /* ...and wait */
	c0[CTL_L2TCACTL/4] = L2TCACTL_L2TFLS | L2TCACTL_FLM_CLEAN;   /* write back dirty L2T lines... */
	l2t_flush_wait(c0);                                  /* ...and wait for the clean to complete */
	c0[CTL_SLCACTL/4] = SLCACTL_INVAL_ALL;              /* drop stale read-only slice/TMU cache view */
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
int v3d_phoenix_scanout_active(void)
{
	/* "active" = a scanout buffer is backing an RT: single-buffer sets scanout_claimed; double-
	 * buffer tracks scanout_claim_idx (>0 once a buffer is claimed). Without the double case this
	 * read 0 in double mode and the present path wrongly fell back to CPU readback. */
	return W.scanout_claimed || (W.scanout_double && W.scanout_claim_idx > 0);
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
	case DRM_V3D_SUBMIT_TFU:
		return ioc_submit_tfu(arg);
	default:
		return 0;   /* perfmon/csd: no-op for now */
	}
}
