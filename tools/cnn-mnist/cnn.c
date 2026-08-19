/* Self-contained CNN inference for MNIST on Phoenix-RTOS.
 * fixed random 3x3 conv (8 ch) -> relu -> 2x2 maxpool -> flatten -> linear(10) -> argmax.
 * Weights + test digits + reference predictions embedded (cnn_data.h). */
#include <stdio.h>
#include "cnn_data.h"
int main(void)
{
	int t, f, pr, pc, dr, dc, i, j, k, d, mismatch = 0, correct = 0;
	for (t = 0; t < NTEST; t++) {
		const unsigned char *img = test_img + t * 28 * 28;
		float feat[FEATDIM];
		for (f = 0; f < NF; f++) {
			for (pr = 0; pr < 13; pr++) {
				for (pc = 0; pc < 13; pc++) {
					float mx = -1e30f;
					for (dr = 0; dr < 2; dr++) {
						for (dc = 0; dc < 2; dc++) {
							int orr = pr * 2 + dr, occ = pc * 2 + dc;
							float s = conv_b[f];
							for (i = 0; i < 3; i++)
								for (j = 0; j < 3; j++)
									s += (img[(orr + i) * 28 + (occ + j)] / 255.0f) * conv_w[f * 9 + i * 3 + j];
							if (s < 0.0f) s = 0.0f; /* relu */
							if (s > mx) mx = s;
						}
					}
					feat[f * 169 + pr * 13 + pc] = mx;
				}
			}
		}
		float z[10];
		for (k = 0; k < 10; k++) {
			float s = fc_b[k];
			for (d = 0; d < FEATDIM; d++) s += feat[d] * fc_w[d * 10 + k];
			z[k] = s;
		}
		int am = 0;
		for (k = 1; k < 10; k++) if (z[k] > z[am]) am = k;
		printf("img %d: pred=%d ref=%d label=%d %s\n", t, am, ref_pred[t], test_label[t], am == ref_pred[t] ? "MATCH" : "MISMATCH");
		if (am != ref_pred[t]) mismatch++;
		if (am == test_label[t]) correct++;
	}
	printf("ACCURACY %d/%d ; ref-mismatch %d\n", correct, NTEST, mismatch);
	printf(mismatch == 0 ? "CNN-OK\n" : "CNN-FAIL\n");
	return mismatch == 0 ? 0 : 1;
}
