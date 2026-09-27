/*
 * Phoenix-RTOS
 *
 * rpi4-kms host test - the scan-out rules for imported buffers (gap G7,
 * kms_scanout.h), native gcc, no Pi
 *
 * -DSCANOUT_TEST_NO_RULES builds the same checks against "no import rules" (what
 * rpi4-kms did before G7 would have needed to accept) - the negative control: the
 * refusal checks must then FAIL.
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

#include "kms_scanout.h"

#ifdef SCANOUT_TEST_NO_RULES
#define why_buf(pa, size, contig)  ((void)(pa), (void)(size), (void)(contig), (const char *)NULL)
#define why_fb(rq, size, why)      ((void)(rq), (void)(size), (why))
#else
#define why_buf(pa, size, contig)  kms_import_why((pa), (size), (contig))
#define why_fb(rq, size, why)      kms_import_fb_why((rq), (size), (why))
#endif

#define MOD_BROADCOM_UIF 0x0700000000000006ull   /* fourcc_mod_code(BROADCOM, 6) */
#define FB_1080 (7680ull * 1080ull)

static int checks, fails;


static void expect(const char *what, const char *got, const char *want)
{
	int ok = ((got == NULL) && (want == NULL)) || ((got != NULL) && (want != NULL) && (strcmp(got, want) == 0));

	checks++;
	if (!ok) {
		fails++;
	}
	printf("KMSHOST %-44s got=%-9s want=%-9s %s\n", what, (got != NULL) ? got : "ok", (want != NULL) ? want : "ok",
		ok ? "ok" : "FAIL");
}


static kms_addfb2_req_t fb(uint32_t w, uint32_t h, uint32_t pitch, uint32_t offset, uint64_t mod)
{
	kms_addfb2_req_t rq;

	memset(&rq, 0, sizeof(rq));
	rq.width = w;
	rq.height = h;
	rq.format = KMS_FMT_XRGB8888;
	rq.handle = 1u;
	rq.pitch = pitch;
	rq.offset = offset;
	rq.modifier = mod;
	return rq;
}


int main(void)
{
	kms_addfb2_req_t rq;
	const uint64_t size = (FB_1080 + 4095u) & ~4095ull;   /* 2025 pages, as a 1920x1080 render BO */

	/* the buffer itself */
	expect("buf low, contiguous", why_buf(0x06000000ull, size, 1), NULL);
	expect("buf ends exactly at 1 GiB", why_buf(KMS_SCANOUT_LIMIT - size, size, 1), NULL);
	expect("buf crosses 1 GiB by one page", why_buf(KMS_SCANOUT_LIMIT - size + 4096u, size, 1), "above_1g");
	expect("buf at 1 GiB", why_buf(KMS_SCANOUT_LIMIT, size, 1), "above_1g");
	expect("buf at 3.5 GiB", why_buf(0xe0000000ull, size, 1), "above_1g");
	expect("buf not contiguous", why_buf(0x06000000ull, size, 0), "noncontig");
	expect("buf size 0", why_buf(0x06000000ull, 0u, 1), "above_1g");
	expect("buf pa wraps", why_buf(~0ull - 4095u, size, 1), "above_1g");

	/* ADDFB2 of it */
	rq = fb(1920u, 1080u, 7680u, 0u, KMS_MOD_LINEAR);
	expect("fb 1920x1080 XRGB pitch 7680 LINEAR", why_fb(&rq, size, NULL), NULL);
	expect("fb ... but the buffer is above 1 GiB", why_fb(&rq, size, "above_1g"), "above_1g");
	rq = fb(1920u, 1080u, 7680u, 0u, MOD_BROADCOM_UIF);
	expect("fb UIF modifier", why_fb(&rq, size, NULL), "modifier");
	rq = fb(1920u, 1080u, 7684u, 0u, KMS_MOD_LINEAR);
	expect("fb pitch 7684 (not 64-aligned)", why_fb(&rq, size, NULL), "align");
	rq = fb(1920u, 1080u, 7680u, 32u, KMS_MOD_LINEAR);
	expect("fb offset 32 (not 64-aligned)", why_fb(&rq, size, NULL), "align");
	rq = fb(1920u, 1080u, 4096u, 0u, KMS_MOD_LINEAR);
	expect("fb pitch < width * 4", why_fb(&rq, size, NULL), "size");
	rq = fb(1920u, 1080u, 7680u, 0u, KMS_MOD_LINEAR);
	expect("fb larger than a 64 KiB buffer", why_fb(&rq, 65536u, NULL), "size");
	rq = fb(1920u, 1080u, 7680u, 7680u, KMS_MOD_LINEAR);
	expect("fb offset one row past the end", why_fb(&rq, FB_1080, NULL), "size");
	rq = fb(64u, 64u, 256u, 0u, KMS_MOD_LINEAR);
	expect("fb 64x64 cursor-sized", why_fb(&rq, 65536u, NULL), NULL);

	printf("KMSHOST RESULT checks=%d fails=%d verdict=%s\n", checks, fails, (fails == 0) ? "PASS" : "FAIL");
	return (fails == 0) ? 0 : 1;
}
