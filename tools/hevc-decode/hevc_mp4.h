/* hevc_mp4.h — minimal ISOBMFF (MP4/MOV) → Annex-B HEVC demux for hevc-play.
 *
 * Lets the runtime player consume a real `.mp4`/`.mov` file (HEVC video) and
 * feed the resulting Annex-B elementary stream to the existing NAL pipeline —
 * no ffmpeg / libavformat dependency.
 *
 * Scope (deliberately narrow, "reject don't mis-handle"): a NON-fragmented file
 * carrying exactly one HEVC (`hev1`/`hvc1`) video track. Other tracks (e.g. an
 * audio track) are fine — the video track's samples are read by their true file
 * offsets via the sample tables, so interleaved audio is skipped. Files with no
 * video track, more than one video track, or fragmentation (`moof`) are rejected
 * loudly via hevc_err() rather than silently mis-demuxed.
 *
 * The video track's coded samples are located via its stsc → chunk → stco/co64
 * mapping (sizes from stsz); each sample is a run of length-prefixed NALs, which
 * are emitted as Annex-B. The VPS/SPS/PPS parameter sets come from `hvcC`.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef HEVC_MP4_H
#define HEVC_MP4_H

#include <stdint.h>

/* 1 if `buf` looks like ISOBMFF (an `ftyp` box at offset 0), else 0. */
int hevc_mp4_detect(const uint8_t *buf, uint32_t len);

/* Demux an in-memory MP4 into a freshly malloc'd Annex-B elementary stream.
 * On success returns 0 and sets *out (caller must free()) + *out_len. On reject
 * returns -1; hevc_err() explains why. */
int hevc_mp4_to_annexb(const uint8_t *buf, uint32_t len,
                       uint8_t **out, uint32_t *out_len);

#endif /* HEVC_MP4_H */
