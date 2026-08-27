/*
 * csd_gemm8 — batched V3D compute matmul microbench (ML phase-2, decisive test).
 *
 * C[i][k] = sum_j W[i*N+j] * X[j*8+k], M=256 rows, K=8 batch columns, N runtime.
 * Each GPU invocation computes 8 batch outputs holding 8 accumulators and loading
 * each weight-row element ONCE (reused across the batch) — the weight-reuse a plain
 * per-vector GEMV lacks. Same D=256 grid as CSMATMUL (kernel-only change). Answers:
 * does batching amortise the per-vector cost enough for the GPU to BEAT the CPU?
 * (single-vector was measured at parity). Kernel = v3d-shader-tool CSGEMM8.
 *
 * RESULT 2026-08-27 — DEFERRED (HW-HANGS): the CSGEMM8 kernel compiles clean
 * host-side but on real V3D the dispatch TIMES OUT (CSD status=0x7,
 * num_completed=0, GPU "time" = the server's timeout spin) with garbage output.
 * The 8-accumulator kernel drops to threads=2 (register pressure) and never
 * signals completion — likely a spill/loop/uniform-ABI issue in the hand-built
 * NIR. Root-causing a hand-authored compute-shader HW hang is deep multi-cycle
 * GPU-compute debugging (and borders the owner-gated tiled-GEMM grind), so it is
 * DEFERRED. Kept as reusable substrate + an honest record of the attempt. The
 * working, validated GPU matmul is the single-vector csd_matmul.c (bit-exact,
 * ~CPU parity). A GPU-ML *speedup* remains unproven pending this batched path.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "v3d_drm.h"

extern int phoenix_v3d_ioctl(int fd, unsigned long request, void *arg);

#define D 256           /* rows / dispatch grid */
#define GK 8            /* batch columns */
#define N_MAX 1024
#define ITERS 100
#define WG_COUNT_SHIFT   16
#define WGS_PER_SG_SHIFT 8
#define BATCHES_M1_SHIFT 12
#define WG_SIZE_SHIFT    0
#define CFG5_PROPAGATE_NANS (1u << 2)
#define CFG5_THREADING      (1u << 0)

/* CSGEMM8 QPU (55 words), local_size=64, K=8, N runtime from ssbo3[0]. v3d-shader-tool.
 * Uniforms (13): [2]=binding3(params) VA, [5]=binding0(W) VA, [6]=binding1(X) VA,
 * [10]=binding2(C) VA (contents=53); the rest are literal consts supplied verbatim. */
static const uint64_t GEMM8[] = {
	0x3c403186bb800000ull, 0x3c403180b582e000ull, 0x3c6031817d82e080ull, 0x3de033027cf78006ull,
	0x3de000cc38fca000ull, 0x3fe35106bbfc0000ull, 0x2983f186bbf8030dull, 0x3de01146bbfc0000ull,
	0x3de01186bbfc0000ull, 0x3de011c6bbfc0000ull, 0x3de01206bbfc0000ull, 0x3de01246bbfc0000ull,
	0x3de01286bbfc0000ull, 0x3de012c6bbfc0000ull, 0x0c001386bbf8030dull, 0x3c00f1863c83e0cdull,
	0x020000af0020d000ull, 0x3c003186bb800000ull, 0x3c003186bb800000ull, 0x3c003186bb800000ull,
	0x3c6031833883e383ull, 0x3de031847c83b002ull, 0x05e023107c97e0c5ull, 0x3da46193388373d0ull,
	0x3fe4a1833883e0c1ull, 0x3c00318d3883e450ull, 0x3c903186bb800000ull, 0x54907106bb180480ull,
	0x54908587053f41d2ull, 0x54b0d546bb580480ull, 0x3c00318d3883e453ull, 0x54001506bb780480ull,
	0x3c9021860583e196ull, 0x3c9061850583e155ull, 0x3c90a1840583e114ull, 0x5490f086bb580480ull,
	0x54002109057f2252ull, 0x02ffff30ff00d000ull, 0x540020c8051f4212ull, 0x5400200b053f32d2ull,
	0x3c00218a05830280ull, 0x3c4032c6bbf801c0ull, 0x3c0032c6bbf80180ull, 0x3c0032c6bbf80140ull,
	0x3de031817c83e305ull, 0x3c0032c238fb910full, 0x3c00318d3880d000ull, 0x3c0032c6bbf802c0ull,
	0x3c0032c6bbf80280ull, 0x3c2032c6bbf80240ull, 0x3c2032c6bbf80200ull, 0x3c00318d38815000ull,
	0x3c203183bb815000ull, 0x3c003186bb800000ull, 0x3c003186bb800000ull,
};

static uint32_t make_bo(int fd, uint32_t size, uint32_t *gpuva, void **cpu)
{
	struct drm_v3d_create_bo c; struct drm_v3d_mmap_bo m;
	memset(&c, 0, sizeof(c)); c.size = size;
	if (phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_CREATE_BO, &c) != 0) return 0;
	memset(&m, 0, sizeof(m)); m.handle = c.handle;
	if (phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_MMAP_BO, &m) != 0) return 0;
	*gpuva = c.offset; *cpu = (void *)(uintptr_t)m.offset; return c.handle;
}

