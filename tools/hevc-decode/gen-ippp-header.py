#!/usr/bin/env python3
# Generate a multi-frame IPPP header (rolling-DPB inter sequence) from an
# all-P HEVC stream: frame 0 = IDR, frames 1..N-1 = P, each referencing the
# immediately previous decoded frame (POC N -> ref POC N-1). Robust to per-frame
# slice-header length (data_byte_offset derived from the trace's last slice-header
# element). Non-weighted P (weightp=0) => 5 slice messages per P.
#
# Usage: gen-ippp-header.py <in.265> <W> <H> <GUARD> [nogolden] > out.h
#   nogolden: skip the embedded per-frame golden luma (for long playback clips).
# SPDX-License-Identifier: BSD-3-Clause
import sys, subprocess, re
f265, W, H, guard = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
want_golden = not (len(sys.argv) > 5 and sys.argv[5] == 'nogolden')
d = open(f265, 'rb').read()

# NAL boundaries (check 4-byte start code first to avoid double-count).
starts = []; i = 0
while i < len(d) - 3:
    if d[i:i+4] == b'\x00\x00\x00\x01': sc = 4
    elif d[i:i+3] == b'\x00\x00\x01': sc = 3
    else: i += 1; continue
    s = i + sc; starts.append(s); i = s
nal_bytes = {}   # payload-start-offset -> stripped NAL bytes
for k, s in enumerate(starts):
    if k+1 < len(starts):
        ns = starts[k+1]; end = (ns - 3) if d[ns-4] != 0 else (ns - 4)
    else: end = len(d)
    nal_bytes[s] = d[s:end]

# Per-slice params from trace_headers (in decode order).
tr = subprocess.run(['ffmpeg','-hide_banner','-v','trace','-i',f265,'-c','copy',
                     '-bsf:v','trace_headers','-f','null','-'],capture_output=True,text=True).stderr
groups, cur = [], None
for ln in tr.splitlines():
    if re.search(r'nal_unit_type: \d+\(', ln):
        if cur: groups.append(cur)
        cur = [ln]
    elif cur is not None:
        cur.append(ln)
if cur: groups.append(cur)

frames = []                 # {nal, is_p, poc, qp, dbo, bfnum}
seen = set()
for g in groups:
    txt = '\n'.join(g)
    st = re.search(r'slice_type\s+[01]+ = (\d)', txt)
    if not st: continue
    lf = re.search(r'(\d+)\s+slice_loop_filter_across_slices_enabled_flag', txt)
    if not lf: continue
    E = int(lf.group(1)) + 1
    dbo = (E + (8 if E % 8 == 0 else 8 - E % 8)) // 8
    poc_m = re.search(r'slice_pic_order_cnt_lsb\s+[01]+ = (\d+)', txt)
    qd = int(re.search(r'slice_qp_delta\s+[01]+ = (-?\d+)', txt).group(1))
    is_p = (st.group(1) == '1')
    poc = int(poc_m.group(1)) if poc_m else 0
    # associate to the NAL in decode order (first unseen slice NAL)
    key = (is_p, poc, qd)
    if key in seen: continue     # skip trace's 2nd (extract_extradata) pass duplicates
    seen.add(key)
    frames.append({'is_p': is_p, 'poc': poc, 'qp': 26 + qd, 'dbo': dbo})

# Match frames to NAL byte blobs in decode order (slice NALs = type 1/19/20).
slice_nals = [nal_bytes[s] for s in starts if ((d[s] >> 1) & 0x3f) in (1, 19, 20)]
assert len(slice_nals) == len(frames), (len(slice_nals), len(frames))
for fr, nb in zip(frames, slice_nals):
    fr['nal'] = nb
    fr['bfnum'] = len(nb) - fr['dbo']

# Golden NV12 luma per frame.
ref_path = f265 + '.nv12'
subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-i',f265,'-f','rawvideo',
                '-pix_fmt','nv12','-y',ref_path], check=True)
