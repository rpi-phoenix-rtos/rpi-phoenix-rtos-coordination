/*
 * pl_phoenix_vk_vid.c — Phoenix-RTOS / V3DV Vulkan video shim for vkQuake.
 *
 * Replaces upstream gl_vidsdl.c (which is SDL_Vulkan + VK_KHR_swapchain WSI). On
 * Phoenix the V3DV ICD is loader-less and has NO WSI/swapchain extension, so this shim
 * brings Vulkan up the same way the HW-proven v3dv_harness.c does:
 *
 *   vkCreateInstance  -> publish g_vk_instance   (load-bearing for vk_trampolines.c)
 *   vkEnumeratePhysicalDevices / vkCreateDevice -> publish g_vk_device + vulkan_globals.device
 *   vkGetDeviceQueue  -> vulkan_globals.queue
 *
 * and presents to /dev/fb0 via the v3d winsys scanout (v3d_phoenix_set_scanout), NOT a
 * VkSwapchainKHR — exactly the Tier-4a/4b path the harness proved lands pixels on HDMI.
 * That keeps the link closed: pulling in gl_vidsdl's present chain would reintroduce
 * vkAcquireNextImageKHR / vkQueuePresentKHR (WSI the ICD lacks) as fresh undefineds.
 *
 * SCOPE (this session, host-side link): this shim DEFINES every vid/render symbol the
 * engine references at link (the 26 of bucket C), publishes the global instance/device
 * for the trampolines, and populates the vulkan_globals fields the renderer reads, so
 * the link closes to 0 undefined and the struct is runtime-plausible for the main agent.
 * The per-frame GL_BeginRendering/GL_EndRendering are kept MINIMAL-but-real (command
 * buffer begin -> submit -> scanout). The full render-target/render-pass/pipeline build
 * lives in the engine's own gl_rmisc.c R_Create* path (already compiled); this shim only
 * supplies device/queue/formats/dispatch-pointers it keys off.
 *
 * NOT YET RUNTIME-COMPLETE (flagged for the main agent's on-HW Tier-1 work):
 *   - the swapchain images vulkan_globals expects are faked to a single fb0-backed image;
 *     the renderer's NUM_COLOR_BUFFERS/double-buffer assumptions may need adjustment.
 *   - V3DV-feature steering: r_usesops / screen_effects_sops / ray_query are forced off
 *     (V3D has no subgroup-size-control / RT); compute-lightmap path left to the engine's
 *     own fallback. These are runtime knobs, not link symbols.
 *
 * Copyright 2026 Phoenix Systems  %LICENSE%
 */
#include "quakedef.h"
#include "glquake.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h>

/* ---- the global instance/device the trampoline layer (vk_trampolines.c) resolves
 *      every direct vk* call against. Published right after create, as the harness does. */
extern VkInstance g_vk_instance;
extern VkDevice	  g_vk_device;

/* The one ICD entry the loader-less client needs; vk_icd_link.c aliases it to
 * v3dv_GetInstanceProcAddr. Global cmds: NULL instance. Instance cmds: g_vk_instance.
 * Device cmds: vkGetDeviceProcAddr(g_vk_device, ...). (vk_trampolines.c enforces this.) */
extern PFN_vkVoidFunction vkGetInstanceProcAddr (VkInstance instance, const char *pName);

/* v3d winsys scanout (libv3d-phoenix.a) — the fb0 present path the harness proved. */
extern void v3d_phoenix_set_scanout (uint32_t pa, uint32_t bytes);
extern void v3d_phoenix_set_next_scanout (void);

/* ============================ globals the engine references ============================ */
/* (a) GENUINELY UNDEFINED — this shim must DEFINE them (verified vs undefined-symbols.txt) */
viddef_t	  vid;					   /* global video state (vid.h) */
modestate_t	  modestate = MS_UNINIT;
task_handle_t prev_end_rendering_task = INVALID_TASK_HANDLE;

/* vid_* cvars — copied verbatim (names + defaults + flags) from gl_vidsdl.c so the engine's
 * Cvar_RegisterVariable(&vid_gamma) etc. (in gl_rmisc.c / view.c) bind the same objects.
 * NOTE the cvar *names* "gamma"/"contrast" differ from the C identifiers (upstream quirk). */
cvar_t vid_palettize   = {"vid_palettize", "0", CVAR_ARCHIVE};
cvar_t vid_filter	   = {"vid_filter", "0", CVAR_ARCHIVE};
cvar_t vid_anisotropic = {"vid_anisotropic", "0", CVAR_ARCHIVE};
cvar_t vid_fsaa		   = {"vid_fsaa", "0", CVAR_ARCHIVE};
cvar_t vid_fsaamode	   = {"vid_fsaamode", "0", CVAR_ARCHIVE};
cvar_t vid_gamma	   = {"gamma", "0.9", CVAR_ARCHIVE};
cvar_t vid_contrast	   = {"contrast", "1.4", CVAR_ARCHIVE};
cvar_t r_usesops	   = {"r_usesops", "1", CVAR_ARCHIVE};

