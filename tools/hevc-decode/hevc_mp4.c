/* hevc_mp4.c — see hevc_mp4.h. Minimal ISOBMFF → Annex-B HEVC demux.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "hevc_mp4.h"
#include "hevc_parse.h"   /* hevc_set_err() — shared hevc_err() channel */
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t rd64(const uint8_t *p) { return ((uint64_t)rd32(p) << 32) | rd32(p + 4); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

int hevc_mp4_detect(const uint8_t *buf, uint32_t len)
{
	return len >= 8 && memcmp(buf + 4, "ftyp", 4) == 0;
}

/* What the box scan collects; the go/no-go guard is applied afterwards. */
typedef struct {
	const uint8_t *buf;
	uint32_t len;
	uint32_t mdat_off, mdat_len, n_mdat;
	uint32_t hvcc_off, hvcc_len;   /* payload range (after the 8-byte box header) */
	uint32_t n_trak, n_vide, n_moof;
	int bad;                        /* a structurally-invalid box was seen */
} scan_t;

/* Container boxes we descend into (plain boxes; stsd is handled specially). */
static int is_container(const char *t)
{
	static const char *c[] = { "moov", "trak", "mdia", "minf", "stbl",
				   "edts", "dinf", "mvex", "udta", 0 };
	for (int i = 0; c[i]; i++)
		if (memcmp(t, c[i], 4) == 0)
			return 1;
	return 0;
}

/* Parse a Visual sample entry inside stsd and record its hvcC (if HEVC). */
static void scan_stsd(scan_t *s, uint32_t off, uint32_t end)
{
	/* FullBox: 4 bytes version/flags + 4 bytes entry_count, then entries. */
	uint32_t p = off + 8;
	if (p + 8 > end)
		return;
	uint32_t esize = rd32(s->buf + p);
	const char *etype = (const char *)(s->buf + p + 4);
	int hevc = (memcmp(etype, "hev1", 4) == 0) || (memcmp(etype, "hvc1", 4) == 0);
	if (!hevc || esize < 8 || p + esize > end)
		return;
	/* VisualSampleEntry: 8-byte box header + 78 bytes, then child boxes. */
	uint32_t c = p + 8 + 78, cend = p + esize;
	while (c + 8 <= cend) {
		uint32_t csize = rd32(s->buf + c);
		const char *ctype = (const char *)(s->buf + c + 4);
		if (csize < 8 || c + csize > cend) { s->bad = 1; return; }
		if (memcmp(ctype, "hvcC", 4) == 0) {
			s->hvcc_off = c + 8;
			s->hvcc_len = csize - 8;
		}
		c += csize;
	}
}

/* Recursive box walk; records the fields scan_t needs. */
static void scan_boxes(scan_t *s, uint32_t off, uint32_t end)
{
	while (off + 8 <= end && !s->bad) {
		uint32_t size = rd32(s->buf + off);
		const char *type = (const char *)(s->buf + off + 4);
		uint32_t hdr = 8;
		uint64_t bsize;
		if (size == 1) {
			if (off + 16 > end) { s->bad = 1; return; }
			bsize = rd64(s->buf + off + 8);
			hdr = 16;
		} else if (size == 0) {
			bsize = end - off;   /* extends to container end */
		} else {
			bsize = size;
		}
		if (bsize < hdr || off + bsize > end) { s->bad = 1; return; }

		/* Record fields (independent tests — a container like `trak` must
		 * be both counted AND descended into, so these are not an else-if
		 * chain with the recursion below). */
		if (memcmp(type, "trak", 4) == 0) s->n_trak++;
		else if (memcmp(type, "moof", 4) == 0) s->n_moof++;
		else if (memcmp(type, "mdat", 4) == 0) {
			s->n_mdat++;
			s->mdat_off = off + hdr;
			s->mdat_len = (uint32_t)(bsize - hdr);
		} else if (memcmp(type, "hdlr", 4) == 0) {
			/* FullBox(4) + pre_defined(4) + handler_type(4) */
			if (off + hdr + 12 <= end &&
			    memcmp(s->buf + off + hdr + 8, "vide", 4) == 0)
				s->n_vide++;
		}

		/* Descend: stsd holds a sample entry (parsed specially); other
		 * container boxes recurse generically. */
		if (memcmp(type, "stsd", 4) == 0)
			scan_stsd(s, off + hdr, off + (uint32_t)bsize);
		else if (is_container(type))
			scan_boxes(s, off + hdr, off + (uint32_t)bsize);

		off += (uint32_t)bsize;
	}
}

/* Growable output buffer. */
typedef struct { uint8_t *d; uint32_t len, cap; } obuf_t;
static int oput(obuf_t *b, const uint8_t *s, uint32_t n)
{
	if (b->len + n > b->cap) {
		uint32_t nc = b->cap ? b->cap : 65536;
		while (nc < b->len + n) nc *= 2;
		uint8_t *p = realloc(b->d, nc);
		if (!p) return -1;
		b->d = p; b->cap = nc;
	}
	memcpy(b->d + b->len, s, n);
	b->len += n;
	return 0;
}
static const uint8_t START_CODE[4] = { 0, 0, 0, 1 };
static int oput_nal(obuf_t *b, const uint8_t *nal, uint32_t n)
{
	return oput(b, START_CODE, 4) || oput(b, nal, n);
}

int hevc_mp4_to_annexb(const uint8_t *buf, uint32_t len,
		       uint8_t **out, uint32_t *out_len)
{
	scan_t s;
	memset(&s, 0, sizeof(s));
	s.buf = buf;
	s.len = len;
	scan_boxes(&s, 0, len);

	if (s.bad) return hevc_set_err("mp4: malformed box structure");
	if (s.n_moof) return hevc_set_err("mp4: fragmented (moof) files unsupported — remux with -c copy");
	if (s.n_trak != 1) return hevc_set_err("mp4: expected exactly 1 track — strip audio with `ffmpeg -an -c copy`");
	if (!s.n_vide) return hevc_set_err("mp4: track is not video (vide)");
	if (s.n_mdat != 1) return hevc_set_err("mp4: expected exactly 1 mdat box");
	if (!s.hvcc_len) return hevc_set_err("mp4: no hvcC record (not an HEVC track?)");

	/* hvcC record: fixed 22-byte prefix, then numOfArrays, then arrays. The
	 * byte at [21] low 2 bits = lengthSizeMinusOne; [22] = numOfArrays. */
	if (s.hvcc_len < 23) return hevc_set_err("mp4: hvcC record too short");
	const uint8_t *hc = buf + s.hvcc_off;
	uint32_t length_size = (hc[21] & 0x3) + 1;
	uint32_t num_arrays = hc[22];

	obuf_t o;
	memset(&o, 0, sizeof(o));

	/* Parameter sets (VPS/SPS/PPS) from hvcC, in record order. */
	uint32_t p = 23;
	for (uint32_t a = 0; a < num_arrays; a++) {
		if (p + 3 > s.hvcc_len) { free(o.d); return hevc_set_err("mp4: hvcC array header overrun"); }
		uint32_t cnt = rd16(hc + p + 1);
		p += 3;
		for (uint32_t n = 0; n < cnt; n++) {
			if (p + 2 > s.hvcc_len) { free(o.d); return hevc_set_err("mp4: hvcC NALU length overrun"); }
			uint32_t nl = rd16(hc + p);
			p += 2;
			if (p + nl > s.hvcc_len) { free(o.d); return hevc_set_err("mp4: hvcC NALU payload overrun"); }
			if (oput_nal(&o, hc + p, nl)) { free(o.d); return hevc_set_err("mp4: out of memory"); }
			p += nl;
		}
	}

	/* Coded NALs: walk mdat as length-prefixed units (self-delimiting for a
	 * single track — no sample tables needed). */
	uint32_t m = s.mdat_off, mend = s.mdat_off + s.mdat_len;
	while (m + length_size <= mend) {
		uint32_t nl = 0;
		for (uint32_t i = 0; i < length_size; i++)
			nl = (nl << 8) | buf[m + i];
		m += length_size;
		if (nl == 0 || m + nl > mend) { free(o.d); return hevc_set_err("mp4: truncated/invalid NAL length in mdat"); }
		if (oput_nal(&o, buf + m, nl)) { free(o.d); return hevc_set_err("mp4: out of memory"); }
		m += nl;
	}
	if (m != mend) { free(o.d); return hevc_set_err("mp4: mdat did not end on a NAL boundary"); }
	if (!o.len) { free(o.d); return hevc_set_err("mp4: no coded NAL units found"); }

	*out = o.d;
	*out_len = o.len;
	return 0;
}
