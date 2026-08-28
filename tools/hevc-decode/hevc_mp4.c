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

/* Per-track state gathered while walking one `trak` subtree. Box fields store
 * the payload range (after the 8-byte box header) of each sample table. */
typedef struct {
	int is_vide;
	uint32_t hvcc_off, hvcc_len;
	uint32_t stsz_off, stsz_len;
	uint32_t stco_off, stco_len;
	int      stco_is64;               /* co64 (64-bit) vs stco (32-bit) offsets */
	uint32_t stsc_off, stsc_len;
} trak_t;

typedef struct {
	const uint8_t *buf;
	uint32_t len;
	uint32_t n_vide, n_moof;
	trak_t vtrak;                     /* the selected video track */
	int bad;
} scan_t;

/* Container boxes we descend into inside a trak (stsd handled specially). */
static int is_trak_container(const char *t)
{
	static const char *c[] = { "mdia", "minf", "stbl", "edts", "dinf", 0 };
	for (int i = 0; c[i]; i++)
		if (memcmp(t, c[i], 4) == 0)
			return 1;
	return 0;
}

/* Parse a Visual sample entry inside stsd; record its hvcC if HEVC. */
static void scan_stsd(scan_t *s, trak_t *t, uint32_t off, uint32_t end)
{
	uint32_t p = off + 8;   /* FullBox version/flags(4) + entry_count(4) */
	if (p + 8 > end)
		return;
	uint32_t esize = rd32(s->buf + p);
	const char *etype = (const char *)(s->buf + p + 4);
	int hevc = (memcmp(etype, "hev1", 4) == 0) || (memcmp(etype, "hvc1", 4) == 0);
	if (!hevc || esize < 8 || p + esize > end)
		return;
	uint32_t c = p + 8 + 78, cend = p + esize;   /* VisualSampleEntry: box(8)+78 */
	while (c + 8 <= cend) {
		uint32_t csize = rd32(s->buf + c);
		const char *ctype = (const char *)(s->buf + c + 4);
		if (csize < 8 || c + csize > cend) { s->bad = 1; return; }
		if (memcmp(ctype, "hvcC", 4) == 0) {
			t->hvcc_off = c + 8;
			t->hvcc_len = csize - 8;
		}
		c += csize;
	}
}

/* Walk one trak subtree, filling *t (handler + sample-table box ranges). */
static void scan_trak(scan_t *s, trak_t *t, uint32_t off, uint32_t end)
{
	while (off + 8 <= end && !s->bad) {
		uint32_t size = rd32(s->buf + off);
		const char *type = (const char *)(s->buf + off + 4);
		uint32_t hdr = 8;
		uint64_t bsize;
		if (size == 1) {
			if (off + 16 > end) { s->bad = 1; return; }
			bsize = rd64(s->buf + off + 8); hdr = 16;
		} else if (size == 0) {
			bsize = end - off;
		} else {
			bsize = size;
		}
		if (bsize < hdr || off + bsize > end) { s->bad = 1; return; }
		uint32_t p = off + hdr, pend = off + (uint32_t)bsize;

		if (memcmp(type, "hdlr", 4) == 0) {
			if (p + 12 <= pend && memcmp(s->buf + p + 8, "vide", 4) == 0)
				t->is_vide = 1;
		} else if (memcmp(type, "stsz", 4) == 0) { t->stsz_off = p; t->stsz_len = pend - p; }
		else if (memcmp(type, "stco", 4) == 0)   { t->stco_off = p; t->stco_len = pend - p; t->stco_is64 = 0; }
		else if (memcmp(type, "co64", 4) == 0)   { t->stco_off = p; t->stco_len = pend - p; t->stco_is64 = 1; }
		else if (memcmp(type, "stsc", 4) == 0)   { t->stsc_off = p; t->stsc_len = pend - p; }
		else if (memcmp(type, "stsd", 4) == 0)   scan_stsd(s, t, p, pend);
		else if (is_trak_container(type))        scan_trak(s, t, p, pend);

		off += (uint32_t)bsize;
	}
}

