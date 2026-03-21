/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file render_backend.h Abstract rendering backend interface for GPU-accelerated presentation. */

#ifndef VIDEO_RENDER_BACKEND_H
#define VIDEO_RENDER_BACKEND_H

#include "../core/geometry_type.hpp"
#include "../gfx_type.h"
#include "postprocess.h"
#include "motion_vector.h"
#include <string>
#include <memory>

/**
 * Upscaling technology available through external plugins.
 */
enum class UpscaleTechnology : uint8_t {
	None,         ///< No external upscaler.
	FSR2,         ///< AMD FSR 2.x temporal upscaling (OpenGL compute or Vulkan).
	FSR3,         ///< AMD FSR 3.x temporal upscaling (Vulkan only).
	DLSS,         ///< NVIDIA DLSS Super Resolution (DX11/Vulkan via plugin).
};

/**
 * Information about a rendering backend's capabilities.
 */
struct RenderBackendCapabilities {
	bool supports_compute = false;           ///< GL 4.3+ compute shaders available.
	bool supports_fbo = false;               ///< Framebuffer objects available.
	bool supports_temporal_upscale = false;   ///< Temporal upscaling possible (needs compute + MV).
	bool supports_dlss_plugin = false;        ///< DLSS plugin can be loaded (DX11 or Vulkan backend).
	bool supports_fsr2 = false;              ///< FSR 2 integration available.
	bool supports_fsr3 = false;              ///< FSR 3 integration available.
	std::string api_name;                    ///< "OpenGL 4.5", "Vulkan 1.3", "DirectX 11", etc.
	int api_version_major = 0;               ///< API major version.
	int api_version_minor = 0;               ///< API minor version.
};

/**
 * Abstract rendering backend interface.
 *
 * Rendering backends are composed inside existing video drivers (SDL2, Win32, Cocoa).
 * The video driver handles window management, input, and context creation.
 * The render backend handles GPU resource management and presentation.
 *
 * This abstraction allows the same video driver to use OpenGL, Vulkan, or DX11
 * for rendering without changing the driver registration or factory pattern.
 */
class RenderBackend {
public:
	virtual ~RenderBackend() = default;

	/* --- Lifecycle --- */

	/** Get the backend's capabilities. */
	virtual RenderBackendCapabilities GetCapabilities() const = 0;

	/** Resize the rendering surface. */
	virtual bool Resize(int w, int h, bool force = false) = 0;

	/* --- CPU buffer access (for blitter) --- */

	/** Map the video buffer for CPU write access. */
	virtual void *GetVideoBuffer() = 0;

	/** Unmap the video buffer and upload changed region. */
	virtual void ReleaseVideoBuffer(const Rect &update_rect) = 0;

	/* --- Animation buffer (for 40bpp blitter) --- */

	/** Check if an animation buffer is available. */
	virtual bool HasAnimBuffer() const { return false; }

	/** Map the animation buffer for CPU write access. */
	virtual uint8_t *GetAnimBuffer() { return nullptr; }

	/** Unmap the animation buffer and upload changed region. */
	virtual void ReleaseAnimBuffer(const Rect &update_rect) {}

	/* --- Rendering --- */

	/** Update the palette. */
	virtual void UpdatePalette(const Colour *pal, uint first, uint length) = 0;

	/** Render the frame (blit CPU buffer to screen, apply post-processing). */
	virtual void Paint() = 0;

	/* --- Cursor --- */

	/** Draw the mouse cursor. */
	virtual void DrawMouseCursor() {}

	/** Cache system cursor sprites. */
	virtual void PopulateCursorCache() {}

	/** Clear the cursor cache. */
	virtual void ClearCursorCache() {}

	/* --- Post-processing --- */

	/** Set the post-processing configuration. */
	virtual void SetPostProcessConfig(const PostProcessConfig &config) {}

	/** Check if post-processing is supported. */
	virtual bool IsPostProcessSupported() const { return false; }

	/* --- Motion vectors --- */

	/** Submit draw commands for motion vector generation. */
	virtual void SubmitDrawCommands(const std::vector<DrawCommand> &cmds,
	                                 int16_t scroll_dx, int16_t scroll_dy) {}

	/* --- External upscaler plugin --- */

	/**
	 * Load an external upscaling plugin (DLSS).
	 * @param path Path to the plugin shared library.
	 * @return True if the plugin was loaded successfully.
	 */
	virtual bool LoadUpscalePlugin(const std::string &path) { return false; }

	/** Get the currently active upscaling technology. */
	virtual UpscaleTechnology GetActiveUpscaler() const { return UpscaleTechnology::None; }

	/* --- Info --- */

	/** Get a human-readable driver info string. */
	virtual std::string GetDriverName() const = 0;
};

/**
 * Select the best available render backend for the current platform.
 * Tries Vulkan first, then falls back to OpenGL.
 * @return A new render backend instance, or nullptr on failure.
 */
std::unique_ptr<RenderBackend> CreateBestRenderBackend();

#endif /* VIDEO_RENDER_BACKEND_H */
