import re,glob,os,collections
base='/home/houp/phoenix-rpi/artifacts/'
files=sorted(glob.glob(base+'rpi4b-uart/rpi4b-uart-20260930-16*-gate-*.log'))+sorted(glob.glob(base+'rpi4b-uart/rpi4b-uart-20260930-155*.log'))+glob.glob(base+'rpi4b-uart/rpi4b-uart-20260930-11*-gate2A.log')+[base+'hdmi-video/%s.cyclog'%n for n in 'g5-verify w1-qs20-stk xfce-apps xfce-video x11 dillo'.split()]
def clean(l):
    l=re.sub(r'\x1b\[[0-9;?]*[A-Za-z]','',l); l=re.sub(r'\x1b\[[0-9;]*$','',l)
    return re.sub(r'[^\x20-\x7e\t]','',l).rstrip()
def norm(s):
    s=re.sub(r'^\s*\[\s*[\d.]+\]\s*','',s)
    s=re.sub(r'\d\d:\d\d:\d\d(\.\d+)?','T',s)
    s=re.sub(r'0x[0-9a-fA-F]+','0xN',s)
    s=re.sub(r'\b[0-9a-f]{8,}\b','H',s)
    s=re.sub(r'\d+','N',s)
    s=re.sub(r'\s+',' ',s).strip()
    return s
def emitter(s):
    m=re.match(r'XFCE log ([^:]+): (.*)',s)
    pre=''
    if m: pre='XFCE log '+m.group(1)+' | '; s=m.group(2)
    for r in [r'\(([\w.-]+):N\): ([\w-]+)-(CRITICAL|WARNING|Message|INFO|DEBUG)',r'^([\w-]+)-(CRITICAL|WARNING|Message|INFO|DEBUG)',r'^\*\* \(([\w.-]+)',r'^\[(error|warn|info|verbose|debug)\s*\] ([\w ]+?):',r'^\[(ERROR|INFO|DEBUG)\]',r'^\((WW|EE|II|==|--|\*\*|NI)\)',r'^([A-Za-z][\w@./-]{1,30}):']:
        mm=re.search(r,s)
        if mm: return pre+'/'.join(g for g in mm.groups() if g)
    return pre+'?'
cls={}
for f in files:
    tag=os.path.basename(f).replace('rpi4b-uart-20260930-','').replace('.log','').replace('.cyclog','@cyc')
    for line in open(f,'rb').read().decode('latin1').split('\n'):
        l=clean(line)
        if not l.strip(): continue
        k=norm(l)
        e=emitter(k)
        if k not in cls: cls[k]=[e,0,collections.Counter(),l.strip()[:240]]
        cls[k][1]+=1; cls[k][2][tag]+=1
print(len(cls))
byE=collections.defaultdict(list)
for k,v in cls.items(): byE[v[0]].append((k,v))
with open('/tmp/logscan/all.txt','w') as o:
    for e in sorted(byE,key=lambda e:-sum(v[1] for k,v in byE[e])):
        tot=sum(v[1] for k,v in byE[e])
        o.write(f"=== {e}  (classes={len(byE[e])} lines={tot})\n")
        for k,v in sorted(byE[e],key=lambda x:-x[1][1]):
            o.write(f"  {v[1]:4d} L{len(v[2])} {k[:200]}\n")