/* ================================ local Vulkan handles ================================ */
static VkPhysicalDevice phys_device = VK_NULL_HANDLE;
static VkQueue			gfx_queue	= VK_NULL_HANDLE;

/* fb0 scanout geometry (discovered in VID_Init). */
typedef struct {
	unsigned short	   width, height, bpp, pitch;
	unsigned long long smemlen, framebuffer;
} rpi4fb_mode_t;
static rpi4fb_mode_t fb_mode;
static int			 fb_ready = 0;

/* ---- no-WSI render resources (this shim's GL_CreateRenderResources equivalent) ----
 * vkQuake's upstream render path is a WSI swapchain with double-buffered primary + a dozen
 * secondary command-buffer contexts, OIT/WBOIT/post-process render passes, etc. The V3DV ICD
 * has no WSI; we render into the SINGLE fb0-backed scanout VkImage (the Tier-4b harness proved
 * loadOp=CLEAR -> storeOp=STORE into a LINEAR scanout image lands on HDMI). So this collapses the
 * whole machinery to: one primary command buffer + one single-attachment UI render pass + one
 * framebuffer wrapping the fb0 scanout image. SCR_DrawGUI records the 2D/console draws straight
 * into that primary cb (no secondary cbs, no vkCmdExecuteCommands), and the "present" is the
 * scanout image itself. World/3D rendering (V_RenderView) is the documented next blocker. */
static VkCommandPool   gfx_command_pool	  = VK_NULL_HANDLE;
static VkCommandBuffer frame_cb			  = VK_NULL_HANDLE;
static VkImage		   scanout_image	  = VK_NULL_HANDLE;
static VkDeviceMemory  scanout_memory	  = VK_NULL_HANDLE;
static VkImageView	   scanout_view		  = VK_NULL_HANDLE;
static VkRenderPass	   ui_render_pass	  = VK_NULL_HANDLE;
static VkFramebuffer   ui_framebuffer	  = VK_NULL_HANDLE;
static int			   render_resources_created = 0;
static int			   frame_recording	  = 0; /* set between GL_BeginRendering and GL_EndRendering */

#define GIPA(inst, name) ((PFN_##name) vkGetInstanceProcAddr ((inst), #name))

static PFN_vkVoidFunction gdpa (const char *name)
{
	static PFN_vkGetDeviceProcAddr fp = NULL;
	if (!fp)
		fp = (PFN_vkGetDeviceProcAddr) vkGetInstanceProcAddr (g_vk_instance, "vkGetDeviceProcAddr");
	return fp ? fp (g_vk_device, name) : NULL;
}

/* ============================== instance / device bring-up ============================== */
static qboolean create_instance (void)
{
	PFN_vkCreateInstance pCreateInstance = GIPA (NULL, vkCreateInstance);
	if (!pCreateInstance) {
		Sys_Printf ("vkvid: vkGetInstanceProcAddr(vkCreateInstance) NULL\n");
		return false;
	}
	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vkQuake-phoenix",
		.apiVersion = VK_API_VERSION_1_1,
	};
	VkInstanceCreateInfo ici = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app,
		.enabledExtensionCount = 0, /* no WSI/surface ext: fb0 scanout, not a swapchain */
	};
	VkInstance inst = VK_NULL_HANDLE;
	VkResult r = pCreateInstance (&ici, NULL, &inst);
	Sys_Printf ("vkvid: vkCreateInstance -> %d\n", (int)r);
	if (r != VK_SUCCESS)
		return false;
	g_vk_instance = inst; /* publish for the trampolines */
	return true;
}

static qboolean create_device (void)
{
	PFN_vkEnumeratePhysicalDevices pEnum = GIPA (g_vk_instance, vkEnumeratePhysicalDevices);
	PFN_vkCreateDevice			   pCreateDevice = GIPA (g_vk_instance, vkCreateDevice);
	if (!pEnum || !pCreateDevice) {
		Sys_Printf ("vkvid: missing enum/createdevice proc\n");
		return false;
	}

	uint32_t n = 1;
	VkResult r = pEnum (g_vk_instance, &n, &phys_device);
	Sys_Printf ("vkvid: vkEnumeratePhysicalDevices -> %d count=%u\n", (int)r, n);
	if (r < 0 || phys_device == VK_NULL_HANDLE)
		return false;

	/* Memory/feature/queue properties the renderer reads from vulkan_globals. Resolve the
	 * phys-device queries via instance-proc-addr (they dispatch to the instance table). */
	PFN_vkGetPhysicalDeviceMemoryProperties pMem =
		GIPA (g_vk_instance, vkGetPhysicalDeviceMemoryProperties);
	PFN_vkGetPhysicalDeviceFeatures pFeat = GIPA (g_vk_instance, vkGetPhysicalDeviceFeatures);
	if (pMem)
		pMem (phys_device, &vulkan_globals.memory_properties);
	if (pFeat)
		pFeat (phys_device, &vulkan_globals.device_features);
	/* The V3D exposes one universal (graphics+compute+transfer) queue family at index 0. */
	vulkan_globals.gfx_queue_family_index = 0;

	float				  prio = 1.0f;
	VkDeviceQueueCreateInfo qci = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = vulkan_globals.gfx_queue_family_index,
		.queueCount = 1,
		.pQueuePriorities = &prio,
	};
	VkDeviceCreateInfo dci = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &qci,
	};
	VkDevice dev = VK_NULL_HANDLE;
	r = pCreateDevice (phys_device, &dci, NULL, &dev);
	Sys_Printf ("vkvid: vkCreateDevice -> %d\n", (int)r);
	if (r != VK_SUCCESS)
		return false;

	g_vk_device			   = dev; /* publish for the trampolines (device cmds) */
	vulkan_globals.device  = dev;

	PFN_vkGetDeviceQueue pGetQueue = (PFN_vkGetDeviceQueue) gdpa ("vkGetDeviceQueue");
	if (pGetQueue)
		pGetQueue (dev, vulkan_globals.gfx_queue_family_index, 0, &gfx_queue);
	vulkan_globals.queue = gfx_queue;
	return true;
}

