#!/usr/bin/env python3
# Generate a minimal IPB header (one bidirectional B-frame) for the rpivid HW
# decoder B-frame bring-up. Takes the first 3 coded frames of an IBP stream in
# DECODE order — I(POC0), P(POC2), B(POC1) — and emits their slice payloads +
# per-frame params + POC-matched golden luma. The B references the past anchor
# (POC0, L0) and the future anchor (POC2, L1); with temporal-MVP off the HW
# phase-2 setup is identical to P (see the B-frame plan).
#
# Usage: gen-ipb-header.py <in.265> <W> <H> <GUARD> > out.h
# SPDX-License-Identifier: BSD-3-Clause
import sys, subprocess, re
f265, W, H, guard = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
d = open(f265, 'rb').read()

# NAL boundaries (4-byte start code checked first).
starts = []; i = 0
while i < len(d) - 3:
    if d[i:i+4] == b'\x00\x00\x00\x01': sc = 4
    elif d[i:i+3] == b'\x00\x00\x01': sc = 3
    else: i += 1; continue
    s = i + sc; starts.append(s); i = s
nal_bytes = {}
for k, s in enumerate(starts):
    if k+1 < len(starts):
        ns = starts[k+1]; end = (ns - 3) if d[ns-4] != 0 else (ns - 4)
    else: end = len(d)
    nal_bytes[s] = d[s:end]

# Slice params from trace_headers, in decode order.
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

frames, seen = [], set()
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
    stype = int(st.group(1))                 # 0=B, 1=P, 2=I
    poc = int(poc_m.group(1)) if poc_m else 0
    key = (stype, poc, qd)
    if key in seen: continue
    seen.add(key)
    frames.append({'stype': stype, 'poc': poc, 'qp': 26 + qd, 'dbo': dbo})

slice_nals = [nal_bytes[s] for s in starts if ((d[s] >> 1) & 0x3f) in (0, 1, 19, 20)]
assert len(slice_nals) == len(frames), (len(slice_nals), len(frames))
for fr, nb in zip(frames, slice_nals):
    fr['nal'] = nb
    fr['bfnum'] = len(nb) - fr['dbo']

# Keep only the first I, first P, first B (decode order I,P,B for a bframes=1 GOP).
want = frames[:3]
assert [f['stype'] for f in want] == [2, 1, 0], [f['stype'] for f in want]
i_poc, p_poc, b_poc = want[0]['poc'], want[1]['poc'], want[2]['poc']
# ref POCs: P L0 = the I anchor; B L0 = past anchor (I), L1 = future anchor (P).
want[1]['l0'] = i_poc; want[1]['l1'] = 0
want[2]['l0'] = i_poc; want[2]['l1'] = p_poc

# Golden luma per POC (ffmpeg decodes to DISPLAY order; POC==display index here).
ref_path = f265 + '.nv12'
subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-i',f265,'-f','rawvideo',
                '-pix_fmt','nv12','-y',ref_path], check=True)
ref = open(ref_path, 'rb').read()
fsz = W*H + W*H//2
def golden_y(poc): return ref[poc*fsz : poc*fsz + W*H]

blob = b''.join(fr['nal'] for fr in want)
off = 0
for fr in want:
    fr['off'] = off; off += len(fr['nal'])

ctb_w, ctb_h = (W+63)//64, (H+63)//64
print("/* %s — minimal IPB (1 bidirectional B). SPDX: BSD-3-Clause */" % guard.lower())
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
print("#define FRAME_DATA_BYTE_OFFSET %du" % want[0]['dbo'])
print("#define FRAME_DATA_LEN %du" % want[0]['bfnum'])
print("#define FRAME_SLICE_QP %d" % want[0]['qp'])
print("#define FRAME_EXPECT_Y 0u\n#define FRAME_EXPECT_C 0u")
print("#define EXPECTED_Y(x,y) 0\n#define EXPECTED_C(x,y) 0")
print("#define IPB_TEST 1")
print("struct ipb_frame { unsigned off, dbo, bfnum, stype, poc, l0_poc, l1_poc; int qp; };")
print("static const struct ipb_frame ipb_frames[3] = {")
for fr in want:
    print("\t{%du,%du,%du,%du,%du,%du,%du,%d}," % (fr['off'], fr['dbo'], fr['bfnum'], fr['stype'],
          fr['poc'], fr.get('l0', 0), fr.get('l1', 0), fr['qp']))
print("};")
def carr(name, b):
    o = ["static const unsigned char %s[%d] = {" % (name, len(b))]
    for j in range(0, len(b), 16):
        o.append("\t" + ",".join("0x%02x" % x for x in b[j:j+16]) + ",")
    o.append("};"); return "\n".join(o)
print(carr("clip_blob", blob))
# Golden in DECODE order (frame f's output compared to golden[its POC]).
allY = b''.join(golden_y(fr['poc']) for fr in want)
print(carr("ipb_golden_y", allY))
print("#endif")
