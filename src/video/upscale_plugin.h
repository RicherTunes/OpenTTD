/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file upscale_plugin.h C ABI interface for external upscaling plugins (DLSS, FSR).
 *
 * This header defines the stable C ABI boundary between the GPLv2 OpenTTD binary
 * and optional proprietary upscaling plugins (e.g., NVIDIA DLSS via Streamline SDK).
 *
 * The plugin DLL is loaded at runtime via dlopen()/LoadLibrary(). The main OpenTTD
 * binary never #includes any proprietary headers or links against proprietary libraries.
 * This satisfies GPLv2 requirements through the "mere aggregation" of independent programs.
 *
 * Plugin developers: implement the UpscalePluginAPI struct and export GetUpscalePlugin().
 */

#ifndef VIDEO_UPSCALE_PLUGIN_H
#define VIDEO_UPSCALE_PLUGIN_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/** Quality mode for upscaling. */
enum UpscaleQualityMode {
	UPSCALE_QUALITY_NATIVE_AA = 0,   /**< Native resolution anti-aliasing (DLAA). */
	UPSCALE_QUALITY_QUALITY = 1,     /**< Quality mode (~1.5x upscale). */
	UPSCALE_QUALITY_BALANCED = 2,    /**< Balanced mode (~1.7x upscale). */
	UPSCALE_QUALITY_PERFORMANCE = 3, /**< Performance mode (~2.0x upscale). */
	UPSCALE_QUALITY_ULTRA_PERF = 4,  /**< Ultra Performance mode (~3.0x upscale). */
};

/** Resource description for texture inputs/outputs. */
struct UpscaleTextureDesc {
	uint32_t gl_texture_id;   /**< OpenGL texture handle (or 0 if not GL). */
	void *native_handle;      /**< Native API handle (ID3D11ShaderResourceView*, VkImage, etc.). */
	uint32_t width;           /**< Texture width in pixels. */
	uint32_t height;          /**< Texture height in pixels. */
	uint32_t format;          /**< Pixel format (API-specific enum value). */
};

/** Per-frame parameters for upscaling dispatch. */
struct UpscaleDispatchParams {
	UpscaleTextureDesc color_input;   /**< Input color buffer (render resolution). */
	UpscaleTextureDesc motion_vectors; /**< Motion vector texture (RG16F, screen-space). */
	UpscaleTextureDesc depth;         /**< Depth texture (R16F or R32F). */
	UpscaleTextureDesc output;        /**< Output texture (display resolution). */
	float jitter_x;                   /**< Sub-pixel jitter X offset in pixels. */
	float jitter_y;                   /**< Sub-pixel jitter Y offset in pixels. */
	float delta_time;                 /**< Frame delta time in seconds. */
	int32_t reset;                    /**< Non-zero to reset temporal history (scene cut). */
	int32_t render_width;             /**< Input render width. */
	int32_t render_height;            /**< Input render height. */
	int32_t display_width;            /**< Output display width. */
	int32_t display_height;           /**< Output display height. */
};

/** Plugin capability flags. */
enum UpscalePluginCaps {
	UPSCALE_CAP_SUPER_RES    = 0x01, /**< Supports temporal super resolution upscaling. */
	UPSCALE_CAP_FRAME_GEN    = 0x02, /**< Supports frame generation. */
	UPSCALE_CAP_RAY_RECON    = 0x04, /**< Supports ray reconstruction. */
	UPSCALE_CAP_OPENGL       = 0x10, /**< Works with OpenGL context. */
	UPSCALE_CAP_VULKAN       = 0x20, /**< Works with Vulkan context. */
	UPSCALE_CAP_DX11         = 0x40, /**< Works with DirectX 11 context. */
	UPSCALE_CAP_DX12         = 0x80, /**< Works with DirectX 12 context. */
};

/**
 * The upscaling plugin API.
 * All functions use the C calling convention for ABI stability.
 * The plugin is responsible for managing its own GPU resources.
 */
struct UpscalePluginAPI {
	/** Plugin version (must match UPSCALE_PLUGIN_API_VERSION). */
	uint32_t api_version;

	/**
	 * Initialize the plugin.
	 * @param device Native graphics device handle (ID3D11Device*, VkDevice, or NULL for GL).
	 * @param render_w Initial render width.
	 * @param render_h Initial render height.
	 * @param display_w Initial display width.
	 * @param display_h Initial display height.
	 * @return 0 on success, non-zero error code on failure.
	 */
	int32_t (*init)(void *device, int32_t render_w, int32_t render_h,
	                int32_t display_w, int32_t display_h);

	/** Shut down the plugin and free all resources. */
	void (*shutdown)(void);

	/**
	 * Execute upscaling for one frame.
	 * @param params Per-frame dispatch parameters.
	 * @return 0 on success, non-zero on failure.
	 */
	int32_t (*evaluate)(const UpscaleDispatchParams *params);

	/**
	 * Set the upscaling quality mode.
	 * @param mode Quality mode enum value.
	 */
	void (*set_quality_mode)(int32_t mode);

	/** Get the plugin's capability flags. */
	uint32_t (*get_capabilities)(void);

	/** Get a human-readable plugin name string. */
	const char *(*get_name)(void);

	/** Get the plugin version string. */
	const char *(*get_version)(void);

	/**
	 * Query the recommended render resolution for a given display resolution and quality mode.
	 * @param display_w Display width.
	 * @param display_h Display height.
	 * @param quality_mode Quality mode.
	 * @param[out] render_w Recommended render width.
	 * @param[out] render_h Recommended render height.
	 */
	void (*get_render_resolution)(int32_t display_w, int32_t display_h, int32_t quality_mode,
	                              int32_t *render_w, int32_t *render_h);
};

/** Current plugin API version. Increment on breaking changes. */
#define UPSCALE_PLUGIN_API_VERSION 1

/**
 * Plugin entry point. The plugin DLL must export this function.
 * @return Pointer to the plugin's API struct, or NULL on failure.
 */
typedef UpscalePluginAPI *(*GetUpscalePluginFunc)(void);

#ifdef __cplusplus
}
#endif

/* --- C++ wrapper for loading plugins --- */

#ifdef __cplusplus

#include <string>

/**
 * Load an upscaling plugin from a shared library.
 * @param path Path to the plugin DLL/SO.
 * @return Pointer to the plugin API, or nullptr on failure.
 */
UpscalePluginAPI *LoadUpscalePlugin(const std::string &path);

/**
 * Unload the currently loaded upscaling plugin.
 */
void UnloadUpscalePlugin();

/**
 * Get the currently loaded upscaling plugin, or nullptr.
 */
UpscalePluginAPI *GetLoadedUpscalePlugin();

#endif /* __cplusplus */

#endif /* VIDEO_UPSCALE_PLUGIN_H */
