/*
 * Phoenix-RTOS
 *
 * libdrm-phoenix host harness - runs the real drmprobe against the fake servers
 *
 * Usage: e2e [dri|legacy]   dri: the servers also registered /dev/dri names
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <string.h>

#include "v3da_proto.h"

int drmprobe_main(int argc, char **argv);
void fake_set_dri(int on);
uint32_t fake_unaligned_ends(void);
uint32_t fake_payload_msgs(void);
uint32_t fake_msgs(void);
void fake_last_cl(v3da_cl_desc_t *d, uint32_t *nbo, uint32_t *nin, uint32_t *nout, uint32_t *submits);
uint32_t fake_deferred_flips(void);
void fake_m3p2(uint32_t *fstats, uint32_t *atsizes, uint32_t *imports, uint32_t *imports_closed);


int main(int argc, char **argv)
{
	char a0[] = "drmprobe", a1[] = "-n", a2[] = "8";
	char *av[] = { a0, a1, a2, NULL };
	v3da_cl_desc_t d;
	uint32_t nbo, nin, nout, submits, fstats, atsizes, imports, imports_closed;
	int dri = ((argc > 1) && (strcmp(argv[1], "dri") == 0)) ? 1 : 0, rc;

	fake_set_dri(dri);
	rc = drmprobe_main(3, av);
	fake_last_cl(&d, &nbo, &nin, &nout, &submits);
	printf("HOSTE2E mode=%s rc=%d submits=%u last_cl bcl=0x%x..0x%x rcl=0x%x..0x%x qma=0x%x qms=%u qts=0x%x nbo=%u nin=%u "
		"nout=%u msgs=%u payload_msgs=%u unaligned_ends=%u\n", dri ? "dri" : "legacy", rc, submits, d.bcl_start,
		d.bcl_end, d.rcl_start, d.rcl_end, d.qma, d.qms, d.qts, nbo, nin, nout, fake_msgs(), fake_payload_msgs(),
		fake_unaligned_ends());
	fake_m3p2(&fstats, &atsizes, &imports, &imports_closed);
	printf("HOSTE2E m3p2 mode=%s fstats=%u atsizes=%u imports=%u imports_closed=%u deferred_flips=%u\n",
		dri ? "dri" : "legacy", fstats, atsizes, imports, imports_closed, fake_deferred_flips());
	return 0;
}
