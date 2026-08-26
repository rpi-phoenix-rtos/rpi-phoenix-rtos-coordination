#!/usr/bin/env python3
# mlp_train_export.py — train a small MNIST MLP (784->256->256->10, ReLU) in numpy
# and export weights + test digits + reference predictions to mlp_data.h, for the
# GPU-accelerated inference demo (mlp_gpu.c). The 256-wide hidden layers land in the
# V3D CSD-matmul GPU sweet spot (D=256, N in {784,256}); the last layer is dispatched
# at D=256 with rows 10..255 zero-padded (see mlp_gpu.c).
#
# Weight layout matches the GPU kernel o[i]=sum_j W[i*N+j]*x[j]: W is (out x in)
# row-major, stride N=in. Run: .venv/bin/python mlp_train_export.py
import numpy as np
np.random.seed(1)

def load(imgf, lblf):
    with open(imgf, 'rb') as f: f.read(16); a = np.frombuffer(f.read(), np.uint8)
    with open(lblf, 'rb') as f: f.read(8);  l = np.frombuffer(f.read(), np.uint8)
    return a.reshape(-1, 784).astype(np.float32) / 255.0, l.astype(np.int64)

Xtr, ytr = load('mnist/train-images-idx3-ubyte', 'mnist/train-labels-idx1-ubyte')
Xte, yte = load('mnist/t10k-images-idx3-ubyte',  'mnist/t10k-labels-idx1-ubyte')

H = 256
# He-ish init.
W1 = (np.random.randn(784, H) * np.sqrt(2.0 / 784)).astype(np.float32); b1 = np.zeros(H, np.float32)
W2 = (np.random.randn(H, H)  * np.sqrt(2.0 / H)).astype(np.float32);    b2 = np.zeros(H, np.float32)
W3 = (np.random.randn(H, 10) * np.sqrt(2.0 / H)).astype(np.float32);    b3 = np.zeros(10, np.float32)

def fwd(X, cache=False):
    z1 = X @ W1 + b1;  a1 = np.maximum(z1, 0)
    z2 = a1 @ W2 + b2; a2 = np.maximum(z2, 0)
    z3 = a2 @ W3 + b3
    z3 = z3 - z3.max(1, keepdims=True); e = np.exp(z3); p = e / e.sum(1, keepdims=True)
    return (p, (z1, a1, z2, a2)) if cache else p

lr, bs = 0.1, 128
for ep in range(8):
    idx = np.random.permutation(len(Xtr))
    for k in range(0, len(Xtr), bs):
        bi = idx[k:k + bs]; x = Xtr[bi]; y = ytr[bi]; n = len(bi)
        p, (z1, a1, z2, a2) = fwd(x, cache=True)
        dz3 = p.copy(); dz3[np.arange(n), y] -= 1; dz3 /= n
        dW3 = a2.T @ dz3; db3 = dz3.sum(0)
        da2 = dz3 @ W3.T; dz2 = da2 * (z2 > 0)
        dW2 = a1.T @ dz2; db2 = dz2.sum(0)
        da1 = dz2 @ W2.T; dz1 = da1 * (z1 > 0)
        dW1 = x.T @ dz1;  db1 = dz1.sum(0)
        for W, dW in ((W1, dW1), (W2, dW2), (W3, dW3), (b1, db1), (b2, db2), (b3, db3)):
            W -= lr * dW
    acc = (fwd(Xte).argmax(1) == yte).mean()
    print(f"epoch {ep}: test acc {acc:.4f}")

# Pick NTEST deterministic test digits + numpy reference predictions.
NTEST = 12
xt = Xte[:NTEST]; ref = fwd(xt).argmax(1)

def emit(f, name, arr):
    a = np.asarray(arr, np.float32).ravel()
    f.write(f"static const float {name}[{a.size}]={{")
    f.write(",".join(f"{v:.8f}f" for v in a))
    f.write("};\n")

with open('mlp_data.h', 'w') as f:
    f.write("/* auto-gen by mlp_train_export.py: MNIST MLP 784->256->256->10 (ReLU) */\n")
    f.write(f"#define IN 784\n#define H 256\n#define NCLS 10\n#define NTEST {NTEST}\n")
    # W stored (out x in) row-major to match GPU kernel o[i]=sum_j W[i*IN+j]*x[j].
    emit(f, "mlp_w1", W1.T)   # (H, 784)
    emit(f, "mlp_b1", b1)
    emit(f, "mlp_w2", W2.T)   # (H, H)
    emit(f, "mlp_b2", b2)
    emit(f, "mlp_w3", W3.T)   # (10, H)
    emit(f, "mlp_b3", b3)
    emit(f, "mlp_test", xt)   # (NTEST, 784)
    f.write(f"static const int mlp_ref[{NTEST}]={{" + ",".join(str(int(v)) for v in ref) + "};\n")
    f.write(f"static const int mlp_lbl[{NTEST}]={{" + ",".join(str(int(v)) for v in yte[:NTEST]) + "};\n")
print("wrote mlp_data.h; refs:", ref.tolist(), "labels:", yte[:NTEST].tolist())
