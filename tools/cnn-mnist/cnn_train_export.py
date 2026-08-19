import numpy as np, struct
np.random.seed(1)
def load(imgf, lblf):
    with open(imgf,'rb') as f:
        f.read(16); a=np.frombuffer(f.read(),dtype=np.uint8)
    with open(lblf,'rb') as f:
        f.read(8); l=np.frombuffer(f.read(),dtype=np.uint8)
    return a.reshape(-1,28,28).astype(np.float32)/255.0, l.astype(np.int64)
Xtr,ytr = load('mnist/train-images-idx3-ubyte','mnist/train-labels-idx1-ubyte')
Xte,yte = load('mnist/t10k-images-idx3-ubyte','mnist/t10k-labels-idx1-ubyte')
NF=8
W1 = (np.random.randn(NF,3,3).astype(np.float32))*0.5   # fixed random conv filters
b1 = np.zeros(NF,dtype=np.float32)
def features(X):  # conv(3x3 valid) -> relu -> maxpool 2x2 -> flatten
    n=X.shape[0]; H=26; 
    out=np.zeros((n,NF,26,26),dtype=np.float32)
    for f in range(NF):
        for i in range(3):
            for j in range(3):
                out[:,f]+=X[:,i:i+26,j:j+26]*W1[f,i,j]
        out[:,f]+=b1[f]
    out=np.maximum(out,0)
    # maxpool 2x2 -> 13x13
    p=out.reshape(n,NF,13,2,13,2).max(axis=(3,5))
    return p.reshape(n,-1)   # n x (NF*169=1352)
Ftr=features(Xtr); Fte=features(Xte)
D=Ftr.shape[1]
# train linear softmax head
W2=np.zeros((D,10),dtype=np.float32); b2=np.zeros(10,dtype=np.float32)
lr=0.1; bs=128; nep=8
for ep in range(nep):
    idx=np.random.permutation(len(Ftr))
    for k in range(0,len(idx),bs):
        bi=idx[k:k+bs]; x=Ftr[bi]; y=ytr[bi]
        z=x@W2+b2; z-=z.max(1,keepdims=True); e=np.exp(z); p=e/e.sum(1,keepdims=True)
        p[np.arange(len(bi)),y]-=1; p/=len(bi)
        W2-=lr*(x.T@p); b2-=lr*p.sum(0)
zt=Fte@W2+b2; pred=zt.argmax(1); acc=(pred==yte).mean()
print("TEST-ACC %.4f"%acc)
# export: conv W1/b1, fc W2/b2, and NTEST test images + labels + ref preds
NT=10
sel=np.arange(NT)
def carr(name,arr):
    flat=arr.astype(np.float32).ravel()
    return "static const float %s[%d]={%s};\n"%(name,flat.size,",".join("%.8ff"%v for v in flat))
with open('cnn_data.h','w') as f:
    f.write("/* auto-generated: fixed-random-conv CNN + trained linear head, MNIST */\n")
    f.write("#define NF %d\n#define FEATDIM %d\n#define NTEST %d\n"%(NF,D,NT))
    f.write(carr("conv_w",W1)); f.write(carr("conv_b",b1))
    f.write(carr("fc_w",W2)); f.write(carr("fc_b",b2))
    imgs=(Xte[sel]*255).astype(np.uint8).ravel()
    f.write("static const unsigned char test_img[%d]={%s};\n"%(imgs.size,",".join(str(int(v)) for v in imgs)))
    f.write("static const int test_label[%d]={%s};\n"%(NT,",".join(str(int(v)) for v in yte[sel])))
    f.write("static const int ref_pred[%d]={%s};\n"%(NT,",".join(str(int(v)) for v in pred[sel])))
print("wrote cnn_data.h; sample refpreds", pred[sel].tolist(), "labels", yte[sel].tolist())
