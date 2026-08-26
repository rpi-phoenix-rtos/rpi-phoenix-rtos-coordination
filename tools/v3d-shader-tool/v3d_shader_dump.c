/*
 * v3d_shader_dump — host tool: compile a trivial shader through Mesa's v3d
 * NIR->QPU compiler and print the QPU bytecode (u64 words). Validate the output
 * with the built qpu_disasm. Lets us derive correct, validated V3D-4.2 shaders
 * off-device for the Phoenix bare-metal triangle (GLQuake Path A).
 *
 * NOT upstream Mesa — a local harness in the clone. Build as an in-tree meson
 * executable linked against the v3d compiler libs.
 */
#include <stdio.h>
#include <stdlib.h>
#include "broadcom/common/v3d_device_info.h"
#include "broadcom/compiler/v3d_compiler.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/glsl_types.h"
#include "qpu/qpu_disasm.h"
#include "util/ralloc.h"

static void dbg_out(const char *msg, void *data) { (void)data; fprintf(stderr, "%s", msg); }

static unsigned v3d_type_size(const struct glsl_type *type, bool bindless)
{ (void)bindless; return glsl_count_attribute_slots(type, false); }

/* Replicate the driver's pre-compile NIR finalization so I/O is wired correctly
 * (gather_info from the actual IR; nir_lower_io for FS; v3d does VS I/O itself). */
static void finalize_nir(nir_shader *s)
{
	nir_shader_gather_info(s, nir_shader_get_entrypoint(s));
	if (s->info.stage == MESA_SHADER_FRAGMENT) {
		nir_lower_fragcolor(s, 1); /* gl_FragColor -> per-RT outputs (max_rb=1) */
		nir_lower_io(s, nir_var_shader_in | nir_var_shader_out, v3d_type_size, 0);
	}
	v3d_optimize_nir(NULL, s);
	nir_lower_var_copies(s);
}

/* v3d NIR compiler options — copied verbatim from gallium/drivers/v3d/v3d_screen.c
 * v3d_screen_get_compiler_options() (lower_fsat set for ver<71 = true at v4.2). */
static nir_shader_compiler_options v3d_options =
{
                .compact_arrays = true,
                .lower_uadd_sat = true,
                .lower_usub_sat = true,
                .lower_iadd_sat = true,
                .lower_extract_byte = true,
                .lower_extract_word = true,
                .lower_insert_byte = true,
                .lower_insert_word = true,
                .lower_bitfield_insert = true,
                .lower_bitfield_extract = true,
                .lower_bitfield_extract16 = true,
                .lower_bitfield_extract8 = true,
                .lower_bitfield_reverse = true,
                .lower_bit_count = true,
                .lower_cs_local_id_to_index = true,
                .lower_ffract = true,
                .lower_fmod = true,
                .lower_pack_unorm_2x16 = true,
                .lower_pack_snorm_2x16 = true,
                .lower_pack_unorm_4x8 = true,
                .lower_pack_snorm_4x8 = true,
                .lower_unpack_unorm_4x8 = true,
                .lower_unpack_snorm_4x8 = true,
                .lower_pack_half_2x16 = true,
                .lower_unpack_half_2x16 = true,
                .lower_pack_32_2x16 = true,
                .lower_pack_32_2x16_split = true,
                .lower_unpack_32_2x16_split = true,
                .lower_fdiv = true,
                .lower_find_lsb = true,
                .lower_flrp32 = true,
                .lower_fpow = true,
                .lower_fsqrt = true,
                .lower_ifind_msb = true,
                .lower_isign = true,
                .lower_hadd = true,
                .lower_fisnormal = true,
                .lower_mul_high = true,
                .lower_wpos_pntc = true,
                .lower_to_scalar = true,
                .lower_interpolate_at = true,
                .lower_int64_options =
                        nir_lower_bcsel64 |
                        nir_lower_bit_count64 |
                        nir_lower_conv64 |
                        nir_lower_divmod64 |
                        nir_lower_iabs64 |
                        nir_lower_iadd64 |
                        nir_lower_icmp64 |
                        nir_lower_imul_2x32_64 |
                        nir_lower_imul64 |
                        nir_lower_ineg64 |
                        nir_lower_logic64 |
                        nir_lower_minmax64 |
                        nir_lower_shift64 |
                        nir_lower_ufind_msb64,
                .lower_fquantize2f16 = true,
                .lower_ufind_msb = true,
                .has_fsub = true,
                .has_isub = true,
                .has_imul24 = true,
                .has_umul24 = true,
                .has_uclz = true,
                .divergence_analysis_options =
                       nir_divergence_multiple_workgroup_per_compute_subgroup,
                /* This will enable loop unrolling in the state tracker so we won't
                 * be able to selectively disable it in backend if it leads to
                 * lower thread counts or TMU spills. Choose a conservative maximum to
                 * limit register pressure impact.
                 */
                .max_unroll_iterations = 16,
                .max_samples = 4,
                .force_indirect_unrolling_sampler = true,
                .scalarize_ddx = true,
                .max_varying_expression_cost = 4,
        };

