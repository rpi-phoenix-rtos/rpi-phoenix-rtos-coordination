/*
 * mlp_gpu — GPU-accelerated MNIST MLP inference on V3D (CSD compute matmul).
 *
 * A real 3-layer MLP (784->256->256->10, ReLU) forward pass with every matmul
 * run on the GPU via the CSD path, vs the same forward on the CPU. Proves an
 * end-to-end neural-net inference is BOTH numerically correct (predictions match
 * the numpy reference, incl. a model-misclassified digit) AND faster on the GPU
 * (the 256-wide layers land in the measured GPU sweet spot, ~11x at N<=512).
 *
 * Every layer is dispatched at the proven fixed grid D=256; the 10-class output
 * layer zero-pads its weight to 256 rows and uses the first 10 (no grid change).
 * Kernel o[i]=sum_j W[i*N+j]*x[j] (runtime N via params SSBO) = v3d-shader-tool
 * CSMATMUL, identical to csd_matmul.c. Weights/test digits from mlp_data.h.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "v3d_drm.h"
#include "mlp_data.h"

extern int phoenix_v3d_ioctl(int fd, unsigned long request, void *arg);

#define D 256               /* fixed dispatch grid (outputs per matmul) */
#define NMAX 784            /* largest layer input dimension */
#define ITERS 50            /* timing repeats over the whole test set */
#define WG_COUNT_SHIFT   16
#define WGS_PER_SG_SHIFT 8
#define BATCHES_M1_SHIFT 12
#define WG_SIZE_SHIFT    0
#define CFG5_PROPAGATE_NANS (1u << 2)
#define CFG5_THREADING      (1u << 0)

/* CSMATMUL QPU (31 words), local_size=64, N runtime from ssbo3[0]. Same as csd_matmul.c. */
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

/* Globals for the GPU layer helper (BOs allocated once, reused every layer). */
static int g_fd;
static struct drm_v3d_submit_csd g_s;
static float *g_x, *g_o;
static uint32_t *g_params, *g_u;

/* One GPU layer: out[i]=relu?(sum_j W[i*nin+j]*in[j] + bias[i]), i<nout.
 * Weights live in a PERSISTENT per-layer BO (uploaded once, padded to 256 rows) —
 * we only point uniform[4] at that BO's GPU-VA, stream the activation into g_x,
 * dispatch, and read back. No per-call weight upload (that was the bottleneck). */
static void gpu_layer(uint32_t w_va, const float *bias, const float *in,
                      int nout, int nin, int relu, float *out)
{
	int i, j;
	for (j = 0; j < nin; j++) g_x[j] = in[j];
	g_u[4] = w_va;                                  /* binding0 (W) VA for this layer */
	g_params[0] = (uint32_t)nin;
	phoenix_v3d_ioctl(g_fd, DRM_IOCTL_V3D_SUBMIT_CSD, &g_s);
	for (i = 0; i < nout; i++) {
		float v = g_o[i] + bias[i];
		out[i] = (relu && v < 0.0f) ? 0.0f : v;
	}
}

static void cpu_layer(const float *W, const float *bias, const float *in,
                      int nout, int nin, int relu, float *out)
{
	int i, j;
	for (i = 0; i < nout; i++) {
		float s = bias[i];
		for (j = 0; j < nin; j++) s += W[i * nin + j] * in[j];
		out[i] = (relu && s < 0.0f) ? 0.0f : s;
	}
}

static int argmax(const float *v, int n)
{
	int i, m = 0;
	for (i = 1; i < n; i++) if (v[i] > v[m]) m = i;
	return m;
}

