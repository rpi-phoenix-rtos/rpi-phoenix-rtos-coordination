# Use Linux/Pi4 as the reference implementation for #67 (owner-proposed, 2026-09-03)

Owner: *"maybe the long chased torches bug in vkQuake is actually a bug on Linux
as well? … If yes - we can simply document this as a known GPU issue on Pi4 and
stop working on this. If no - we can try to debug, analyze what Linux does
differently."*

This is the strongest untried angle. Every other line of attack has compared
Phoenix against itself; this compares it against a working implementation of the
*same* Mesa, the *same* Vulkan and the *same* engine.

## Why it is cheap and NOT owner-blocked

`artifacts/linux-netboot/` already holds a Raspberry Pi OS Lite arm64 NFS-root
netboot image (see `feedback_selfflash_sd_via_netboot_linux`). Linux on the Pi4
needs no card and no manual step, so this can run unattended.

## Prior from the literature (checked 2026-09-03)

Igalia's V3DV dev log documents vkQuake 1/2/3 working on the Pi 4, and a web
search finds **no** report of missing alias/flame models on V3DV. So the base
rate of "V3DV renders vkQuake correctly on Linux" is high, which makes outcome 2
or 3 below more likely than outcome 1. Not a conclusion — a prior.

## What is shared and what is not

| layer | Linux/Pi4 | Phoenix/Pi4 |
|---|---|---|
| engine (vkQuake) | same fork, same shareware pak | same |
| Vulkan driver (V3DV) | same Mesa source | same Mesa source |
| **kernel driver** | **real `drivers/gpu/drm/v3d`**: DRM scheduler, fences, MMU, reset | **our `v3d_phoenix_winsys.c`**: poll-wait submit, hand-rolled reset, `drmSyncobjWait` stubbed to "signalled" |
| WSI/present | DRM/KMS | no-WSI blit to `/dev/fb0` |

So the layer we replaced is exactly the layer the current evidence implicates
(a wedged one-shot upload job dropped and reported as success).

## Three outcomes, not two

1. **Reproduces on Linux** → shared Mesa/V3DV or silicon defect. Do what the
   owner says: document as a known Pi4 GPU issue, stop, and consider an upstream
   bug report with the CLIF dump attached.
2. **Does not reproduce, and `dmesg` shows NO v3d job timeouts** → the wedge
   itself is ours. Diff our submit sequence against `v3d_submit_cl_ioctl` /
   `v3d_sched`: cache maintenance (`v3d_invalidate_caches`), bin→render
   handoff, what is waited on, MMU flush placement.
3. **Does not reproduce, but `dmesg` DOES show `v3d_sched: job timed out`** →
   the marginal wedge is *shared*, and the difference is that Linux **reports and
   recovers** while we silently drop. Then the Phoenix bug is error propagation,
   not the wedge — which is exactly what the measurements so far suggest (a
   *valid* CL wedging, `drmSyncobjWait` always returning signalled).

Outcome 3 is the most likely given current evidence and is the one that would
never be found without a control implementation.

## Steps, cheapest first

1. **Netboot Linux, confirm V3DV is live**: `vulkaninfo | grep -i v3d`,
   `glxinfo`/`vainfo` not needed. Record Mesa version — pin the comparison to a
   Mesa close to `external/mesa`, or note the delta.
2. **Run the same test**: build vkQuake from our fork *without* the Phoenix glue
   (upstream build), same shareware `id1/`, `+map start`. Grade the frame with
   **`scripts/check-torch-rois.py`** — the reference frame is a host quakespasm
   render, so the existing ROIs and viewpoint check apply unchanged. This makes
   the Linux result directly comparable to our 0/8 and 1/8 numbers rather than a
   subjective look. Run n>=8; #67 is intermittent at ~10-20%.
3. **Capture `dmesg`** across those runs: `v3d_sched: job timed out`, MMU
   faults, resets. This alone splits outcome 2 from 3.
4. **Dump the control lists on Linux**: `V3D_DEBUG=cl` and `V3D_DEBUG=clif`
   (`external/mesa/src/broadcom/common/v3d_debug.c:43,47`). CLIF output is
   replayable and human-readable — it gives **ground truth for what the ~98-byte
   meta-copy RCL should contain**, which is precisely the open question on
   Phoenix (one wedged upload had a valid-looking CL, another read back as image
   data).
5. **Make the two dumps diffable**: `v3d_debug` is **not** currently compiled
   into our archives (`nm` on `libv3dv-phoenix.a`: 0 matches). Adding it lets
   both platforms emit the *same* dumper's output for the same draw, so the
   comparison is a diff rather than an interpretation. Do this only if step 4
   shows the dumps are worth aligning.
6. **If the CLs match byte-for-byte**, the defect is in submit/execution, not
   content → compare register programming and wait/fence sequencing against
   `v3d_sched.c` (note: Linux declares a job dead after **500 ms**,
   `v3d_sched.c:859`).

## Guardrails

- Do not conflate Mesa versions: if the distro Mesa differs materially from
  `external/mesa`, say so in the write-up; a behaviour difference across Mesa
  versions is a different finding from a Phoenix-vs-Linux difference.
- Keep the Pi-lock discipline: Linux netboot and Phoenix netboot both use the
  single UART/TFTP rig, one at a time.
- The torch verdict must come from `--rate` over >=8 boots, never a screenshot.
  This bug has five false closures on single frames.