int main(int argc, char **argv)
{
	/* V3D 4.2 device info, computed from the real Pi4 core IDENT regs
	 * (IDENT0=0x04443356, IDENT1=0x81001422) per v3d_get_device_info(). */
	struct v3d_device_info devinfo = { 0 };
	devinfo.ver = 42;                 /* major 4, minor 2 */
	devinfo.rev = 0;
	devinfo.vpm_size = 65536;         /* (IDENT1>>28 & 0xf)=8 * 8192 */
	devinfo.qpu_count = 8;            /* nslc(2) * qups(4) */
	devinfo.has_accumulators = true;  /* ver < 71 */
	devinfo.page_size = 4096;
	devinfo.clipper_xy_granularity = 256.0f; /* ver 42 */
	devinfo.cle_readahead = 256u;            /* ver 42 */
	v3d_options.lower_fsat = true; /* ver < 71 */

	glsl_type_singleton_init_or_ref();

	const struct v3d_compiler *compiler = v3d_compiler_init(&devinfo, 0);
	if (!compiler) { fprintf(stderr, "v3d_compiler_init failed\n"); return 1; }

	/* Minimal fragment shader: gl_FragData[0] = const vec4. */
	nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
		&v3d_options, "constfs");
	/* Emit store_output directly (v3d_compile expects I/O intrinsics, not var
	 * derefs — the driver runs nir_lower_io before compile; we skip that by
	 * emitting the intrinsic). */
	/* Output variable (drives c->outputs sizing in ntq_setup_outputs); the
	 * value is written via the store_output intrinsic below (no deref). */
	nir_variable *outc = nir_variable_create(b.shader, nir_var_shader_out,
		glsl_vec4_type(), "gl_FragColor");
	outc->data.location = FRAG_RESULT_DATA0;
	nir_store_var(&b, outc, nir_imm_vec4(&b, 0.07, 0.13, 0.20, 1.0), 0xf);
	finalize_nir(b.shader);

	struct v3d_fs_key fs_key = { 0 };
	fs_key.cbufs = 1;  /* RT0 present (bit mask) */

	struct v3d_prog_data *prog_data = NULL;
	uint32_t asm_size = 0;
	uint64_t *insts = v3d_compile(compiler, &fs_key.base, &prog_data,
		b.shader, dbg_out, NULL, 0, 0, &asm_size);
	if (!insts) { fprintf(stderr, "v3d_compile FAILED\n"); return 2; }

	uint32_t n = asm_size / sizeof(uint64_t);
	printf("/* FS QPU: %u instructions (%u bytes) */\n", n, asm_size);
	for (uint32_t i = 0; i < n; i++)
		printf("0x%016llxull, /* %s */\n", (unsigned long long)insts[i],
			v3d_qpu_disasm(&devinfo, insts[i]));

	/* ---- Vertex shader (passthrough position) + its coordinate variant ----
	 * Built deref-based like mesa's blit VS; v3d_compile lowers VS I/O itself. */
	for (int coord = 0; coord <= 1; coord++) {
		nir_builder vb = nir_builder_init_simple_shader(MESA_SHADER_VERTEX,
			&v3d_options, coord ? "coordvs" : "rendervs");
		nir_variable *pin = nir_variable_create(vb.shader, nir_var_shader_in,
			glsl_vec4_type(), "pos");
		pin->data.location = VERT_ATTRIB_GENERIC0;
		pin->data.driver_location = 0;
		nir_variable *pout = nir_variable_create(vb.shader, nir_var_shader_out,
			glsl_vec4_type(), "gl_Position");
		pout->data.location = VARYING_SLOT_POS;
		pout->data.driver_location = 0;
		nir_store_var(&vb, pout, nir_load_var(&vb, pin), 0xf);
		finalize_nir(vb.shader);

		struct v3d_vs_key vs_key = { 0 };
		vs_key.is_coord = coord;
		vs_key.num_used_outputs = 0;

		struct v3d_prog_data *vpd = NULL;
		uint32_t vsz = 0;
		uint64_t *vi = v3d_compile(compiler, &vs_key.base, &vpd, vb.shader,
			dbg_out, NULL, 0, 0, &vsz);
		if (!vi) { fprintf(stderr, "VS(coord=%d) compile FAILED\n", coord); continue; }
		uint32_t vn = vsz / sizeof(uint64_t);
		printf("/* %s QPU: %u instructions */\n", coord ? "COORD-VS" : "RENDER-VS", vn);
		for (uint32_t i = 0; i < vn; i++)
			printf("0x%016llxull, /* %s */\n", (unsigned long long)vi[i],
				v3d_qpu_disasm(&devinfo, vi[i]));
	}

	/* ---- Compute shader (CSD probe: out[gid] = gid) ----
	 * Models gallium v3d's compute path (v3d_program.c:281): a zeroed base
	 * v3d_key, plus nir_lower_compute_system_values to lower
	 * gl_GlobalInvocationID. Output is written to SSBO 0 so an on-device CSD
	 * harness can read it back and numerically verify the dispatch. */
	{
		nir_builder cb = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
			&v3d_options, "csprobe");
		cb.shader->info.workgroup_size[0] = 16;
		cb.shader->info.workgroup_size[1] = 1;
		cb.shader->info.workgroup_size[2] = 1;
		cb.shader->info.num_ssbos = 1;

		nir_def *gid = nir_load_global_invocation_id(&cb, 32);
		nir_def *idx = nir_channel(&cb, gid, 0);
		nir_def *byteoff = nir_imul_imm(&cb, idx, 4);
		nir_store_ssbo(&cb, idx, nir_imm_int(&cb, 0), byteoff,
			.write_mask = 0x1, .access = 0, .align_mul = 4, .align_offset = 0);

		nir_shader_gather_info(cb.shader, nir_shader_get_entrypoint(cb.shader));
		struct nir_lower_compute_system_values_options cs_opts = { 0 };
		nir_lower_compute_system_values(cb.shader, &cs_opts);
		v3d_optimize_nir(NULL, cb.shader);
		nir_lower_var_copies(cb.shader);

		struct v3d_key ckey = { 0 };
		struct v3d_prog_data *cpd = NULL;
		uint32_t csz = 0;
		uint64_t *ci = v3d_compile(compiler, &ckey, &cpd, cb.shader,
			dbg_out, NULL, 0, 0, &csz);
		if (!ci) {
			fprintf(stderr, "CS compile FAILED\n");
		}
		else {
			uint32_t cn = csz / sizeof(uint64_t);
			struct v3d_compute_prog_data *cs =
				(struct v3d_compute_prog_data *)cpd;
			printf("/* CS QPU: %u instructions; local_size=%ux%ux%u shared=%u */\n",
				cn, cs->local_size[0], cs->local_size[1], cs->local_size[2],
				cs->shared_size);
			for (uint32_t i = 0; i < cn; i++)
				printf("0x%016llxull, /* %s */\n", (unsigned long long)ci[i],
					v3d_qpu_disasm(&devinfo, ci[i]));
			printf("/* CS prog_data: threads=%u single_seg=%d uniforms=%u */\n",
				cpd->threads, (int)cpd->single_seg, cpd->uniforms.count);
			for (uint32_t u = 0; u < cpd->uniforms.count; u++)
				printf("/*   uniform[%u]: contents=%d data=0x%08x */\n",
					u, (int)cpd->uniforms.contents[u], cpd->uniforms.data[u]);
		}
	}

	/* ---- CSMATMUL: o[i] = sum_j w[i*N+j]*x[j], N RUNTIME via ssbo3[0] (microbench) ----
	 * N is read from a 4th SSBO (binding 3, element 0) so it drives BOTH the loop
	 * bound AND the row stride consistently — a compile-time MMN let the compiler
	 * bake the stride as an immediate while only the bound tracked a uniform, which
	 * corrupted results for N!=256 (HW-confirmed). One kernel now serves any N. */
	{
		nir_builder mb = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
			&v3d_options, "csmatmul");
		mb.shader->info.workgroup_size[0] = 64;
		mb.shader->info.workgroup_size[1] = 1;
		mb.shader->info.workgroup_size[2] = 1;
		mb.shader->info.num_ssbos = 4;

		nir_variable *sumv = nir_local_variable_create(mb.impl, glsl_float_type(), "sum");
		nir_variable *jvar = nir_local_variable_create(mb.impl, glsl_uint_type(), "j");
		nir_def *ii = nir_channel(&mb, nir_load_global_invocation_id(&mb, 32), 0);
		/* N = params[0] from ssbo binding 3 (hoisted out of the loop). */
		nir_def *nrt = nir_load_ssbo(&mb, 1, 32, nir_imm_int(&mb, 3),
			nir_imm_int(&mb, 0), .align_mul = 4, .align_offset = 0);
		nir_store_var(&mb, sumv, nir_imm_float(&mb, 0.0f), 0x1);
		nir_store_var(&mb, jvar, nir_imm_int(&mb, 0), 0x1);

		nir_push_loop(&mb);
		{
			nir_def *jj = nir_load_var(&mb, jvar);
			nir_break_if(&mb, nir_uge(&mb, jj, nrt));
			nir_def *widx = nir_iadd(&mb, nir_imul(&mb, ii, nrt), jj);
			nir_def *wval = nir_load_ssbo(&mb, 1, 32, nir_imm_int(&mb, 0),
				nir_imul_imm(&mb, widx, 4), .align_mul = 4, .align_offset = 0);
			nir_def *xval = nir_load_ssbo(&mb, 1, 32, nir_imm_int(&mb, 1),
				nir_imul_imm(&mb, jj, 4), .align_mul = 4, .align_offset = 0);
			nir_def *acc = nir_fadd(&mb, nir_load_var(&mb, sumv),
				nir_fmul(&mb, wval, xval));
			nir_store_var(&mb, sumv, acc, 0x1);
			nir_store_var(&mb, jvar, nir_iadd_imm(&mb, jj, 1), 0x1);
		}
		nir_pop_loop(&mb, NULL);

		nir_store_ssbo(&mb, nir_load_var(&mb, sumv), nir_imm_int(&mb, 2),
			nir_imul_imm(&mb, ii, 4), .write_mask = 0x1, .access = 0,
			.align_mul = 4, .align_offset = 0);

		nir_shader_gather_info(mb.shader, nir_shader_get_entrypoint(mb.shader));
		struct nir_lower_compute_system_values_options mo = { 0 };
		nir_lower_compute_system_values(mb.shader, &mo);
		nir_lower_vars_to_ssa(mb.shader);
		v3d_optimize_nir(NULL, mb.shader);
		nir_lower_var_copies(mb.shader);

		struct v3d_key mk = { 0 };
		struct v3d_prog_data *mpd = NULL;
		uint32_t msz = 0;
		uint64_t *mins = v3d_compile(compiler, &mk, &mpd, mb.shader,
			dbg_out, NULL, 0, 0, &msz);
		if (!mins) {
			fprintf(stderr, "CSMATMUL compile FAILED\n");
		}
		else {
			uint32_t mnn = msz / sizeof(uint64_t);
			printf("/* CSMATMUL QPU: %u instructions; threads=%u single_seg=%d uniforms=%u */\n",
				mnn, mpd->threads, (int)mpd->single_seg, mpd->uniforms.count);
			for (uint32_t z = 0; z < mnn; z++)
				printf("0x%016llxull, /* %s */\n", (unsigned long long)mins[z],
					v3d_qpu_disasm(&devinfo, mins[z]));
			for (uint32_t u = 0; u < mpd->uniforms.count; u++)
				printf("/*   uniform[%u]: contents=%d data=0x%08x */\n",
					u, (int)mpd->uniforms.contents[u], mpd->uniforms.data[u]);
		}
	}

	/* ---- CSCONST: store a constant to out[0] (TMU write, no gid math) — step 2 ---- */
	{
		nir_builder cb = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
			&v3d_options, "csconst");
		cb.shader->info.workgroup_size[0] = 16;
		cb.shader->info.workgroup_size[1] = 1;
		cb.shader->info.workgroup_size[2] = 1;
		cb.shader->info.num_ssbos = 1;
		nir_store_ssbo(&cb, nir_imm_int(&cb, 0xC0DE1234), nir_imm_int(&cb, 0),
			nir_imm_int(&cb, 0), .write_mask = 0x1, .access = 0,
			.align_mul = 4, .align_offset = 0);
		nir_shader_gather_info(cb.shader, nir_shader_get_entrypoint(cb.shader));
		struct nir_lower_compute_system_values_options o = { 0 };
		nir_lower_compute_system_values(cb.shader, &o);
		v3d_optimize_nir(NULL, cb.shader);
		nir_lower_var_copies(cb.shader);
		struct v3d_key k = { 0 };
		struct v3d_prog_data *pd = NULL;
		uint32_t sz = 0;
		uint64_t *ins = v3d_compile(compiler, &k, &pd, cb.shader,
			dbg_out, NULL, 0, 0, &sz);
		if (!ins) {
			fprintf(stderr, "CSCONST compile FAILED\n");
		}
		else {
			uint32_t nn = sz / sizeof(uint64_t);
			printf("/* CSCONST QPU: %u instructions; threads=%u single_seg=%d uniforms=%u */\n",
				nn, pd->threads, (int)pd->single_seg, pd->uniforms.count);
			for (uint32_t i = 0; i < nn; i++)
				printf("0x%016llxull, /* %s */\n", (unsigned long long)ins[i],
					v3d_qpu_disasm(&devinfo, ins[i]));
			for (uint32_t u = 0; u < pd->uniforms.count; u++)
				printf("/*   uniform[%u]: contents=%d data=0x%08x */\n",
					u, (int)pd->uniforms.contents[u], pd->uniforms.data[u]);
		}
	}

	/* ---- CSNOP: empty compute kernel (thread-end only) for CSD liveness ---- */
	{
		nir_builder nb = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
			&v3d_options, "csnop");
		nb.shader->info.workgroup_size[0] = 16;
		nb.shader->info.workgroup_size[1] = 1;
		nb.shader->info.workgroup_size[2] = 1;
		/* no body: just thread-end */
		nir_shader_gather_info(nb.shader, nir_shader_get_entrypoint(nb.shader));
		struct nir_lower_compute_system_values_options o = { 0 };
		nir_lower_compute_system_values(nb.shader, &o);
		v3d_optimize_nir(NULL, nb.shader);
		nir_lower_var_copies(nb.shader);

		struct v3d_key k = { 0 };
		struct v3d_prog_data *pd = NULL;
		uint32_t sz = 0;
		uint64_t *ins = v3d_compile(compiler, &k, &pd, nb.shader,
			dbg_out, NULL, 0, 0, &sz);
		if (!ins) {
			fprintf(stderr, "CSNOP compile FAILED\n");
		}
		else {
			uint32_t nn = sz / sizeof(uint64_t);
			printf("/* CSNOP QPU: %u instructions; threads=%u single_seg=%d uniforms=%u */\n",
				nn, pd->threads, (int)pd->single_seg, pd->uniforms.count);
			for (uint32_t i = 0; i < nn; i++)
				printf("0x%016llxull, /* %s */\n", (unsigned long long)ins[i],
					v3d_qpu_disasm(&devinfo, ins[i]));
		}
	}

	return 0;
}
