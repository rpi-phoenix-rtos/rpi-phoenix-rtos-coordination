import re,glob,os,sys,collections
base='/home/houp/phoenix-rpi/artifacts/'
files=sorted(glob.glob(base+'rpi4b-uart/rpi4b-uart-20260930-16*-gate-*.log'))+sorted(glob.glob(base+'rpi4b-uart/rpi4b-uart-20260930-155*.log'))+glob.glob(base+'rpi4b-uart/rpi4b-uart-20260930-11*-gate2A.log')+[base+'hdmi-video/%s.cyclog'%n for n in 'g5-verify w1-qs20-stk xfce-apps xfce-video x11 dillo'.split()]
pat=re.compile(r'(error|warn|critical|fail|not implemented|not supported|unsupported|deprecat|could not|couldn\'t|cannot|can\'t|unable|unknown|missing|no such|not found|invalid|denied|abort|fault|exception|assert|timeout|timed out|refused|ignor|fatal|panic|lost|not available|stub|bad |illegal|mismatch|overflow|oops)',re.I)
def norm(s):
    s=re.sub(r'\x1b\[[0-9;?]*[A-Za-z]','',s)
    s=re.sub(r'[^\x20-\x7e]','',s)
    s=re.sub(r'^\s*\[\s*[\d.]+\]\s*','',s)
    s=re.sub(r'0x[0-9a-fA-F]+','0xN',s)
    s=re.sub(r'\b\d+(\.\d+)*\b','N',s)
    s=re.sub(r'\s+',' ',s).strip()
    return s
cls=collections.OrderedDict()
for f in files:
    tag=os.path.basename(f).replace('rpi4b-uart-20260930-','')
    for line in open(f,'rb').read().decode('latin1').splitlines():
        l=re.sub(r'\x1b\[[0-9;?]*[A-Za-z]','',line)
        if not pat.search(l): continue
        k=norm(l)
        if k not in cls: cls[k]=[0,collections.Counter(),l.strip()[:300]]
        cls[k][0]+=1; cls[k][1][tag]+=1
for k,(n,c,ex) in sorted(cls.items(),key=lambda x:-x[1][0]):
    print(f"{n:4d} [{len(c)} logs] {k[:260]}")
    print(f"       logs: {','.join(sorted(c))[:200]}")
