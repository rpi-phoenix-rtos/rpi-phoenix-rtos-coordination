/* hevc_parse_test.c — throwaway host test for hevc_parse.c.
 * Parses every .265 given on the command line, tracks the in-band SPS/PPS, and
 * prints one machine-readable line per coded frame plus an EPB report.
 *   Row: ROW <file> <frameidx> <nal_type> <W> <H> <type> <poc> <qp> <dbo> <bfnum>
 *   EPB: EPB <file> hdr=<n> data=<n>   (n = #emulation-prevention bytes)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "hevc_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *slurp(const char *path, uint32_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return NULL; }
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = malloc(n);
	if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
	fclose(f);
	*len = (uint32_t)n;
	return b;
}

static const char *typename(int t) { return t == 2 ? "I" : t == 1 ? "P" : "B"; }

int main(int argc, char **argv)
{
	int rc = 0;
	for (int a = 1; a < argc; a++) {
		uint32_t len;
		uint8_t *buf = slurp(argv[a], &len);
		if (!buf) { rc = 1; continue; }
		const char *base = strrchr(argv[a], '/');
		base = base ? base + 1 : argv[a];

		hevc_sps_t sps; int have_sps = 0;
		hevc_pps_t pps; int have_pps = 0;
		hevc_nal_iter_t it = { buf, len, 0, 0 };
		hevc_nal_t nal;
		int frame = 0;
		uint32_t epb_hdr = 0, epb_data = 0;

		while (hevc_nal_next(&it, &nal)) {
			if (nal.type == HEVC_NAL_SPS) {
				if (hevc_parse_sps(nal.data, nal.len, &sps) < 0) {
					printf("ERR %s SPS: %s\n", base, hevc_err());
					rc = 1;
				} else have_sps = 1;
			} else if (nal.type == HEVC_NAL_PPS) {
				if (hevc_parse_pps(nal.data, nal.len, &pps) < 0) {
					printf("ERR %s PPS: %s\n", base, hevc_err());
					rc = 1;
				} else have_pps = 1;
			} else if (nal.type == HEVC_NAL_TRAIL_N ||
			           nal.type == HEVC_NAL_TRAIL_R ||
			           nal.type == HEVC_NAL_IDR_W_RADL ||
			           nal.type == HEVC_NAL_IDR_N_LP) {
				hevc_slice_t s;
				if (hevc_parse_slice(nal.data, nal.len, nal.type,
				                     have_sps ? &sps : NULL,
				                     have_pps ? &pps : NULL, &s) < 0) {
					printf("ERR %s slice: %s\n", base, hevc_err());
					rc = 1; frame++; continue;
				}
				/* EPB accounting: header region (first dbo bytes) vs the
				 * coded slice data that follows. */
				epb_hdr += hevc_count_epb(nal.data, s.data_byte_offset);
				epb_data += hevc_count_epb(nal.data + s.data_byte_offset,
				                           nal.len - s.data_byte_offset);
				printf("ROW %s %d %d %u %u %s %u %d %u %u  l0=%u l1=%u mmc=%u mvdl1z=%d cabac=%d\n",
				       base, frame, nal.type,
				       have_sps ? sps.width : 0,
				       have_sps ? sps.height : 0,
				       typename(s.slice_type), s.poc, s.slice_qp,
				       s.data_byte_offset, s.bfnum,
				       s.nb_refs_l0, s.nb_refs_l1, s.max_num_merge_cand,
				       s.mvd_l1_zero_flag, s.cabac_init_flag);
				frame++;
			}
			/* other NAL types (VPS 32, SEI 39/40, AUD 35) ignored */
		}
		printf("EPB %s hdr=%u data=%u\n", base, epb_hdr, epb_data);
		free(buf);
	}
	return rc;
}
