/*
 * csd_matmul — V3D compute matmul microbench (ML phase-2).
 *
 * o[i] = sum_j w[i*N+j]*x[j] on the GPU (CSD) vs CPU, SWEPT over the inner
 * (reduction) dimension N with the output dimension D fixed at 256 (so the
 * proven CSD dispatch grid is unchanged and only N + the buffers vary). PRIMARY
 * gate = numeric diff (GPU vs CPU, tight rel-tol). Also reports GPU-incl-dispatch
 * vs CPU time per N + the fixed dispatch floor (empty CSNOP).
 *
 * N is driven at runtime via uniform[2] (the compiler materialised the former
 * compile-time MMN=256 as a constant uniform): if the GPU result stays bit-exact
 * as N changes, uniform[2] feeds BOTH the loop bound and the row stride and this
 * one kernel serves any N. The 256 row is the control (must match the prior run).
 * Kernel from v3d-shader-tool CSMATMUL.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "v3d_drm.h"

extern int phoenix_v3d_ioctl(int fd, unsigned long request, void *arg);

#define D 256           /* output dimension = dispatch grid (FIXED) */
#define N_MAX 2048      /* largest inner dimension in the sweep */
#define ITERS 100
#define WG_COUNT_SHIFT       16
#define WGS_PER_SG_SHIFT     8
#define BATCHES_M1_SHIFT     12
#define WG_SIZE_SHIFT        0
#define CFG5_PROPAGATE_NANS  (1u << 2)
#define CFG5_THREADING       (1u << 0)
#define CFG5_SINGLE_SEG      (1u << 1)

/* CSMATMUL QPU (31 words), local_size=64, N RUNTIME from ssbo3[0]. v3d-shader-tool.
 * Uniform layout (contents=53 => supply that SSBO binding's GPU-VA; contents=0 =>
 * supply the literal): [0]=0xffff [1]=0x1a (gid consts) [2]=binding3(params) VA
 * [3]=0xc [4]=binding0(W) VA [5]=binding1(x) VA [6]=0xfffffff0 [7]=binding2(o) VA. */
static const uint64_t MATMUL[] = {
	0x3c403186bb800000ull, 0x3c403180b582e000ull, 0x3c6031817d82e080ull, 0x3de033027cf78006ull,
	0x3de000c538fca000ull, 0x3fe19106bbfc0000ull, 0x28003186bbf80146ull, 0x0c0011c6bbf80146ull,
	0x3c00f1863c83e0c6ull, 0x0200005f0020d000ull, 0x3c003186bb800000ull, 0x3c003186bb800000ull,
	0x3c003186bb800000ull, 0x3c4031833883e1c3ull, 0x3de031847c83b002ull, 0x05e033007c97e0c2ull,
	0x3c603186bb800000ull, 0x05e010cc38f850c1ull, 0x3c003186bb800000ull, 0x3c907186bb800000ull,
	0x02ffff80ff00d000ull, 0x3c90b186bb800000ull, 0x540030c6bb440000ull, 0x3c00218405833100ull,
	0x3de031847c83e142ull, 0x3c6032c6bbf80100ull, 0x3c20318c38825000ull, 0x3c003186bb800000ull,
	0x3c203180bb815000ull, 0x3c003186bb800000ull, 0x3c003186bb800000ull,
};

/* CSNOP: empty compute kernel (thread-end only), local_size=16, single_seg=1,
 * uniforms=0. Used to measure the FIXED per-dispatch floor (submit ioctl + GPU
 * launch + INT_CSDDONE wait, no memory/compute). From v3d-shader-tool. */
static const uint64_t CSNOP[] = {
	0x3c203186bb800000ull, /* nop ; nop ; thrsw */
	0x3c003186bb800000ull, /* nop ; nop */
	0x3c003186bb800000ull, /* nop ; nop */
};