/* Populate the dispatch-table function pointers vulkan_globals exposes (the renderer calls
 * through vulkan_globals.vk_cmd_* directly in the hot path). Resolve via vkGetDeviceProcAddr.
 * Unsupported-on-V3D entries (push-descriptor / buffer-device-address / accel-structure) are
 * left NULL — the renderer feature-gates them, and we force those features off below. */
static void populate_dispatch_table (void)
{
	vulkan_globals.vk_cmd_bind_pipeline = (PFN_vkCmdBindPipeline) gdpa ("vkCmdBindPipeline");
	vulkan_globals.vk_cmd_push_constants = (PFN_vkCmdPushConstants) gdpa ("vkCmdPushConstants");
	vulkan_globals.vk_cmd_bind_descriptor_sets =
		(PFN_vkCmdBindDescriptorSets) gdpa ("vkCmdBindDescriptorSets");
	vulkan_globals.vk_cmd_bind_index_buffer =
		(PFN_vkCmdBindIndexBuffer) gdpa ("vkCmdBindIndexBuffer");
	vulkan_globals.vk_cmd_bind_vertex_buffers =
		(PFN_vkCmdBindVertexBuffers) gdpa ("vkCmdBindVertexBuffers");
	vulkan_globals.vk_cmd_draw = (PFN_vkCmdDraw) gdpa ("vkCmdDraw");
	vulkan_globals.vk_cmd_draw_indexed = (PFN_vkCmdDrawIndexed) gdpa ("vkCmdDrawIndexed");
	vulkan_globals.vk_cmd_draw_indexed_indirect =
		(PFN_vkCmdDrawIndexedIndirect) gdpa ("vkCmdDrawIndexedIndirect");
	vulkan_globals.vk_cmd_pipeline_barrier =
		(PFN_vkCmdPipelineBarrier) gdpa ("vkCmdPipelineBarrier");
	vulkan_globals.vk_cmd_copy_buffer_to_image =
		(PFN_vkCmdCopyBufferToImage) gdpa ("vkCmdCopyBufferToImage");
	vulkan_globals.vk_cmd_dispatch = (PFN_vkCmdDispatch) gdpa ("vkCmdDispatch");
	/* push-descriptor / buffer-device-address / accel-structure: V3DV-unsupported -> NULL. */
}

/* ============================== fb0 scanout discovery ============================== */
static void discover_fb0 (void)
{
	int fd = open ("/dev/fb0", O_RDWR);
	memset (&fb_mode, 0, sizeof (fb_mode));
	if (fd >= 0 && ioctl (fd, _IOR ('g', 1, rpi4fb_mode_t), &fb_mode) == 0 && fb_mode.framebuffer != 0) {
		Sys_Printf ("vkvid: fb0 %ux%u pitch=%u pa=0x%llx size=%llu\n", fb_mode.width, fb_mode.height,
		            fb_mode.pitch, (unsigned long long)fb_mode.framebuffer,
		            (unsigned long long)fb_mode.smemlen);
		v3d_phoenix_set_scanout ((uint32_t)fb_mode.framebuffer, (uint32_t)fb_mode.smemlen);
		fb_ready = 1;
	} else {
		Sys_Printf ("vkvid: fb0 GETMODE unavailable (offscreen only)\n");
	}
	if (fd >= 0)
		close (fd);
}

/* ============================ no-WSI render-resource bring-up ============================ */
/* Pick the first memory type the image's requirements allow (the V3DV scanout BO is type 0,
 * HOST_VISIBLE + uncached — the same one the Tier-4b harness selected). */
static uint32_t pick_memory_type (uint32_t type_bits)
{
	for (uint32_t i = 0; i < 32; ++i)
		if (type_bits & (1u << i))
			return i;
	return 0;
}