/* Top-level walk: descend moov→trak, count video tracks + moof, select the
 * single video track. */
static void scan_top(scan_t *s, uint32_t off, uint32_t end)
{
	while (off + 8 <= end && !s->bad) {
		uint32_t size = rd32(s->buf + off);
		const char *type = (const char *)(s->buf + off + 4);
		uint32_t hdr = 8;
		uint64_t bsize;
		if (size == 1) {
			if (off + 16 > end) { s->bad = 1; return; }
			bsize = rd64(s->buf + off + 8); hdr = 16;
		} else if (size == 0) {
			bsize = end - off;
		} else {
			bsize = size;
		}
		if (bsize < hdr || off + bsize > end) { s->bad = 1; return; }

		if (memcmp(type, "moof", 4) == 0) {
			s->n_moof++;
		} else if (memcmp(type, "trak", 4) == 0) {
			trak_t t;
			memset(&t, 0, sizeof(t));
			scan_trak(s, &t, off + hdr, off + (uint32_t)bsize);
			if (t.is_vide) { s->n_vide++; s->vtrak = t; }
		} else if (memcmp(type, "moov", 4) == 0) {
			scan_top(s, off + hdr, off + (uint32_t)bsize);   /* traks live in moov */
		}
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

/* Emit the length-prefixed NALs of one sample [off,off+size) as Annex-B. */
static int emit_sample(obuf_t *o, const uint8_t *buf, uint64_t off, uint32_t size,
		       uint32_t length_size, uint32_t filelen)
{
	if (off + size > filelen) return hevc_set_err("mp4: sample past end of file");
	uint64_t p = off, end = off + size;
	while (p + length_size <= end) {
		uint32_t nl = 0;
		for (uint32_t i = 0; i < length_size; i++)
			nl = (nl << 8) | buf[p + i];
		p += length_size;
		if (nl == 0 || p + nl > end) return hevc_set_err("mp4: truncated/invalid NAL length in sample");
		if (oput_nal(o, buf + p, nl)) return hevc_set_err("mp4: out of memory");
		p += nl;
	}
	if (p != end) return hevc_set_err("mp4: sample did not end on a NAL boundary");
	return 0;
}

int hevc_mp4_to_annexb(const uint8_t *buf, uint32_t len,
		       uint8_t **out, uint32_t *out_len)
{
	scan_t s;
	memset(&s, 0, sizeof(s));
	s.buf = buf;
	s.len = len;
	scan_top(&s, 0, len);

	if (s.bad) return hevc_set_err("mp4: malformed box structure");
	if (s.n_moof) return hevc_set_err("mp4: fragmented (moof) files unsupported — remux with -c copy");
	if (s.n_vide == 0) return hevc_set_err("mp4: no video track found");
	if (s.n_vide > 1) return hevc_set_err("mp4: multiple video tracks — not supported");
	trak_t *t = &s.vtrak;
	if (!t->hvcc_len) return hevc_set_err("mp4: video track is not HEVC (no hvcC)");
	if (!t->stsz_len || !t->stco_len || !t->stsc_len)
		return hevc_set_err("mp4: video track missing sample tables (stsz/stco/stsc)");

	/* hvcC: 22-byte prefix; [21] low 2 bits = lengthSizeMinusOne, [22] = numOfArrays. */
	if (t->hvcc_len < 23) return hevc_set_err("mp4: hvcC record too short");
	const uint8_t *hc = buf + t->hvcc_off;
	uint32_t length_size = (hc[21] & 0x3) + 1;
	uint32_t num_arrays = hc[22];

	/* Parse stsz. */
	if (t->stsz_len < 12) return hevc_set_err("mp4: stsz too short");
	const uint8_t *sz = buf + t->stsz_off;
	uint32_t sample_size = rd32(sz + 4);
	uint32_t sample_count = rd32(sz + 8);
	if (sample_count == 0) return hevc_set_err("mp4: zero samples");
	if (sample_size == 0 && t->stsz_len < 12 + (uint64_t)sample_count * 4)
		return hevc_set_err("mp4: stsz size array overrun");

	/* Parse stco/co64. */
	const uint8_t *co = buf + t->stco_off;
	if (t->stco_len < 8) return hevc_set_err("mp4: stco too short");
	uint32_t chunk_count = rd32(co + 4);
	uint32_t osz = t->stco_is64 ? 8 : 4;
	if (chunk_count == 0 || t->stco_len < 8 + (uint64_t)chunk_count * osz)
		return hevc_set_err("mp4: stco offset array overrun");

	/* Parse stsc runs. */
	const uint8_t *sc = buf + t->stsc_off;
	if (t->stsc_len < 8) return hevc_set_err("mp4: stsc too short");
	uint32_t stsc_n = rd32(sc + 4);
	if (stsc_n == 0 || t->stsc_len < 8 + (uint64_t)stsc_n * 12)
		return hevc_set_err("mp4: stsc run array overrun");

	obuf_t o;
	memset(&o, 0, sizeof(o));

	/* Parameter sets (VPS/SPS/PPS) from hvcC, in record order. */
	uint32_t p = 23;
	for (uint32_t a = 0; a < num_arrays; a++) {
		if (p + 3 > t->hvcc_len) { free(o.d); return hevc_set_err("mp4: hvcC array header overrun"); }
		uint32_t cnt = rd16(hc + p + 1);
		p += 3;
		for (uint32_t n = 0; n < cnt; n++) {
			if (p + 2 > t->hvcc_len) { free(o.d); return hevc_set_err("mp4: hvcC NALU length overrun"); }
			uint32_t nl = rd16(hc + p);
			p += 2;
			if (p + nl > t->hvcc_len) { free(o.d); return hevc_set_err("mp4: hvcC NALU payload overrun"); }
			if (oput_nal(&o, hc + p, nl)) { free(o.d); return hevc_set_err("mp4: out of memory"); }
			p += nl;
		}
	}

	/* Walk samples in decode order via the stsc → chunk → stco mapping, reading
	 * each sample from its true file offset (so interleaved audio is skipped). */
	uint32_t sample = 0;      /* 0-based sample index into stsz */
	for (uint32_t r = 0; r < stsc_n && sample < sample_count; r++) {
		uint32_t first_chunk = rd32(sc + 8 + r * 12);          /* 1-based */
		uint32_t spc = rd32(sc + 8 + r * 12 + 4);              /* samples per chunk */
		uint32_t next_first = (r + 1 < stsc_n) ? rd32(sc + 8 + (r + 1) * 12) : chunk_count + 1;
		if (first_chunk == 0 || first_chunk > next_first || next_first > chunk_count + 1 || spc == 0) {
			free(o.d); return hevc_set_err("mp4: bad stsc run");
		}
		for (uint32_t ch = first_chunk; ch < next_first && sample < sample_count; ch++) {
			uint64_t choff = t->stco_is64 ? rd64(co + 8 + (uint64_t)(ch - 1) * 8)
						      : rd32(co + 8 + (ch - 1) * 4);
			uint64_t soff = choff;
			for (uint32_t k = 0; k < spc && sample < sample_count; k++) {
				uint32_t ssz = sample_size ? sample_size : rd32(sz + 12 + sample * 4);
				if (emit_sample(&o, buf, soff, ssz, length_size, len) < 0) { free(o.d); return -1; }
				soff += ssz;
				sample++;
			}
		}
	}
	if (sample != sample_count) { free(o.d); return hevc_set_err("mp4: stsc did not cover all samples"); }
	if (!o.len) { free(o.d); return hevc_set_err("mp4: no coded NAL units found"); }

	*out = o.d;
	*out_len = o.len;
	return 0;
}
