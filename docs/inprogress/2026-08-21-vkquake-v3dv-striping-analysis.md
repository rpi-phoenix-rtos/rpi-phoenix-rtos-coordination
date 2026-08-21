# vkQuake V3DV striping — root-cause analysis (2026-08-21, source, subagent)

**Bug:** vkQuake (Vulkan Quake1 on the Phoenix V3DV / Mesa broadcom-vulkan driver, V3D 4.2)
renders with full-frame horizontal STRIPING of the scene content. quake2/quake3 (gallium GL)
render correctly. This is a SEPARATE bug from the just-fixed gallium `should_tile` RASTER issue.

## Easy causes RULED OUT (source + captured runtime geometry)
- **Present/blit is NOT a stride mismatch.** There is no memcpy/blit — it's render-to-scanout:
  the scanout `VkImage` is created `VK_IMAGE_TILING_LINEAR` at 1920×1080 R8G8B8A8
  (`tools/vkquake-port/platform/pl_phoenix_vk_vid.c:557-596`), its BO pages mapped directly onto
  the fb0 physical pages (`v3d_phoenix_winsys.c` `set_next_scanout`), and each frame's render pass
  `storeOp=STORE` writes straight into fb0. The RCL store honors the LINEAR tiling
  (`external/mesa/src/broadcom/vulkan/v3dvx_cmd_buffer.c:398` `memory_format=RASTER`, `:404` stride
  `= slice->stride`). **width×bpp (7680) == V3DV LINEAR stride (7680) == fb0 pitch (7680)** — all
  three match (captured `fb0 1920x1080 pitch=7680`). No stride shear, no tiled-as-linear.
- **Texture sampling is NOT the gallium should_tile mechanism.** V3DV `v3dv_image.c` tiling is
  STOCK upstream (no Phoenix RASTER mod). All vkQuake sampled textures (world/lightmap/warp) are
  `VK_IMAGE_TILING_OPTIMAL`, uploaded via staging + `vkCmdCopyBufferToImage`, so V3DV tiles them UIF
  and the TMU samples correctly. No large LINEAR sampled image exists in the vkQuake path.

## Localized cause (source-inconclusive for a positive root, but narrowed)
Upstream V3DV NEVER renders directly to a linear scanout — its WSI uses the **prime-blit path**
(render to an OPTIMAL image, then copy to a linear buffer at the display stride; documented at
`v3dv_image.c:419-425`). The Phoenix shim SHORTCUTS this: it renders the full multi-draw textured
scene **directly into the LINEAR RASTER scanout**. The standalone harness only ever proved this for
a clear + single triangle, never a full scene. ⇒ the striping is most likely a **V3D tile-store /
supertile addressing problem in the V3DV RCL setup for the direct-to-RASTER world pass** — i.e. a
V3DV-vs-gallium RCL divergence. (Caveat that keeps this from being conclusive: the gallium GL path
DOES render a full scene direct-to-RASTER at ≥1024×768 and works — `v3d_resource.c:917-920` — so
direct-to-RASTER is not inherently broken; the V3DV RCL store/supertile config for the RASTER RT is
where the divergence must be.)

## Two drifted harness copies — confirmed, NOT the cause
`tools/v3d-driver-port/v3dv_harness.c:281-334` and `tools/vkquake-port/platform/pl_phoenix_vk_vid.c:499-596`
both create the scanout LINEAR at the fb mode with the same stride approach. The drift is only in
test scaffolding (the harness has extra probe/banding-detection paths), not the present code.

## Secondary latent lead (probably NOT the striping)
vkQuake's scanout RT is allocated 2025 pages / 0 scratch for a 1920×1080 RASTER RT (height 1080 not
tile-aligned); the GL RT carries a scratch page. Flagged as a possible bottom-rows OOB — BUT a
RASTER (linear) store writes exactly `height` rows (no tile padding), so a pure RASTER scanout
should not OOB; likely a non-issue for RASTER. Keep as a low-probability lead.

## NEXT (to close — deep, semi-attended GPU dig; lower priority = 1-of-3 renderers)
1. Runtime dump (autonomous, UART): at first present, log `vkGetImageSubresourceLayout(scanout).rowPitch`
   + the RCL store's `memory_format` + stride. Expect rowPitch=7680 / RASTER (confirms present is
   exonerated).
2. Compare the V3DV RCL store setup (`v3dvx_cmd_buffer.c` store_general / the tile-list) vs the
   gallium RCL store for a RASTER RT — find the divergence (supertile/tile-store config, or the
   RT base/stride in the RCL). This is the real root-cause hunt.
3. HDMI band-period: ~64 px bands ⇒ tile-store/supertile addressing (fix = the upstream prime-blit
   path: render OPTIMAL + `vkCmdBlitImage` to the LINEAR scanout, `v3dvx_meta_common.c:982+` handles
   UIF→RASTER detile); ~1 px shear ⇒ stride (already excluded in source). Note: HDMI capture tears,
   so the band-period is best measured on the actual monitor (owner) or from a quake `screenshot`.

**Status:** easy causes ruled out; striping localized to the V3DV direct-to-RASTER RCL divergence
(candidate fix = upstream prime-blit path). Deep + semi-attended to close; lower priority (quake3/
quake2 render fine). Characterization banked for a future GPU/attended pass.
