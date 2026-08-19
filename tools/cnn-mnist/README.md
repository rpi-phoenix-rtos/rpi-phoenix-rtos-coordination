# CNN MNIST inference on Phoenix-RTOS / Pi 4

A self-contained convolutional-net digit classifier running on Phoenix-RTOS —
the first step of the owner's "push ML toward CNN/GPU (not LLM)" direction.

**Net:** 1×28×28 → fixed 3×3 conv (8 ch) → ReLU → 2×2 maxpool → flatten(1352)
→ trained linear head (→10) → argmax. Fixed random conv features + a trained
linear softmax head reach **95.1%** MNIST test accuracy (training the linear
head is small/robust; conv is deterministic).

**Files:**
- `cnn.c` — pure-C inference (no external deps), weights+test data in `cnn_data.h`.
- `cnn_train_export.py` — numpy trainer/exporter (regenerates `cnn_data.h`):
  loads MNIST, computes conv features, trains the linear head, emits the C header
  with the conv/fc weights + 10 test digits + reference predictions.
- `cnn_data.h` — generated: weights + embedded test digits + numpy reference preds.

**Build/run:**
    aarch64-phoenix-gcc -O2 cnn.c -o cnn      # cross-compile
    # stage into the netboot NFS root, run on the Pi:
    /bin/cnn        # => 'img N: pred=.. ref=.. MATCH' x10, 'CNN-OK'

**HW-verified (Pi 4, netboot):** all 10 predictions match the numpy reference
**bit-exact** (incl. a digit the model itself misclassifies — reproduced exactly),
9/10 correct, 0 faults. Proves CNN inference compute (conv/relu/maxpool/dense) is
correct on Phoenix CPU.

**Next (owner's real target):** V3D **GPU** acceleration of the conv/matmul (cf.
the llama2 phase-2 V3D-matmul design) — the compute-heavy path a real/larger CNN
needs.