int main(void)
{
	struct drm_v3d_get_param gp;
	uint32_t shva, w1va, w2va, w3va, xva, ova, unva, pva;
	void *shcpu, *w1cpu, *w2cpu, *w3cpu, *xcpu, *ocpu, *uncpu, *pcpu;
	uint32_t shbo, w1bo, w2bo, w3bo, xbo, obo, unbo, pbo, h[8];
	uint32_t *u;
	float h1[H], h2[H], out[NCLS];
	int t, it, gpu_ok = 0, cpu_ok = 0;
	double tg0, tg1, tc0, tc1;

	g_fd = 0;
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("mlp-gpu: MNIST MLP 784->256->256->10 GPU(CSD) vs CPU, NTEST=%d\n", NTEST);
	memset(&gp, 0, sizeof(gp)); gp.param = DRM_V3D_PARAM_SUPPORTS_CSD;
	phoenix_v3d_ioctl(g_fd, DRM_IOCTL_V3D_GET_PARAM, &gp);

	shbo = make_bo(g_fd, (uint32_t)sizeof(MATMUL), &shva, &shcpu);
	w1bo = make_bo(g_fd, (uint32_t)D * IN * 4u, &w1va, &w1cpu);   /* 256 x 784 */
	w2bo = make_bo(g_fd, (uint32_t)D * H * 4u, &w2va, &w2cpu);    /* 256 x 256 */
	w3bo = make_bo(g_fd, (uint32_t)D * H * 4u, &w3va, &w3cpu);    /* 256 x 256 (10 real rows) */
	xbo = make_bo(g_fd, (uint32_t)NMAX * 4u, &xva, &xcpu);
	obo = make_bo(g_fd, (uint32_t)D * 4u, &ova, &ocpu);
	unbo = make_bo(g_fd, 8u * 4u, &unva, &uncpu);
	pbo = make_bo(g_fd, 16u, &pva, &pcpu);
	if (!shbo || !w1bo || !w2bo || !w3bo || !xbo || !obo || !unbo || !pbo) {
		printf("mlp-gpu: BO alloc FAILED\n"); return 1;
	}
	memcpy(shcpu, MATMUL, sizeof(MATMUL));
	g_x = (float *)xcpu; g_o = (float *)ocpu; g_params = (uint32_t *)pcpu;
	/* Preload weights ONCE into persistent per-layer BOs (rows >=nout zero-padded). */
	memset(w1cpu, 0, (size_t)D * IN * 4); memcpy(w1cpu, mlp_w1, (size_t)H * IN * 4);
	memset(w2cpu, 0, (size_t)D * H * 4);  memcpy(w2cpu, mlp_w2, (size_t)H * H * 4);
	memset(w3cpu, 0, (size_t)D * H * 4);  memcpy(w3cpu, mlp_w3, (size_t)NCLS * H * 4);

	/* Fixed D=256 dispatch (4 wg * local_size 64) + fixed uniform VAs. */
	memset(&g_s, 0, sizeof(g_s));
	g_s.cfg[0] = 4u << WG_COUNT_SHIFT;
	g_s.cfg[1] = 1u << WG_COUNT_SHIFT;
	g_s.cfg[2] = 1u << WG_COUNT_SHIFT;
	g_s.cfg[3] = (1u << WGS_PER_SG_SHIFT) | ((4u - 1u) << BATCHES_M1_SHIFT) | (64u << WG_SIZE_SHIFT);
	g_s.cfg[4] = 15u;
	g_s.cfg[5] = shva | CFG5_PROPAGATE_NANS | CFG5_THREADING;
	g_s.cfg[6] = unva;
	h[0] = shbo; h[1] = w1bo; h[2] = w2bo; h[3] = w3bo; h[4] = xbo; h[5] = obo; h[6] = unbo; h[7] = pbo;
	g_s.bo_handles = (uint64_t)(uintptr_t)h; g_s.bo_handle_count = 8;
	u = (uint32_t *)uncpu; g_u = u;
	/* u[4] (W VA) is set per-layer by gpu_layer; others fixed. */
	u[0] = 0x0000ffffu; u[1] = 0x0000001au; u[2] = pva; u[3] = 0x0000000cu;
	u[4] = w1va; u[5] = xva; u[6] = 0xfffffff0u; u[7] = ova;

	/* Correctness: GPU + CPU predictions vs the numpy reference (mlp_ref). */
	for (t = 0; t < NTEST; t++) {
		const float *img = &mlp_test[t * IN];
		int pg, pc;
		gpu_layer(w1va, mlp_b1, img, H, IN, 1, h1);
		gpu_layer(w2va, mlp_b2, h1, H, H, 1, h2);
		gpu_layer(w3va, mlp_b3, h2, NCLS, H, 0, out);
		pg = argmax(out, NCLS);
		cpu_layer(mlp_w1, mlp_b1, img, H, IN, 1, h1);
		cpu_layer(mlp_w2, mlp_b2, h1, H, H, 1, h2);
		cpu_layer(mlp_w3, mlp_b3, h2, NCLS, H, 0, out);
		pc = argmax(out, NCLS);
		if (pg == mlp_ref[t]) gpu_ok++;
		if (pc == mlp_ref[t]) cpu_ok++;
		printf("mlp-gpu: digit %2d  gpu=%d cpu=%d ref=%d lbl=%d  %s\n",
			t, pg, pc, mlp_ref[t], mlp_lbl[t], (pg == mlp_ref[t]) ? "ok" : "MISMATCH");
	}
	printf("mlp-gpu: GPU preds match numpy ref %d/%d ; CPU %d/%d\n", gpu_ok, NTEST, cpu_ok, NTEST);

	/* Timing: whole test set forward pass, GPU vs CPU, ITERS repeats. */
	tg0 = now_ms();
	for (it = 0; it < ITERS; it++)
		for (t = 0; t < NTEST; t++) {
			const float *img = &mlp_test[t * IN];
			gpu_layer(w1va, mlp_b1, img, H, IN, 1, h1);
			gpu_layer(w2va, mlp_b2, h1, H, H, 1, h2);
			gpu_layer(w3va, mlp_b3, h2, NCLS, H, 0, out);
		}
	tg1 = now_ms();
	tc0 = now_ms();
	for (it = 0; it < ITERS; it++)
		for (t = 0; t < NTEST; t++) {
			const float *img = &mlp_test[t * IN];
			cpu_layer(mlp_w1, mlp_b1, img, H, IN, 1, h1);
			cpu_layer(mlp_w2, mlp_b2, h1, H, H, 1, h2);
			cpu_layer(mlp_w3, mlp_b3, h2, NCLS, H, 0, out);
		}
	tc1 = now_ms();
	printf("mlp-gpu: forward %d imgs x %d: GPU %.3f ms CPU %.3f ms (%.2fx) per-img GPU %.4f ms\n",
		NTEST, ITERS, tg1 - tg0, tc1 - tc0, (tc1 - tc0) / (tg1 - tg0),
		(tg1 - tg0) / (ITERS * NTEST));
	printf("mlp-gpu: %s\n", (gpu_ok == NTEST) ? "PASS (GPU inference correct)" : "FAIL");
	return 0;
}
