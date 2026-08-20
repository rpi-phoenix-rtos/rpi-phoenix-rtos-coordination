import numpy as np
np.random.seed(1)
def load(imgf,lblf):
    with open(imgf,'rb') as f: f.read(16); a=np.frombuffer(f.read(),np.uint8)
    with open(lblf,'rb') as f: f.read(8); l=np.frombuffer(f.read(),np.uint8)
    return a.reshape(-1,28,28).astype(np.float32)/255.0, l.astype(np.int64)
Xtr,ytr=load('mnist/train-images-idx3-ubyte','mnist/train-labels-idx1-ubyte')
Xte,yte=load('mnist/t10k-images-idx3-ubyte','mnist/t10k-labels-idx1-ubyte')
NF=8; H=26; P=13; FD=NF*P*P
def im2col(X):  # (n,28,28)->(n,676,9)
    n=X.shape[0]; cols=np.empty((n,H*H,9),np.float32); k=0
    for i in range(3):
        for j in range(3):
            cols[:,:,k]=X[:,i:i+H,j:j+H].reshape(n,-1); k+=1
    return cols
W1=(np.random.randn(9,NF)*0.2).astype(np.float32); b1=np.zeros(NF,np.float32)
W2=(np.random.randn(FD,10)*0.05).astype(np.float32); b2=np.zeros(10,np.float32)
def fwd(X,cache=False):
    n=X.shape[0]; cols=im2col(X)                     # n,676,9
    co=(cols@W1)+b1                                  # n,676,NF
    co=co.reshape(n,H,H,NF); r=np.maximum(co,0)
    rp=r.reshape(n,P,2,P,2,NF); pooled=rp.max(axis=(2,4))   # n,P,P,NF
    feat=pooled.transpose(0,3,1,2).reshape(n,FD)
    z=feat@W2+b2; z-=z.max(1,keepdims=True); e=np.exp(z); p=e/e.sum(1,keepdims=True)
    if not cache: return p
    return p,(cols,co,r,rp,pooled,feat)
lr=0.05; bs=128
for ep in range(6):
    idx=np.random.permutation(len(Xtr))
    for k in range(0,60000,bs):
        bi=idx[k:k+bs]; x=Xtr[bi]; y=ytr[bi]; n=len(bi)
        p,(cols,co,r,rp,pooled,feat)=fwd(x,cache=True)
        dz=p.copy(); dz[np.arange(n),y]-=1; dz/=n                  # n,10
        dW2=feat.T@dz; db2=dz.sum(0); dfeat=dz@W2.T                # n,FD
        dpooled=dfeat.reshape(n,NF,P,P).transpose(0,2,3,1)
        # maxpool backward: route to argmax in each 2x2
        dr=np.zeros_like(r)
        rp2=rp  # n,P,2,P,2,NF
        mask=(rp2==pooled[:,:,None,:,None,:])
        drp=(dpooled[:,:,None,:,None,:]*mask)
        # normalize ties
        cnt=mask.sum(axis=(2,4),keepdims=True); drp/=np.maximum(cnt,1)
        dr=drp.reshape(n,H,H,NF)
        dco=dr*(co>0)                                             # relu bwd
        dco2=dco.reshape(n,H*H,NF)
        dW1=np.einsum('nkc,nkf->cf',cols,dco2); db1=dco2.sum((0,1))
        W2-=lr*dW2; b2-=lr*db2; W1-=lr*dW1; b1-=lr*db1
    acc=(fwd(Xte[:2000]).argmax(1)==yte[:2000]).mean()
    print("ep%d val-acc %.4f"%(ep,acc))
pred=fwd(Xte).argmax(1); print("TEST-ACC %.4f"%(pred==yte).mean())
# export (same header format the C inference reads; conv_w now [NF][3][3] from W1[9,NF])
Wc=W1.T.reshape(NF,3,3)  # NF,3,3
NT=10
def carr(nm,a): fl=a.astype(np.float32).ravel(); return "static const float %s[%d]={%s};\n"%(nm,fl.size,",".join("%.8ff"%v for v in fl))
with open('cnn_data.h','w') as f:
    f.write("/* auto-gen: FULLY-TRAINED CNN (conv+head), MNIST */\n#define NF %d\n#define FEATDIM %d\n#define NTEST %d\n"%(NF,FD,NT))
    f.write(carr("conv_w",Wc)); f.write(carr("conv_b",b1)); f.write(carr("fc_w",W2)); f.write(carr("fc_b",b2))
    im=(Xte[:NT]*255).astype(np.uint8).ravel()
    f.write("static const unsigned char test_img[%d]={%s};\n"%(im.size,",".join(str(int(v)) for v in im)))
    f.write("static const int test_label[%d]={%s};\n"%(NT,",".join(str(int(v)) for v in yte[:NT])))
    f.write("static const int ref_pred[%d]={%s};\n"%(NT,",".join(str(int(v)) for v in pred[:NT])))
print("refpreds",pred[:NT].tolist(),"labels",yte[:NT].tolist())
