/*
 * csd_matmul — V3D compute matmul microbench (ML phase-2).
 *
 * o[i] = sum_j w[i*N+j]*x[j] on the GPU (CSD) vs CPU. PRIMARY gate = numeric
 * diff (GPU vs CPU, tight rel-tol). Also reports GPU-incl-dispatch vs CPU time
 * (honest perf number; matmul-vector is bandwidth-bound + dispatch is
 * synchronous, so GPU may be slower — that's a legitimate finding).
 * Persistent BOs allocated once. Kernel from v3d-shader-tool CSMATMUL.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "v3d_drm.h"

extern int phoenix_v3d_ioctl(int fd, unsigned long request, void *arg);

#define N 256
#define D 256
#define ITERS 100
#define WG_COUNT_SHIFT       16
#define WGS_PER_SG_SHIFT     8
#define BATCHES_M1_SHIFT     12
#define WG_SIZE_SHIFT        0
#define CFG5_PROPAGATE_NANS  (1u << 2)
#define CFG5_THREADING       (1u << 0)
#define CFG5_SINGLE_SEG      (1u << 1)

/* CSNOP: empty compute kernel (thread-end only), local_size=16, single_seg=1,
 * uniforms=0. From v3d-shader-tool (shaders-dump.txt / csd_probe.c). Used here to
 * measure the FIXED per-dispatch floor (submit ioctl + GPU launch + INT_CSDDONE
 * wait, no memory/compute) — so the GEMM-crossover question (does batching Nb
 * matrix-vector products into one dispatch amortize this floor?) can be answered
 * analytically from floor vs the matmul time. */
static const uint64_t CSNOP[] = {
	0x3c203186bb800000ull, /* nop ; nop ; thrsw */
	0x3c003186bb800000ull, /* nop ; nop */
	0x3c003186bb800000ull, /* nop ; nop */
};

