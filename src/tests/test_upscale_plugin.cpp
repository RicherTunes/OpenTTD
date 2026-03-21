/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file test_upscale_plugin.cpp Tests for upscaling plugin interface and render backend. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../video/upscale_plugin.h"
#include "../video/render_backend.h"

#include "../safeguards.h"

/* --- Plugin API struct tests --- */

TEST_CASE("UpscalePlugin - API version is defined")
{
	CHECK(UPSCALE_PLUGIN_API_VERSION == 1);
}

TEST_CASE("UpscalePlugin - UpscaleDispatchParams has correct defaults")
{
	UpscaleDispatchParams params = {};
	CHECK(params.jitter_x == 0.0f);
	CHECK(params.jitter_y == 0.0f);
	CHECK(params.delta_time == 0.0f);
	CHECK(params.reset == 0);
	CHECK(params.render_width == 0);
	CHECK(params.display_width == 0);
}

TEST_CASE("UpscalePlugin - Quality modes are ordered correctly")
{
	CHECK(UPSCALE_QUALITY_NATIVE_AA < UPSCALE_QUALITY_QUALITY);
	CHECK(UPSCALE_QUALITY_QUALITY < UPSCALE_QUALITY_BALANCED);
	CHECK(UPSCALE_QUALITY_BALANCED < UPSCALE_QUALITY_PERFORMANCE);
	CHECK(UPSCALE_QUALITY_PERFORMANCE < UPSCALE_QUALITY_ULTRA_PERF);
}

TEST_CASE("UpscalePlugin - Capability flags are distinct powers of 2")
{
	CHECK((UPSCALE_CAP_SUPER_RES & UPSCALE_CAP_FRAME_GEN) == 0);
	CHECK((UPSCALE_CAP_SUPER_RES & UPSCALE_CAP_RAY_RECON) == 0);
	CHECK((UPSCALE_CAP_OPENGL & UPSCALE_CAP_VULKAN) == 0);
	CHECK((UPSCALE_CAP_VULKAN & UPSCALE_CAP_DX11) == 0);
	CHECK((UPSCALE_CAP_DX11 & UPSCALE_CAP_DX12) == 0);
}

TEST_CASE("UpscalePlugin - No plugin loaded by default")
{
	CHECK(GetLoadedUpscalePlugin() == nullptr);
}

TEST_CASE("UpscalePlugin - Loading nonexistent plugin returns nullptr")
{
	auto *plugin = LoadUpscalePlugin("nonexistent_plugin_that_does_not_exist.dll");
	CHECK(plugin == nullptr);
	CHECK(GetLoadedUpscalePlugin() == nullptr);
}

/* --- RenderBackendCapabilities tests --- */

TEST_CASE("RenderBackend - Capabilities default to false")
{
	RenderBackendCapabilities caps;
	CHECK_FALSE(caps.supports_compute);
	CHECK_FALSE(caps.supports_fbo);
	CHECK_FALSE(caps.supports_temporal_upscale);
	CHECK_FALSE(caps.supports_dlss_plugin);
	CHECK_FALSE(caps.supports_fsr2);
	CHECK(caps.api_version_major == 0);
}

/* --- UpscaleTechnology enum tests --- */

TEST_CASE("RenderBackend - UpscaleTechnology values are distinct")
{
	CHECK(UpscaleTechnology::None != UpscaleTechnology::FSR2);
	CHECK(UpscaleTechnology::FSR2 != UpscaleTechnology::FSR3);
	CHECK(UpscaleTechnology::FSR3 != UpscaleTechnology::DLSS);
}

/* --- UpscaleTextureDesc layout test --- */

TEST_CASE("UpscalePlugin - TextureDesc zero-init")
{
	UpscaleTextureDesc desc = {};
	CHECK(desc.gl_texture_id == 0);
	CHECK(desc.native_handle == nullptr);
	CHECK(desc.width == 0);
	CHECK(desc.height == 0);
}

/* --- Plugin API struct completeness --- */

TEST_CASE("UpscalePlugin - API struct has all required function pointers")
{
	UpscalePluginAPI api = {};
	/* All function pointers should be null when zero-initialized. */
	CHECK(api.init == nullptr);
	CHECK(api.shutdown == nullptr);
	CHECK(api.evaluate == nullptr);
	CHECK(api.set_quality_mode == nullptr);
	CHECK(api.get_capabilities == nullptr);
	CHECK(api.get_name == nullptr);
	CHECK(api.get_version == nullptr);
	CHECK(api.get_render_resolution == nullptr);
}
