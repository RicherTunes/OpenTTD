/*
 * DLSS Plugin Skeleton for OpenTTD
 *
 * This is a template showing how to create a DLSS plugin that works with
 * OpenTTD's UpscalePluginAPI C ABI. A real implementation would use
 * NVIDIA's Streamline SDK to drive DLSS Super Resolution.
 *
 * IMPORTANT: This plugin links against proprietary NVIDIA DLLs (nvngx_dlss.dll)
 * and MUST be distributed separately from the GPLv2 OpenTTD binary.
 *
 * Build: cl /LD /O2 dlss_plugin.cpp /link /OUT:dlss_plugin.dll
 *
 * Required files (from NVIDIA Streamline SDK):
 *   - sl.interposer.dll
 *   - sl.dlss.dll
 *   - nvngx_dlss.dll
 *
 * To use:
 *   1. Build this plugin into dlss_plugin.dll
 *   2. Place dlss_plugin.dll + NVIDIA DLLs in the OpenTTD game directory
 *   3. OpenTTD will auto-detect and load the plugin on startup
 *   4. Select "Plugin" upscale mode in Game Options > GPU Rendering
 */

#include "../../src/video/upscale_plugin.h"

#ifdef _WIN32
#define PLUGIN_EXPORT __declspec(dllexport)
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#include <cstdio>

/* --- Streamline SDK integration (placeholder) --- */

/*
 * A real DLSS plugin would:
 *
 * #include <sl.h>
 * #include <sl_dlss.h>
 * #include <sl_consts.h>
 *
 * And in init():
 *   sl::Preferences prefs;
 *   prefs.renderAPI = sl::RenderAPI::eD3D11; // or eVulkan
 *   slInit(prefs);
 *   slSetFeatureLoaded(sl::kFeatureDLSS, true);
 *
 * And in evaluate():
 *   sl::DLSSOptions options;
 *   options.mode = sl::DLSSMode::eBalanced;
 *   slDLSSSetOptions(viewport, options);
 *   sl::ResourceTag tags[] = { colorTag, mvTag, depthTag, outputTag };
 *   slSetTag(viewport, tags, ...);
 *   slEvaluateFeature(sl::kFeatureDLSS, frame, &viewport, ...);
 */

static bool s_initialized = false;

static int32_t dlss_init(void *device, int32_t rw, int32_t rh, int32_t dw, int32_t dh)
{
	/* TODO: Initialize Streamline SDK and DLSS feature.
	 * The 'device' parameter is the native graphics device:
	 *   - For DX11: ID3D11Device*
	 *   - For Vulkan: VkDevice
	 *   - For OpenGL: NULL (not natively supported by DLSS)
	 *
	 * DLSS requires DX11, DX12, or Vulkan. If device is NULL (OpenGL),
	 * DLSS cannot be used and this function should return -1.
	 */
	if (device == nullptr) {
		fprintf(stderr, "DLSS plugin: OpenGL not supported, need DX11/Vulkan device\n");
		return -1;
	}

	s_initialized = true;
	return 0;
}

static void dlss_shutdown(void)
{
	/* TODO: slShutdown(); */
	s_initialized = false;
}

static int32_t dlss_evaluate(const UpscaleDispatchParams *params)
{
	if (!s_initialized || params == nullptr) return -1;

	/* TODO: Dispatch DLSS Super Resolution:
	 *
	 * 1. Create Streamline resource tags from params->color_input,
	 *    params->motion_vectors, params->depth
	 * 2. Set DLSS options (mode, output size)
	 * 3. Call slEvaluateFeature(sl::kFeatureDLSS, ...)
	 * 4. Result is written to params->output
	 *
	 * The plugin receives OpenGL texture IDs in params->*.gl_texture_id.
	 * For DLSS on DX11/Vulkan, the plugin would need to manage interop
	 * between OpenGL textures and the native API, or the host would
	 * provide native handles via params->*.native_handle.
	 */

	return -1; /* Not implemented -- skeleton only. */
}

static void dlss_set_quality_mode(int32_t mode)
{
	/* TODO: Map to sl::DLSSMode */
}

static uint32_t dlss_get_capabilities(void)
{
	return UPSCALE_CAP_SUPER_RES | UPSCALE_CAP_DX11 | UPSCALE_CAP_VULKAN;
}

static const char *dlss_get_name(void)
{
	return "DLSS Plugin (Skeleton)";
}

static const char *dlss_get_version(void)
{
	return "0.1.0-skeleton";
}

static void dlss_get_render_resolution(int32_t dw, int32_t dh, int32_t quality,
                                        int32_t *rw, int32_t *rh)
{
	/* DLSS quality mode render scale ratios. */
	float scale = 1.0f;
	switch (quality) {
		case UPSCALE_QUALITY_NATIVE_AA:   scale = 1.0f; break;
		case UPSCALE_QUALITY_QUALITY:     scale = 1.0f / 1.5f; break;
		case UPSCALE_QUALITY_BALANCED:    scale = 1.0f / 1.7f; break;
		case UPSCALE_QUALITY_PERFORMANCE: scale = 0.5f; break;
		case UPSCALE_QUALITY_ULTRA_PERF:  scale = 1.0f / 3.0f; break;
		default: scale = 1.0f / 1.5f; break;
	}
	*rw = (int32_t)(dw * scale) & ~1;
	*rh = (int32_t)(dh * scale) & ~1;
}

static UpscalePluginAPI s_dlss_api = {
	UPSCALE_PLUGIN_API_VERSION,
	dlss_init,
	dlss_shutdown,
	dlss_evaluate,
	dlss_set_quality_mode,
	dlss_get_capabilities,
	dlss_get_name,
	dlss_get_version,
	dlss_get_render_resolution,
};

extern "C" PLUGIN_EXPORT UpscalePluginAPI *GetUpscalePlugin(void)
{
	return &s_dlss_api;
}