/* CSMATMUL QPU (30 words), N=D=256, local_size=64. From shaders-dump.txt. */
static const uint64_t MATMUL[] = {
	0x3c403186bb800000ull, 0x3c403180b582e000ull, 0x3de031827c838006ull, 0x3de010c17dfee080ull,
	0x3de0010538fca000ull, 0x3d81f186bb800000ull, 0x3de021867c83e148ull, 0x3c4031833883e183ull,
	0x3de031847c83b002ull, 0x05e033007c97e0c2ull, 0x3de021833883e0c1ull, 0x3c60f1863c83e0c7ull,
	0x3c00318c38805000ull, 0x3c003186bb800000ull, 0x3c907186bb800000ull, 0x020000270020d000ull,
	0x3c90b186bb800000ull, 0x540030c6bb440000ull, 0x3c00218405833100ull, 0x02ffff80ff00d000ull,
	0x3c003186bb800000ull, 0x3c003186bb800000ull, 0x3c003186bb800000ull, 0x3de031847c83e142ull,
	0x3c6032c6bbf80100ull, 0x3c20318c38825000ull, 0x3c003186bb800000ull, 0x3c203180bb815000ull,
	0x3c003186bb800000ull, 0x3c003186bb800000ull,
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
	uint32_t shva, wva, xva, ova, unva;
	void *shcpu, *wcpu, *xcpu, *ocpu, *uncpu;
	uint32_t shbo, wbo, xbo, obo, unbo, h[5];
	float *w, *x, *og;
	static float ref[D];
	uint32_t seed = 0x1234567u, *u;
	int i, j, k, r = 0, it, badi = 0;
	float maxrel = 0.0f;
	double tg0, tg1, tc0, tc1;

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("csd-matmul: microbench N=%d D=%d iters=%d\n", N, D, ITERS);

	memset(&gp, 0, sizeof(gp));
	gp.param = DRM_V3D_PARAM_SUPPORTS_CSD;
	phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_GET_PARAM, &gp);

	shbo = make_bo(fd, (uint32_t)sizeof(MATMUL), &shva, &shcpu);
	wbo = make_bo(fd, N * D * 4u, &wva, &wcpu);
	xbo = make_bo(fd, N * 4u, &xva, &xcpu);
	obo = make_bo(fd, D * 4u, &ova, &ocpu);
	unbo = make_bo(fd, 8u * 4u, &unva, &uncpu);
	if (!shbo || !wbo || !xbo || !obo || !unbo) {
		printf("csd-matmul: BO alloc FAILED\n");
		return 1;
	}
	memcpy(shcpu, MATMUL, sizeof(MATMUL));

	w = (float *)wcpu;
	x = (float *)xcpu;
	og = (float *)ocpu;
	for (k = 0; k < N * D; k++) {
		seed = seed * 1103515245u + 12345u;
		w[k] = (float)((int32_t)(seed >> 9) & 0xffff) / 32768.0f - 1.0f; /* [-1,1) */
	}
	for (k = 0; k < N; k++) {
		seed = seed * 1103515245u + 12345u;
		x[k] = (float)((int32_t)(seed >> 9) & 0xffff) / 32768.0f - 1.0f;
	}

	u = (uint32_t *)uncpu;
	u[0] = 0x0000ffffu; u[1] = 0x0000001au; u[2] = 0x00000100u;
	u[3] = wva; u[4] = xva; u[5] = 0x00000004u; u[6] = 0xfffffff0u; u[7] = ova;

	memset(&s, 0, sizeof(s));
	s.cfg[0] = 4u << WG_COUNT_SHIFT;   /* D/local_size = 256/64 = 4 workgroups (x) */
	s.cfg[1] = 1u << WG_COUNT_SHIFT;
	s.cfg[2] = 1u << WG_COUNT_SHIFT;
	s.cfg[3] = (1u << WGS_PER_SG_SHIFT) | ((4u - 1u) << BATCHES_M1_SHIFT) | (64u << WG_SIZE_SHIFT);
	s.cfg[4] = 15u;                    /* num_batches-1 = 16-1 (4 wg * 64/16 batches) */
	s.cfg[5] = shva | CFG5_PROPAGATE_NANS | CFG5_THREADING; /* single_seg=0 */
	s.cfg[6] = unva;
	h[0] = shbo; h[1] = wbo; h[2] = xbo; h[3] = obo; h[4] = unbo;
	s.bo_handles = (uint64_t)(uintptr_t)h;
	s.bo_handle_count = 5;

	memset(ocpu, 0, D * 4u);
	tg0 = now_ms();
	for (it = 0; it < ITERS; it++)
		r = phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_SUBMIT_CSD, &s);
	tg1 = now_ms();
	printf("csd-matmul: GPU %d dispatches rc=%d total=%.3f ms (%.4f ms/matmul)\n",
		ITERS, r, tg1 - tg0, (tg1 - tg0) / ITERS);

	/* Fixed-floor: time ITERS empty CSNOP dispatches (1 wg, local_size 16,
	 * single_seg — the known-good csd_probe STEP1 config). This is the per-submit
	 * overhead with ~zero compute/memory, so (matmul_ms - floor_ms) is the actual
	 * per-matmul GPU work. GEMM crossover prediction: batching Nb matrix-vectors
	 * into ONE dispatch costs ~ floor + Nb*(matmul_ms - floor); it beats the CPU
	 * (Nb * cpu_ms/matmul) once Nb is large enough that the amortized floor no
	 * longer dominates. If floor ~= matmul_ms, the crossover is real and near Nb ~
	 * floor/cpu_ms; if floor << matmul_ms, GPU is compute/bandwidth-bound and no
	 * batching helps -> RULED OUT with data. */
	{
		uint32_t nshva, nfloor_r = 0;
		void *nshcpu;
		uint32_t nshbo = make_bo(fd, (uint32_t)sizeof(CSNOP), &nshva, &nshcpu);
		if (nshbo) {
			struct drm_v3d_submit_csd ns;
			uint32_t nh[2];
			double tf0, tf1;
			memcpy(nshcpu, CSNOP, sizeof(CSNOP));
			memset(&ns, 0, sizeof(ns));
			ns.cfg[0] = 1u << WG_COUNT_SHIFT;
			ns.cfg[1] = 1u << WG_COUNT_SHIFT;
			ns.cfg[2] = 1u << WG_COUNT_SHIFT;
			ns.cfg[3] = (1u << WGS_PER_SG_SHIFT) | (0u << BATCHES_M1_SHIFT) | (16u << WG_SIZE_SHIFT);
			ns.cfg[4] = 0u;
			ns.cfg[5] = nshva | CFG5_PROPAGATE_NANS | CFG5_SINGLE_SEG | CFG5_THREADING;
			ns.cfg[6] = unva;   /* unused by CSNOP but must be a valid BO VA */
			nh[0] = nshbo; nh[1] = unbo;
			ns.bo_handles = (uint64_t)(uintptr_t)nh;
			ns.bo_handle_count = 2;
			tf0 = now_ms();
			for (it = 0; it < ITERS; it++)
				nfloor_r = phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_SUBMIT_CSD, &ns);
			tf1 = now_ms();
			printf("csd-matmul: FLOOR (empty CSNOP) %d dispatches rc=%d total=%.3f ms (%.4f ms/dispatch)\n",
				ITERS, nfloor_r, tf1 - tf0, (tf1 - tf0) / ITERS);
		}
		else {
			printf("csd-matmul: FLOOR CSNOP BO alloc FAILED (skipping floor)\n");
		}
	}

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
	printf("csd-matmul: CPU %d matmuls total=%.3f ms (%.4f ms/matmul)\n",
		ITERS, tc1 - tc0, (tc1 - tc0) / ITERS);

	for (i = 0; i < D; i++) {
		float dd = fabsf(og[i] - ref[i]);
		float rel = dd / (fabsf(ref[i]) + 1.0e-6f);
		if (rel > maxrel) { maxrel = rel; badi = i; }
	}
	printf("csd-matmul: numeric max_rel_err=%.3e at i=%d (gpu=%.5f cpu=%.5f) | o[0]=%.5f/%.5f o[255]=%.5f/%.5f\n",
		maxrel, badi, og[badi], ref[badi], og[0], ref[0], og[255], ref[255]);
	if (r == 0 && maxrel < 1.0e-3f)
		printf("csd-matmul: PASS (GPU matmul numerically correct) GPU=%.4f CPU=%.4f ms/matmul ratio=%.2fx\n",
			(tg1 - tg0) / ITERS, (tc1 - tc0) / ITERS, (tg1 - tg0) / (tc1 - tc0));
	else
		printf("csd-matmul: FAIL rc=%d maxrel=%.3e\n", r, maxrel);
	return 0;
}