static double now_ms(void)
{
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

int main(void)
{
	int fd = 0;
	struct drm_v3d_get_param gp;
	struct drm_v3d_submit_csd s;
	uint32_t shva, wva, xva, cva, unva, pva;
	void *shcpu, *wcpu, *xcpu, *ccpu, *uncpu, *pcpu;
	uint32_t shbo, wbo, xbo, cbo, unbo, pbo, h[6];
	float *w, *x, *cg;
	static float ref[D * GK];
	uint32_t *u;
	int i, j, k, si;
	const int sweep[] = { 256, 512, N_MAX };
	const int nsweep = (int)(sizeof(sweep) / sizeof(sweep[0]));

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("csd-gemm8: batched matmul M=%d K=%d N={256,512,%d} iters=%d\n", D, GK, N_MAX, ITERS);
	memset(&gp, 0, sizeof(gp)); gp.param = DRM_V3D_PARAM_SUPPORTS_CSD;
	phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_GET_PARAM, &gp);

	shbo = make_bo(fd, (uint32_t)sizeof(GEMM8), &shva, &shcpu);
	wbo = make_bo(fd, (uint32_t)D * N_MAX * 4u, &wva, &wcpu);
	xbo = make_bo(fd, (uint32_t)N_MAX * GK * 4u, &xva, &xcpu);
	cbo = make_bo(fd, (uint32_t)D * GK * 4u, &cva, &ccpu);
	unbo = make_bo(fd, 16u * 4u, &unva, &uncpu);
	pbo = make_bo(fd, 16u, &pva, &pcpu);
	if (!shbo || !wbo || !xbo || !cbo || !unbo || !pbo) { printf("csd-gemm8: BO alloc FAILED\n"); return 1; }
	memcpy(shcpu, GEMM8, sizeof(GEMM8));
	w = (float *)wcpu; x = (float *)xcpu; cg = (float *)ccpu; u = (uint32_t *)uncpu;

	memset(&s, 0, sizeof(s));
	s.cfg[0] = 4u << WG_COUNT_SHIFT;
	s.cfg[1] = 1u << WG_COUNT_SHIFT;
	s.cfg[2] = 1u << WG_COUNT_SHIFT;
	s.cfg[3] = (1u << WGS_PER_SG_SHIFT) | ((4u - 1u) << BATCHES_M1_SHIFT) | (64u << WG_SIZE_SHIFT);
	s.cfg[4] = 15u;
	s.cfg[5] = shva | CFG5_PROPAGATE_NANS | CFG5_THREADING;
	s.cfg[6] = unva;
	h[0] = shbo; h[1] = wbo; h[2] = xbo; h[3] = cbo; h[4] = unbo; h[5] = pbo;
	s.bo_handles = (uint64_t)(uintptr_t)h; s.bo_handle_count = 6;

	for (si = 0; si < nsweep; si++) {
		int N = sweep[si];
		uint32_t seed = 0x1234567u;
		double tg0, tg1, tc0, tc1;
		float maxrel = 0.0f;
		int it, r = 0, badi = 0;

		for (k = 0; k < D * N; k++) { seed = seed * 1103515245u + 12345u; w[k] = (float)((int32_t)(seed >> 9) & 0xffff) / 32768.0f - 1.0f; }
		for (k = 0; k < N * GK; k++) { seed = seed * 1103515245u + 12345u; x[k] = (float)((int32_t)(seed >> 9) & 0xffff) / 32768.0f - 1.0f; }

		/* 13-uniform CSGEMM8 layout (consts verbatim; SSBO VAs at [2][5][6][10]). */
		*(uint32_t *)pcpu = (uint32_t)N;
		u[0] = 0x0000ffffu; u[1] = 0x0000001au; u[2] = pva;        u[3] = 0x00000010u;
		u[4] = 0x00000014u; u[5] = wva;         u[6] = xva;        u[7] = 0xfffffffcu;
		u[8] = 0xfffffffcu; u[9] = 0xffffffe8u; u[10] = cva;       u[11] = 0xfffffffcu;
		u[12] = 0xfffffffcu;

		memset(ccpu, 0, D * GK * 4u);
		tg0 = now_ms();
		for (it = 0; it < ITERS; it++) r = phoenix_v3d_ioctl(fd, DRM_IOCTL_V3D_SUBMIT_CSD, &s);
		tg1 = now_ms();

		tc0 = now_ms();
		for (it = 0; it < ITERS; it++)
			for (i = 0; i < D; i++)
				for (k = 0; k < GK; k++) {
					float sum = 0.0f;
					for (j = 0; j < N; j++) sum += w[i * N + j] * x[j * GK + k];
					ref[i * GK + k] = sum;
				}
		tc1 = now_ms();

		for (i = 0; i < D * GK; i++) {
			float dd = fabsf(cg[i] - ref[i]);
			float rel = dd / (fabsf(ref[i]) + 1.0e-6f);
			if (rel > maxrel) { maxrel = rel; badi = i; }
		}
		printf("csd-gemm8: N=%4d  GPU=%.4f  CPU=%.4f ms/dispatch(%dx%d outs)  ratio=%.2fx  max_rel=%.3e  %s\n",
			N, (tg1 - tg0) / ITERS, (tc1 - tc0) / ITERS, D, GK,
			(tc1 - tc0) / (tg1 - tg0), maxrel,
			(r == 0 && maxrel < 5.0e-3f) ? "PASS" : "FAIL");
	}
	return 0;
}