static uint32_t make_bo(int fd, uint32_t size, uint32_t *gpuva, void **cpu)
{
	struct drm_v3d_create_bo c;
	struct drm_v3d_mmap_bo m;
	memset(&c, 0, sizeof(c));
	c.size = size;
	if (phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_CREATE_BO, &c) != 0) return 0;
	memset(&m, 0, sizeof(m));
	m.handle = c.handle;
	if (phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_MMAP_BO, &m) != 0) return 0;
	*gpuva = c.offset;
	*cpu = (void *)(uintptr_t)m.offset;
	return c.handle;
}

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

int main(void)
{
	int fd = 0;
	struct drm_v3d_get_param gp;
	struct drm_v3d_submit_csd s;
	uint32_t shva, wva, xva, ova, unva, pva;
	void *shcpu, *wcpu, *xcpu, *ocpu, *uncpu, *pcpu;
	uint32_t shbo, wbo, xbo, obo, unbo, pbo, h[6];
	float *w, *x, *og;
	static float ref[D];
	uint32_t *u;
	int i, j, k, si;
	const int sweep[] = { 256, 512, 1024, N_MAX };
	const int nsweep = (int)(sizeof(sweep) / sizeof(sweep[0]));

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("csd-matmul: sweep D=%d N={256,512,1024,%d} iters=%d\n", D, N_MAX, ITERS);

	memset(&gp, 0, sizeof(gp));
	gp.param = DRM_V3D_PARAM_SUPPORTS_CSD;
	phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_GET_PARAM, &gp);

	/* Allocate once at the max size; each sweep step uses a W[D*N] / x[N] prefix. */
	shbo = make_bo(fd, (uint32_t)sizeof(MATMUL), &shva, &shcpu);
	wbo = make_bo(fd, (uint32_t)D * N_MAX * 4u, &wva, &wcpu);
	xbo = make_bo(fd, (uint32_t)N_MAX * 4u, &xva, &xcpu);
	obo = make_bo(fd, (uint32_t)D * 4u, &ova, &ocpu);
	unbo = make_bo(fd, 8u * 4u, &unva, &uncpu);
	pbo = make_bo(fd, 16u, &pva, &pcpu);   /* params: params[0] = N (runtime) */
	if (!shbo || !wbo || !xbo || !obo || !unbo || !pbo) {
		printf("csd-matmul: BO alloc FAILED\n");
		return 1;
	}
	memcpy(shcpu, MATMUL, sizeof(MATMUL));
	w = (float *)wcpu;
	x = (float *)xcpu;
	og = (float *)ocpu;
	u = (uint32_t *)uncpu;

	/* Fixed CSD dispatch: D=256 outputs = 4 workgroups * local_size 64. Unchanged
	 * across the sweep (only N + buffers vary), so the grid stays the proven one. */
	memset(&s, 0, sizeof(s));
	s.cfg[0] = 4u << WG_COUNT_SHIFT;
	s.cfg[1] = 1u << WG_COUNT_SHIFT;
	s.cfg[2] = 1u << WG_COUNT_SHIFT;
	s.cfg[3] = (1u << WGS_PER_SG_SHIFT) | ((4u - 1u) << BATCHES_M1_SHIFT) | (64u << WG_SIZE_SHIFT);
	s.cfg[4] = 15u;
	s.cfg[5] = shva | CFG5_PROPAGATE_NANS | CFG5_THREADING;
	s.cfg[6] = unva;
	h[0] = shbo; h[1] = wbo; h[2] = xbo; h[3] = obo; h[4] = unbo; h[5] = pbo;
	s.bo_handles = (uint64_t)(uintptr_t)h;
	s.bo_handle_count = 6;

	for (si = 0; si < nsweep; si++) {
		int N = sweep[si];
		uint32_t seed = 0x1234567u;
		double tg0, tg1, tc0, tc1;
		float maxrel = 0.0f;
		int badi = 0, it, r = 0;

		for (k = 0; k < D * N; k++) {
			seed = seed * 1103515245u + 12345u;
			w[k] = (float)((int32_t)(seed >> 9) & 0xffff) / 32768.0f - 1.0f;
		}
		for (k = 0; k < N; k++) {
			seed = seed * 1103515245u + 12345u;
			x[k] = (float)((int32_t)(seed >> 9) & 0xffff) / 32768.0f - 1.0f;
		}

		/* New layout: [2]=params(binding3) VA, [4]=W, [5]=x, [7]=o; consts at
		 * [0]=0xffff [1]=0x1a [3]=0xc [6]=0xfffffff0. N lives in params[0]. */
		*(uint32_t *)pcpu = (uint32_t)N;
		u[0] = 0x0000ffffu; u[1] = 0x0000001au; u[2] = pva;         u[3] = 0x0000000cu;
		u[4] = wva;         u[5] = xva;         u[6] = 0xfffffff0u;  u[7] = ova;

		memset(ocpu, 0, D * 4u);
		tg0 = now_ms();
		for (it = 0; it < ITERS; it++)
			r = phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_SUBMIT_CSD, &s);
		tg1 = now_ms();

		tc0 = now_ms();
		for (it = 0; it < ITERS; it++) {
			for (i = 0; i < D; i++) {
				float sum = 0.0f;
				for (j = 0; j < N; j++)
					sum += w[i * N + j] * x[j];
				ref[i] = sum;
			}
		}
		tc1 = now_ms();

		for (i = 0; i < D; i++) {
			float dd = fabsf(og[i] - ref[i]);
			float rel = dd / (fabsf(ref[i]) + 1.0e-6f);
			if (rel > maxrel) { maxrel = rel; badi = i; }
		}
		printf("csd-matmul: N=%4d  GPU=%.4f  CPU=%.4f ms/matmul  ratio=%.2fx  max_rel_err=%.3e  %s (gpu=%.4f cpu=%.4f @i=%d)\n",
			N, (tg1 - tg0) / ITERS, (tc1 - tc0) / ITERS,
			(tc1 - tc0) / (tg1 - tg0),
			maxrel, (r == 0 && maxrel < 1.0e-3f) ? "PASS" : "FAIL",
			og[badi], ref[badi], badi);
	}

	/* Fixed dispatch floor: ITERS empty CSNOP dispatches (1 wg, local_size 16,
	 * single_seg — the known-good csd_probe STEP1 config). ~zero compute/memory. */
	{
		uint32_t nshva;
		void *nshcpu;
		uint32_t nshbo = make_bo(fd, (uint32_t)sizeof(CSNOP), &nshva, &nshcpu);
		if (nshbo) {
			struct drm_v3d_submit_csd ns;
			uint32_t nh[2];
			double tf0, tf1;
			int it, nr = 0;
			memcpy(nshcpu, CSNOP, sizeof(CSNOP));
			memset(&ns, 0, sizeof(ns));
			ns.cfg[0] = 1u << WG_COUNT_SHIFT;
			ns.cfg[1] = 1u << WG_COUNT_SHIFT;
			ns.cfg[2] = 1u << WG_COUNT_SHIFT;
			ns.cfg[3] = (1u << WGS_PER_SG_SHIFT) | (0u << BATCHES_M1_SHIFT) | (16u << WG_SIZE_SHIFT);
			ns.cfg[4] = 0u;
			ns.cfg[5] = nshva | CFG5_PROPAGATE_NANS | CFG5_SINGLE_SEG | CFG5_THREADING;
			ns.cfg[6] = unva;
			nh[0] = nshbo; nh[1] = unbo;
			ns.bo_handles = (uint64_t)(uintptr_t)nh;
			ns.bo_handle_count = 2;
			tf0 = now_ms();
			for (it = 0; it < ITERS; it++)
				nr = phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_SUBMIT_CSD, &ns);
			tf1 = now_ms();
			printf("csd-matmul: FLOOR (empty CSNOP) rc=%d %.4f ms/dispatch\n",
				nr, (tf1 - tf0) / ITERS);
		}
	}
	return 0;
}
