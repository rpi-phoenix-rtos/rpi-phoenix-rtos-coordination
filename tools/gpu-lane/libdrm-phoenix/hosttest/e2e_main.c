/*
 * Phoenix-RTOS
 *
 * libdrm-phoenix host harness - runs the real drmprobe against the fake servers
 *
 * Usage: e2e [dri|legacy]   dri: the servers also registered /dev/dri names
 *        FAKE_V3DA_PROTO=2   the fake render server predates G4 (negative control)
 *        FAKE_V3DA_PROTO=3   the fake render server predates G6 (negative control)
 *        FAKE_KMS_PROTO=1    the fake display server predates G7 (negative control)
 *        FAKE_KMS_IMPORT_HIGH=1  card0 imports land above 1 GiB (ADDFB2 must refuse them)
 *        FAKE_V3DA_HIGH=1    render BOs lie above 1 GiB unless placed (V3DA_BO_LOWMEM, proto 5)
 *        FAKE_V3DA_EAGER=1   every job is done at submit (G6: the race is never provoked)
 *        FAKE_V3DA_PROTO=4   the fake render server predates proto 5 (ignores the placement flag)
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "v3da_proto.h"

int drmprobe_main(int argc, char **argv);
void fake_set_dri(int on);
uint32_t fake_unaligned_ends(void);
uint32_t fake_payload_msgs(void);
uint32_t fake_msgs(void);
void fake_last_cl(v3da_cl_desc_t *d, uint32_t *nbo, uint32_t *nin, uint32_t *nout, uint32_t *submits);
uint32_t fake_deferred_flips(void);
uint32_t fake_kms_aliases(void);
void fake_m3p2(uint32_t *fstats, uint32_t *atsizes, uint32_t *imports, uint32_t *imports_closed);
void fake_set_v3d_proto(uint32_t proto);
uint32_t fake_g6_queries(void);
void fake_g4(uint32_t *exports_live, uint32_t *v3dbuf_imports, uint32_t *bos_live);
void fake_set_kms(int old_kms, int import_high);
void fake_g7(uint32_t *imports, uint32_t *imports_live, uint32_t *imports_released);
void fake_set_v3d_high(int on);
void fake_set_eager(int on);
uint32_t fake_lowmem_bos(void);


int main(int argc, char **argv)
{
	char a0[] = "drmprobe", a1[] = "-n", a2[] = "8";
	char *av[] = { a0, a1, a2, NULL };
	v3da_cl_desc_t d;
	uint32_t nbo, nin, nout, submits, fstats, atsizes, imports, imports_closed, exports, vimports, bos;
	uint32_t kimports, kimports_live, kimports_released;
	const char *old = getenv("FAKE_V3DA_PROTO");
	const char *old_kms = getenv("FAKE_KMS_PROTO"), *high = getenv("FAKE_KMS_IMPORT_HIGH");
	const char *v3d_high = getenv("FAKE_V3DA_HIGH");
	char proto[12];
	int dri = ((argc > 1) && (strcmp(argv[1], "dri") == 0)) ? 1 : 0, rc;

	fake_set_dri(dri);
	fake_set_v3d_proto((old != NULL) ? (uint32_t)atoi(old) : V3DA_PROTO_VERSION);
	fake_set_kms((old_kms != NULL) && (strcmp(old_kms, "1") == 0), (high != NULL) && (strcmp(high, "1") == 0));
	fake_set_v3d_high((v3d_high != NULL) && (strcmp(v3d_high, "1") == 0));
	fake_set_eager((getenv("FAKE_V3DA_EAGER") != NULL) && (strcmp(getenv("FAKE_V3DA_EAGER"), "1") == 0));
	(void)snprintf(proto, sizeof(proto), "%s", (old != NULL) ? old : "5");
	rc = drmprobe_main(3, av);
	fake_last_cl(&d, &nbo, &nin, &nout, &submits);
	printf("HOSTE2E mode=%s rc=%d submits=%u last_cl bcl=0x%x..0x%x rcl=0x%x..0x%x qma=0x%x qms=%u qts=0x%x nbo=%u nin=%u "
		"nout=%u msgs=%u payload_msgs=%u unaligned_ends=%u\n", dri ? "dri" : "legacy", rc, submits, d.bcl_start,
		d.bcl_end, d.rcl_start, d.rcl_end, d.qma, d.qms, d.qts, nbo, nin, nout, fake_msgs(), fake_payload_msgs(),
		fake_unaligned_ends());
	fake_m3p2(&fstats, &atsizes, &imports, &imports_closed);
	printf("HOSTE2E m3p2 mode=%s fstats=%u atsizes=%u imports=%u imports_closed=%u deferred_flips=%u\n",
		dri ? "dri" : "legacy", fstats, atsizes, imports, imports_closed, fake_deferred_flips());
	fake_g4(&exports, &vimports, &bos);
	printf("HOSTE2E g4 mode=%s server_proto=%s exports_live=%u v3dbuf_imports=%u bos_live=%u\n", dri ? "dri" : "legacy",
		proto, exports, vimports, bos);
	fake_g7(&kimports, &kimports_live, &kimports_released);
	printf("HOSTE2E g7 mode=%s kms_proto=%s import_high=%d card0_imports=%u imports_live=%u imports_released=%u\n",
		dri ? "dri" : "legacy", (old_kms != NULL) ? old_kms : "2", (high != NULL) && (strcmp(high, "1") == 0), kimports,
		kimports_live, kimports_released);
	printf("HOSTE2E g6 mode=%s server_proto=%s last_fence_queries=%u deferred_flips=%u\n", dri ? "dri" : "legacy",
		proto, fake_g6_queries(), fake_deferred_flips());
	printf("HOSTE2E g5 mode=%s kmsbuf_aliases=%u deferred_flips=%u\n", dri ? "dri" : "legacy", fake_kms_aliases(),
		fake_deferred_flips());
	printf("HOSTE2E lowmem mode=%s server_proto=%s v3d_high=%d lowmem_bos=%u\n", dri ? "dri" : "legacy", proto,
		(v3d_high != NULL) && (strcmp(v3d_high, "1") == 0), fake_lowmem_bos());
	return 0;
}