ref = open(ref_path, 'rb').read()
fsz = W*H + W*H//2

blob = b''.join(fr['nal'] for fr in frames)
off = 0
for fr in frames:
    fr['off'] = off; off += len(fr['nal'])

def carr(name, b, t="unsigned char", fmt="0x%02x", per=16):
    o = ["static const %s %s[%d] = {" % (t, name, len(b))]
    for j in range(0, len(b), per):
        o.append("\t" + ",".join(fmt % x for x in b[j:j+per]) + ",")
    o.append("};"); return "\n".join(o)

ctb_w, ctb_h = (W+63)//64, (H+63)//64
print("/* %s — %d-frame IPPP inter sequence (rolling DPB). SPDX: BSD-3-Clause */" % (guard.lower(), len(frames)))
print("#ifndef %s\n#define %s" % (guard, guard))
for n,v in [("FRAME_WIDTH",W),("FRAME_HEIGHT",H),("FRAME_LOG2_CTB",6),("FRAME_CTB_WIDTH",ctb_w),
            ("FRAME_CTB_HEIGHT",ctb_h),("FRAME_LOG2_MIN_CB",3),("FRAME_LOG2_MIN_TB",2),("FRAME_LOG2_MAX_TB",5),
            ("FRAME_MAX_TRAFO_INTRA",0),("FRAME_MAX_TRAFO_INTER",0),("FRAME_CHROMA_FORMAT_IDC",1),
            ("FRAME_STRONG_INTRA_SMOOTH",1),("FRAME_BIT_DEPTH_LUMA_MINUS8",0),("FRAME_DIFF_CU_QP_DELTA_DEPTH",1),
            ("FRAME_CU_QP_DELTA_ENABLED",1),("FRAME_TRANSFORM_SKIP",0),("FRAME_SIGN_DATA_HIDING",1),
            ("FRAME_CONSTRAINED_INTRA_PRED",0),("FRAME_SLICE_TYPE",2)]:
    print("#define %s %du" % (n, v))
for n in ("FRAME_PPS_CB_QP_OFFSET","FRAME_PPS_CR_QP_OFFSET","FRAME_SLICE_CB_QP_OFFSET","FRAME_SLICE_CR_QP_OFFSET"):
    print("#define %s 0" % n)
print("#define FRAME_CONFIG2 0x25888u")
print("#define FRAME_DATA_BYTE_OFFSET %du" % frames[0]['dbo'])   # frame0 (single-path compat)
print("#define FRAME_DATA_LEN %du" % frames[0]['bfnum'])
print("#define FRAME_SLICE_QP %d" % frames[0]['qp'])
print("#define FRAME_EXPECT_Y 0u\n#define FRAME_EXPECT_C 0u")
print("#define EXPECTED_Y(x,y) 0\n#define EXPECTED_C(x,y) 0")
print("#define IPPP_TEST 1")
print("#define IPPP_NFRAMES %d" % len(frames))
print("struct ippp_frame { unsigned off, dbo, bfnum, is_p, poc, ref_poc; int qp; };")
print("static const struct ippp_frame ippp_frames[%d] = {" % len(frames))
for fr in frames:
    print("\t{%du,%du,%du,%du,%du,%du,%d}," % (fr['off'], fr['dbo'], fr['bfnum'], 1 if fr['is_p'] else 0,
                                               fr['poc'], (fr['poc']-1) if fr['is_p'] else 0, fr['qp']))
print("};")
print(carr("clip_blob", blob))
if want_golden:
    print("#define IPPP_HAVE_GOLDEN 1")
    allY = b''.join(ref[fi*fsz : fi*fsz + W*H] for fi in range(len(frames)))
    print("static const unsigned char ippp_golden_y[%d] = {" % len(allY))
    for j in range(0, len(allY), 20):
        print("\t" + ",".join("0x%02x" % x for x in allY[j:j+20]) + ",")
    print("};")
print("#endif")