/* Create the single fb0-backed scanout VkImage + view, the UI render pass + framebuffer, and
 * the basic UI pipelines (reusing the engine's R_CreateBasicPipelines, gated to the UI variant).
 * Returns 1 on success. Mirrors the proven harness present path. Called once. */
static int create_render_resources (void)
{
	if (render_resources_created)
		return 1;
	if (!fb_ready) {
		Sys_Printf ("vkvid: create_render_resources: no fb0 scanout, cannot render\n");
		return 0;
	}

	PFN_vkCreateImage				 pCreateImage	   = (PFN_vkCreateImage) gdpa ("vkCreateImage");
	PFN_vkGetImageMemoryRequirements pGetImageMemReq   = (PFN_vkGetImageMemoryRequirements) gdpa ("vkGetImageMemoryRequirements");
	PFN_vkAllocateMemory			 pAllocateMemory   = (PFN_vkAllocateMemory) gdpa ("vkAllocateMemory");
	PFN_vkBindImageMemory			 pBindImageMemory  = (PFN_vkBindImageMemory) gdpa ("vkBindImageMemory");
	PFN_vkCreateImageView			 pCreateImageView  = (PFN_vkCreateImageView) gdpa ("vkCreateImageView");
	PFN_vkCreateRenderPass			 pCreateRenderPass = (PFN_vkCreateRenderPass) gdpa ("vkCreateRenderPass");
	PFN_vkCreateFramebuffer			 pCreateFramebuffer = (PFN_vkCreateFramebuffer) gdpa ("vkCreateFramebuffer");
	PFN_vkCreateCommandPool			 pCreateCommandPool = (PFN_vkCreateCommandPool) gdpa ("vkCreateCommandPool");
	PFN_vkAllocateCommandBuffers	 pAllocCmdBuffers  = (PFN_vkAllocateCommandBuffers) gdpa ("vkAllocateCommandBuffers");
	if (!pCreateImage || !pGetImageMemReq || !pAllocateMemory || !pBindImageMemory || !pCreateImageView || !pCreateRenderPass ||
	    !pCreateFramebuffer || !pCreateCommandPool || !pAllocCmdBuffers) {
		Sys_Printf ("vkvid: create_render_resources: missing device proc\n");
		return 0;
	}

	VkResult err;

	/* (1) Scanout image: LINEAR R8G8B8A8, color-attachment usage, backed by the fb0 BO. The
	 * winsys binds the NEXT allocated BO to the live scanout, so set_next_scanout() must be
	 * called right before this image's memory is allocated (the harness ordering). */
	{
		VkImageCreateInfo ici = {
			.sType		   = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType	   = VK_IMAGE_TYPE_2D,
			.format		   = vulkan_globals.color_format,
			.extent		   = {fb_mode.width, fb_mode.height, 1},
			.mipLevels	   = 1,
			.arrayLayers   = 1,
			.samples	   = VK_SAMPLE_COUNT_1_BIT,
			.tiling		   = VK_IMAGE_TILING_LINEAR,
			.usage		   = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};
		err = pCreateImage (g_vk_device, &ici, NULL, &scanout_image);
		if (err != VK_SUCCESS) {
			Sys_Printf ("vkvid: vkCreateImage(scanout) -> %d\n", (int)err);
			return 0;
		}

		VkMemoryRequirements mreq;
		memset (&mreq, 0, sizeof (mreq));
		pGetImageMemReq (g_vk_device, scanout_image, &mreq);

		VkMemoryAllocateInfo mai = {
			.sType			 = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize	 = mreq.size ? mreq.size : (VkDeviceSize)fb_mode.smemlen,
			.memoryTypeIndex = pick_memory_type (mreq.memoryTypeBits),
		};
		v3d_phoenix_set_next_scanout (); /* the next BO (this image's memory) backs the live scanout */
		err = pAllocateMemory (g_vk_device, &mai, NULL, &scanout_memory);
		if (err == VK_SUCCESS)
			err = pBindImageMemory (g_vk_device, scanout_image, scanout_memory, 0);
		if (err != VK_SUCCESS) {
			Sys_Printf ("vkvid: scanout image alloc/bind -> %d\n", (int)err);
			return 0;
		}
		Sys_Printf ("vkvid: scanout image %ux%u bound (mem=%u)\n", fb_mode.width, fb_mode.height, (unsigned)mreq.size);
	}

	/* (2) Image view over the scanout image. */
	{
		VkImageViewCreateInfo vci = {
			.sType			  = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image			  = scanout_image,
			.viewType		  = VK_IMAGE_VIEW_TYPE_2D,
			.format			  = vulkan_globals.color_format,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
		};
		err = pCreateImageView (g_vk_device, &vci, NULL, &scanout_view);
		if (err != VK_SUCCESS) {
			Sys_Printf ("vkvid: vkCreateImageView -> %d\n", (int)err);
			return 0;
		}
	}

	/* (3) UI render pass: ONE color attachment, ONE subpass, samples=1, color_format. This is
	 * the exact shape R_CreateBasicPipelines expects for RENDER_PASS_INDEX_UI (attachment_count=1,
	 * subpass=0, samples=1), so the pipelines it builds are render-pass-compatible with the
	 * draw-time vkCmdBeginRenderPass below. loadOp=CLEAR -> storeOp=STORE is the harness's proven
	 * scanout-paint path. finalLayout=GENERAL matches the host-mapped scanout BO. */
	{
		VkAttachmentDescription att = {
			.format			= vulkan_globals.color_format,
			.samples		= VK_SAMPLE_COUNT_1_BIT,
			.loadOp			= VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp		= VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp	= VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout	= VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout	= VK_IMAGE_LAYOUT_GENERAL,
		};
		VkAttachmentReference attref = {
			.attachment = 0,
			.layout		= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};
		VkSubpassDescription sub = {
			.pipelineBindPoint	  = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.colorAttachmentCount = 1,
			.pColorAttachments	  = &attref,
		};
		VkRenderPassCreateInfo rpci = {
			.sType			 = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments	 = &att,
			.subpassCount	 = 1,
			.pSubpasses		 = &sub,
		};
		err = pCreateRenderPass (g_vk_device, &rpci, NULL, &ui_render_pass);
		if (err != VK_SUCCESS) {
			Sys_Printf ("vkvid: vkCreateRenderPass(ui) -> %d\n", (int)err);
			return 0;
		}
	}

	/* (4) Framebuffer wrapping the scanout view. */
	{
		VkFramebufferCreateInfo fbci = {
			.sType			 = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass		 = ui_render_pass,
			.attachmentCount = 1,
			.pAttachments	 = &scanout_view,
			.width			 = fb_mode.width,
			.height			 = fb_mode.height,
			.layers			 = 1,
		};
		err = pCreateFramebuffer (g_vk_device, &fbci, NULL, &ui_framebuffer);
		if (err != VK_SUCCESS) {
			Sys_Printf ("vkvid: vkCreateFramebuffer(ui) -> %d\n", (int)err);
			return 0;
		}
	}

	/* (5) Command pool + one PRIMARY command buffer for the per-frame record/submit. */
	{
		VkCommandPoolCreateInfo cpci = {
			.sType			  = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags			  = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = vulkan_globals.gfx_queue_family_index,
		};
		err = pCreateCommandPool (g_vk_device, &cpci, NULL, &gfx_command_pool);
		if (err != VK_SUCCESS) {
			Sys_Printf ("vkvid: vkCreateCommandPool(gfx) -> %d\n", (int)err);
			return 0;
		}
		VkCommandBufferAllocateInfo cbai = {
			.sType				= VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool		= gfx_command_pool,
			.level				= VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		err = pAllocCmdBuffers (g_vk_device, &cbai, &frame_cb);
		if (err != VK_SUCCESS) {
			Sys_Printf ("vkvid: vkAllocateCommandBuffers(gfx) -> %d\n", (int)err);
			return 0;
		}
	}

	/* (6) The single SCBX_GUI command-buffer context the 2D draw path (SCR_DrawGUI) records into.
	 * Upstream allocates a *pointer* per SCBX index and uses a SECONDARY cb executed into the
	 * primary; here it points at the PRIMARY frame_cb (inline subpass), and shares ui_render_pass /
	 * RENDER_PASS_INDEX_UI with the pipeline build so everything is render-pass-compatible. */
	{
		cb_context_t *gui = (cb_context_t *)Mem_Alloc (sizeof (cb_context_t));
		gui->cb				  = frame_cb;
		gui->current_canvas	  = CANVAS_INVALID;
		gui->render_pass	  = ui_render_pass;
		gui->render_pass_index = RENDER_PASS_INDEX_UI;
		gui->subpass		  = 0;
		vulkan_globals.secondary_cb_contexts[SCBX_GUI] = gui;
	}

	/* (7) Build the basic UI pipelines by reusing the engine's own R_CreateBasicPipelines (now
	 * un-static + guarded to skip the MAIN/OIT/WBOIT variants whose render pass is NULL). It reads
	 * secondary_cb_contexts[SCBX_GUI]->render_pass (set above) for the UI variant. This reuses the
	 * exact vertex-input / blend / push-constant state the draw path expects — hand-authoring it
	 * would risk a silent GPU hang. Requires basic_pipeline_layout (R_CreatePipelineLayouts, done
	 * in VID_Init) + shader modules (created here, destroyed after). */
	R_CreateShaderModules ();
	R_InitVertexAttributes ();
	R_CreateBasicPipelines ();
	R_DestroyShaderModules ();

	vulkan_globals.color_clear_value.color.float32[0] = 0.0f;
	vulkan_globals.color_clear_value.color.float32[1] = 0.0f;
	vulkan_globals.color_clear_value.color.float32[2] = 0.0f;
	vulkan_globals.color_clear_value.color.float32[3] = 1.0f;

	render_resources_created = 1;
	Sys_Printf ("vkvid: render resources created (UI render pass + fb0 framebuffer + basic pipelines)\n");
	return 1;
}

/* ================================ VID_* public surface ================================ */
void VID_Init (void)
{
	Sys_Printf ("vkvid: VID_Init (Phoenix/V3DV fb0 scanout, no WSI)\n");

	/* The vid_* cvars are registered by the engine (gl_rmisc.c / view.c). Bring Vulkan up. */
	if (!create_instance () || !create_device ()) {
		Sys_Error ("VID_Init: Vulkan bring-up failed");
		return;
	}
	populate_dispatch_table ();
	discover_fb0 ();

	/* Steer onto a V3DV-supported feature path (no subgroup-size-control, no RT). These are
	 * the runtime gates the renderer keys off; force them off so it picks the classic raster
	 * + non-sops compute path. (Cvar values; the engine re-reads them.) */
	vulkan_globals.screen_effects_sops = false;
	vulkan_globals.ray_query		   = false;
	vulkan_globals.non_solid_fill	   = false;
	vulkan_globals.multi_draw_indirect = false;
	Cvar_SetValueQuick (&r_usesops, 0.0f);

	/* Force the SINGLE-THREADED render path. host.c calls SCR_UpdateScreen(true), and with the
	 * task path SCR_DrawGUI runs as a worker task while this shim's inline GL_EndRendering would
	 * end the render pass + submit frame_cb before the draws are recorded -> empty/garbage frame.
	 * Our synchronous single-command-buffer present REQUIRES the non-task path. r_tasks=0 makes
	 * SCR_UpdateScreen's use_tasks gate false unconditionally (independent of worker count). */
	{
		extern cvar_t r_tasks;
		Cvar_SetValueQuick (&r_tasks, 0.0f);
	}

	/* Formats + sample count the renderer's render-pass/pipeline build reads. R8G8B8A8 is the
	 * fb0 scanout format the harness rendered into; single-sample (no MSAA on first light). */
	vulkan_globals.swap_chain_format = VK_FORMAT_R8G8B8A8_UNORM;
	vulkan_globals.color_format		 = VK_FORMAT_R8G8B8A8_UNORM;
	vulkan_globals.depth_format		 = VK_FORMAT_D32_SFLOAT;
	vulkan_globals.sample_count		 = VK_SAMPLE_COUNT_1_BIT;
	vulkan_globals.supersampling	 = false;
	vulkan_globals.device_idle		 = true;

	/* Video geometry from fb0 (fallback 1024x768 if fb0 absent — same default the V3D scout
	 * used). The engine recomputes refdef off vid.width/height. */
	vid.width	   = fb_ready ? fb_mode.width : 1024;
	vid.height	   = fb_ready ? fb_mode.height : 768;
	vid.conwidth   = vid.width;
	vid.conheight  = vid.height;
	vid.rowbytes   = fb_ready ? fb_mode.pitch : (vid.width * 4);
	vid.aspect	   = (float)vid.width / (float)vid.height;
	vid.recalc_refdef = 1;
	vid.restart_next_frame = false;
	modestate = MS_FULLSCREEN;

	/* Renderer-resource init — restored from upstream gl_vidsdl.c VID_Init (4108-4116).
	 * This shim REPLACES gl_vidsdl.c, and the original drop of this block was the bug:
	 * the heaps/buffers/layouts these create (most visibly mesh_buffer_heap from
	 * R_InitMeshHeap) were never allocated, so the first alias-model mesh upload hit a
	 * NULL heap in GL_HeapAllocate. These functions live in the COMPILED engine files
	 * (gl_rmisc.c / gl_mesh.c / gl_texmgr.c), so calling them here is the faithful
	 * upstream ordering fix, not a workaround.
	 *
	 * Deliberately NOT called (all static-linkage inside the excluded gl_vidsdl.c, and
	 * only reached via the screen-effects / GL_CreateRenderResources path this shim does
	 * not run — the upstream line 4118 `GL_CreateRenderResources()` is itself commented):
	 *   R_CreatePaletteOctreeBuffers()  -> palette_octree_buffer (screen-effects only)
	 *   VID_Gamma_Init() / VID_Menu_Init()  -> gamma ramp + video menu
	 * If the screen-effects post-process or the video menu are later enabled, those will
	 * need un-static-ing in external/vkquake (and tracking in the .patch). */
	vulkan_globals.staging_buffer_size = INITIAL_STAGING_BUFFER_SIZE_KB * 1024;
	R_InitStagingBuffers ();
	R_CreateDescriptorSetLayouts ();
	R_CreateDescriptorPool ();
	R_InitGPUBuffers ();
	R_InitMeshHeap ();
	TexMgr_InitHeap ();
	R_InitSamplers ();
	R_CreatePipelineLayouts ();

	Sys_Printf ("vkvid: VID_Init done (%dx%d, device=%p queue=%p)\n", vid.width, vid.height,
	            (void *)g_vk_device, (void *)gfx_queue);
}

void VID_Shutdown (void)
{
	GL_WaitForDeviceIdle ();
	/* Device/instance teardown deliberately omitted: the V3DV ICD destroy paths are not
	 * exercised here and the process exits anyway. (Main agent: wire vkDestroyDevice /
	 * vkDestroyInstance if a clean restart is needed.) */
	Sys_Printf ("vkvid: VID_Shutdown\n");
}

void VID_Restart (qboolean set_mode)
{
	(void)set_mode;
	/* No mode switching on the fixed fb0 scanout; a real restart would rebuild render
	 * resources. Synchronize any in-flight rendering so the engine's invariant holds. */
	GL_SynchronizeEndRenderingTask ();
	GL_WaitForDeviceIdle ();
	Sys_Printf ("vkvid: VID_Restart (no-op on fixed fb0 scanout)\n");
}

void VID_Toggle (void)
{
	/* Fullscreen-only on HDMI scanout; nothing to toggle. */
}

void VID_Lock (void) {}

/* VID_HasMouseOrInputFocus / VID_IsMinimized / VID_GetWindow are referenced by the SDL host
 * loop only; pl_phoenix_main.c uses a fixed-cadence loop, so they are not link symbols here. */

/* ================================ GL_* render surface ================================ */
void GL_SetObjectName (uint64_t object, VkObjectType object_type, const char *name)
{
	/* Debug-utils object naming: no-op (VK_EXT_debug_utils not enabled). */
	(void)object;
	(void)object_type;
	(void)name;
}

void GL_WaitForDeviceIdle (void)
{
	GL_SynchronizeEndRenderingTask ();
	PFN_vkDeviceWaitIdle pWait = (PFN_vkDeviceWaitIdle) gdpa ("vkDeviceWaitIdle");
	if (pWait && g_vk_device)
		pWait (g_vk_device);
	vulkan_globals.device_idle = true;
}

void GL_SynchronizeEndRenderingTask (void)
{
	/* The end-of-frame task is run inline (single submit path below), so there is never an
	 * outstanding task to join; just clear the handle. (Main agent: if tasks.c offloads the
	 * end-rendering work, join prev_end_rendering_task here.) */
	if (prev_end_rendering_task != INVALID_TASK_HANDLE) {
		Task_Join (prev_end_rendering_task, 0);
		prev_end_rendering_task = INVALID_TASK_HANDLE;
	}
}

void GL_UpdateDescriptorSets (void)
{
	/* Deferred descriptor-set writes: the engine batches vkUpdateDescriptorSets calls here.
	 * Minimal shim flushes nothing extra (the renderer issues its writes directly); kept as a
	 * defined symbol + device-idle barrier so the engine's ordering assumption holds. */
}

qboolean GL_BeginRendering (qboolean use_tasks, task_handle_t *begin_rendering_task, int *width,
                            int *height)
{
	(void)use_tasks;
	if (vid.restart_next_frame) {
		VID_Restart (false);
		vid.restart_next_frame = false;
	}

	/* Create render resources lazily on the first frame (matches upstream GL_BeginRendering, which
	 * defers GL_CreateRenderResources to the first frame so the device/heaps are fully up). */
	if (!render_resources_created && !create_render_resources ())
		return false;

	*width	= vid.width;
	*height = vid.height;
	if (begin_rendering_task)
		*begin_rendering_task = INVALID_TASK_HANDLE; /* inline path: no async begin task */

	/* Begin the per-frame primary command buffer, then begin the UI render pass INLINE on the fb0
	 * framebuffer. SCR_DrawGUI then records its 2D draws straight into frame_cb (it reads the
	 * SCBX_GUI cb_context we point at frame_cb). Reset the canvas + pipeline so GL_SetCanvas /
	 * R_BindPipeline re-emit their state this frame. */
	PFN_vkResetCommandBuffer pReset = (PFN_vkResetCommandBuffer) gdpa ("vkResetCommandBuffer");
	PFN_vkBeginCommandBuffer pBegin = (PFN_vkBeginCommandBuffer) gdpa ("vkBeginCommandBuffer");
	PFN_vkCmdBeginRenderPass pBeginRP = (PFN_vkCmdBeginRenderPass) gdpa ("vkCmdBeginRenderPass");
	if (!pBegin || !pBeginRP) {
		Sys_Printf ("vkvid: GL_BeginRendering: missing device proc\n");
		return false;
	}

	if (pReset)
		pReset (frame_cb, 0);

	cb_context_t *gui = vulkan_globals.secondary_cb_contexts[SCBX_GUI];
	gui->cb				= frame_cb;
	gui->current_canvas = CANVAS_INVALID;
	memset (&gui->current_pipeline, 0, sizeof (gui->current_pipeline));

	VkCommandBufferBeginInfo bbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	VkResult err = pBegin (frame_cb, &bbi);
	if (err != VK_SUCCESS) {
		Sys_Printf ("vkvid: vkBeginCommandBuffer -> %d\n", (int)err);
		return false;
	}

	VkClearValue clear = vulkan_globals.color_clear_value;
	VkRenderPassBeginInfo rpbi = {
		.sType			 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass		 = ui_render_pass,
		.framebuffer	 = ui_framebuffer,
		.renderArea		 = {{0, 0}, {(uint32_t)vid.width, (uint32_t)vid.height}},
		.clearValueCount = 1,
		.pClearValues	 = &clear,
	};
	pBeginRP (frame_cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

	/* The viewport/scissor are dynamic state set per-canvas by GL_SetCanvas -> GL_Viewport. Seed
	 * a full-frame scissor/viewport so a draw before the first GL_SetCanvas has valid state. */
	PFN_vkCmdSetViewport pSetVp = (PFN_vkCmdSetViewport) gdpa ("vkCmdSetViewport");
	PFN_vkCmdSetScissor	 pSetSc = (PFN_vkCmdSetScissor) gdpa ("vkCmdSetScissor");
	if (pSetVp) {
		VkViewport vp = {0.0f, 0.0f, (float)vid.width, (float)vid.height, 0.0f, 1.0f};
		pSetVp (frame_cb, 0, 1, &vp);
	}
	if (pSetSc) {
		VkRect2D sc = {{0, 0}, {(uint32_t)vid.width, (uint32_t)vid.height}};
		pSetSc (frame_cb, 0, 1, &sc);
	}

	/* New frame: cycle the dynamic vertex/index/uniform buffers (resets per-frame alloc offsets).
	 * The 2D draw path (R_VertexAllocate / R_IndexAllocate in gl_draw.c) writes into these; without
	 * the swap they'd accumulate across frames and overflow. Mirrors GL_BeginRenderingTask's tail. */
	R_SwapDynamicBuffers ();

	vulkan_globals.device_idle = false;
	frame_recording = 1;
	return true;
}

task_handle_t GL_EndRendering (qboolean use_tasks, qboolean use_swapchain)
{
	(void)use_tasks;
	(void)use_swapchain;

	if (!frame_recording)
		return INVALID_TASK_HANDLE;
	frame_recording = 0;

	/* Flush staging (texture uploads) + dynamic-buffer mapped ranges so the GPU sees the 2D vertex/
	 * index/uniform data recorded this frame. R_SubmitStagingBuffers runs its own staging cb+fence;
	 * R_FlushDynamicBuffers flushes the non-coherent mapped dynamic buffers. Mirrors the head of
	 * GL_EndRenderingTask. Must happen before the queue submit below. */
	R_SubmitStagingBuffers ();
	R_FlushDynamicBuffers ();

	/* End the UI render pass + command buffer, submit on the graphics queue, and wait. The
	 * render pass's storeOp=STORE writes the rendered tiles to the fb0 scanout BO -> the frame
	 * is visible on HDMI. This single-image scanout IS the "present" (no WSI swapchain, no
	 * vkQueuePresentKHR). Synchronous submit + wait mirrors the proven Tier-4b harness. */
	PFN_vkCmdEndRenderPass pEndRP	= (PFN_vkCmdEndRenderPass) gdpa ("vkCmdEndRenderPass");
	PFN_vkEndCommandBuffer pEnd		= (PFN_vkEndCommandBuffer) gdpa ("vkEndCommandBuffer");
	PFN_vkQueueSubmit	   pSubmit	= (PFN_vkQueueSubmit) gdpa ("vkQueueSubmit");
	if (!pEndRP || !pEnd || !pSubmit) {
		Sys_Printf ("vkvid: GL_EndRendering: missing device proc\n");
		return INVALID_TASK_HANDLE;
	}

	pEndRP (frame_cb);

	VkResult err = pEnd (frame_cb);
	if (err != VK_SUCCESS) {
		Sys_Printf ("vkvid: vkEndCommandBuffer -> %d\n", (int)err);
		return INVALID_TASK_HANDLE;
	}

	VkSubmitInfo si = {
		.sType				= VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers	= &frame_cb,
	};
	err = pSubmit (gfx_queue, 1, &si, VK_NULL_HANDLE);
	if (err != VK_SUCCESS)
		Sys_Printf ("vkvid: vkQueueSubmit -> %d\n", (int)err);

	GL_WaitForDeviceIdle ();
	return INVALID_TASK_HANDLE; /* inline: no async end task to chain */
}

/* ================================ menu + screenshot stubs ================================ */
void M_Menu_Video_f (void)
{
	/* Video options menu: no modes to pick on the fixed fb0 scanout. Stubbed. */
}

void M_Video_Draw (cb_context_t *cbx)
{
	(void)cbx;
}

void M_Video_Key (int key)
{
	(void)key;
}

void SCR_ScreenShot_f (void)
{
	/* Screenshot: would read back the scanout BO + write a PNG (lodepng is linked). Stubbed
	 * for first-light; the harness already proves scanout readback works. */
	Sys_Printf ("vkvid: screenshot not implemented\n");
}
