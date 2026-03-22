/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file pp_screenshot.cpp Post-processing pipeline screenshot capture via glReadPixels. */

#include "../stdafx.h"
#include "pp_screenshot.h"
#include "video_driver.hpp"
#include "../debug.h"
#include "../fileio_func.h"

#include <fstream>
#include <string>
#include <vector>

#include "../safeguards.h"

/** Write a little-endian 32-bit value to a buffer. */
static inline void WriteLE32(uint8_t *buf, int32_t val)
{
	buf[0] = static_cast<uint8_t>(val);
	buf[1] = static_cast<uint8_t>(val >> 8);
	buf[2] = static_cast<uint8_t>(val >> 16);
	buf[3] = static_cast<uint8_t>(val >> 24);
}

/** Write a little-endian 16-bit value to a buffer. */
static inline void WriteLE16(uint8_t *buf, int16_t val)
{
	buf[0] = static_cast<uint8_t>(val);
	buf[1] = static_cast<uint8_t>(val >> 8);
}

/** Queued screenshot with its settings snapshot. */
struct PPScreenshotRequest {
	std::string filename;
	PPSettingsSnapshot settings;
};

static std::vector<PPScreenshotRequest> _pending_pp_screenshots;
static PPPixelReaderFunc _pp_pixel_reader = nullptr;

void SetPPPixelReader(PPPixelReaderFunc reader)
{
	_pp_pixel_reader = reader;
}

/**
 * Capture all _video_* globals into a PPSettingsSnapshot.
 * This mirrors the config sync block in OpenGLBackend::Paint() so that
 * every post-processing parameter is preserved for deferred screenshots.
 * @return Snapshot of the current post-processing state.
 */
PPSettingsSnapshot CapturePPSettings()
{
	PPSettingsSnapshot s;
	s.post_processing = _video_post_processing;
	s.texture_filter = _video_texture_filter;

	PostProcessConfig &c = s.config;

	/* Core upscaling. */
	c.render_scale = _video_render_scale;
	c.upscale_mode = static_cast<UpscaleMode>(Clamp<uint8_t>(_video_upscale_mode, 0, static_cast<uint8_t>(UpscaleMode::Plugin)));
	c.sharpening = _video_sharpening;
	c.bilinear_filtering = (_video_texture_filter >= 1);
	c.bicubic_filtering = (_video_texture_filter == 2);

	/* Anti-aliasing. */
	c.fxaa = _video_fxaa;
	c.fxaa_quality = _video_fxaa_quality;
	c.fxaa_threshold = _video_fxaa_threshold;

	/* Effect toggles. */
	c.night_mode = _video_night_mode;
	c.crt_filter = _video_crt_filter;
	c.vignette = _video_vignette;
	c.tiltshift = _video_tiltshift;
	c.film_grain = _video_film_grain;

	/* Color grading. */
	c.cg_brightness = _video_brightness;
	c.cg_contrast = _video_contrast;
	c.cg_saturation = _video_saturation;
	c.cg_temperature = _video_color_temperature;
	c.color_grading = (_video_brightness != 0 || _video_contrast != 100 ||
	                   _video_saturation != 100 || _video_color_temperature != 0);

	/* Night mode sub-parameters. */
	c.night_intensity = _video_night_intensity;
	c.night_blue_shift = _video_night_blue_shift;

	/* CRT sub-parameters. */
	c.crt_scanlines = _video_crt_scanlines;
	c.crt_curvature = _video_crt_curvature;
	c.crt_aberration = _video_crt_aberration;

	/* Vignette sub-parameters. */
	c.vignette_intensity = _video_vignette_intensity;
	c.vignette_radius = _video_vignette_radius;
	c.vignette_softness = _video_vignette_softness;

	/* Tilt-shift sub-parameters. */
	c.tiltshift_focus_y = _video_tiltshift_focus_y;
	c.tiltshift_focus_width = _video_tiltshift_focus_width;
	c.tiltshift_blur = _video_tiltshift_blur;

	/* Film grain sub-parameter. */
	c.grain_intensity = _video_grain_intensity;

	/* Dynamic lighting. */
	c.dynamic_lighting = _video_dynamic_lighting;

	/* Bloom. */
	c.bloom = _video_bloom;
	c.bloom_threshold = _video_bloom_threshold;
	c.bloom_intensity = _video_bloom_intensity;

	/* Weather. */
	c.weather_type = _video_weather_type;
	c.weather_intensity = _video_weather_intensity;

	/* Pixel art smoothing. */
	c.pixel_smoothing = _video_pixel_smoothing;
	c.pixel_smooth_amount = _video_pixel_smooth_amount;

	/* Auto-supersample. */
	c.auto_supersample = _video_auto_supersample;

	/* Fake directional shadows. */
	c.fake_shadows = _video_fake_shadows;
	c.shadow_intensity = _video_shadow_intensity;
	c.shadow_angle = _video_shadow_angle;
	c.shadow_length = _video_shadow_length;
	c.shadow_softness = _video_shadow_softness;

	/* Water reflections. */
	c.water_reflections = _video_water_reflections;
	c.reflection_intensity = _video_reflection_intensity;
	c.reflection_distortion = _video_reflection_distortion;

	/* Screen-space ambient occlusion. */
	c.ssao = _video_ssao;
	c.ssao_radius = _video_ssao_radius;
	c.ssao_intensity = _video_ssao_intensity;
	c.ssao_samples = _video_ssao_samples;

	/* Terrain transition smoothing. */
	c.terrain_smooth = _video_terrain_smooth;
	c.terrain_smooth_radius = _video_terrain_smooth_radius;
	c.terrain_smooth_strength = _video_terrain_smooth_strength;

	/* Animated tree sway. */
	c.tree_sway = _video_tree_sway;
	c.tree_sway_amount = _video_tree_sway_amount;
	c.tree_sway_speed = _video_tree_sway_speed;

	/* Procedural sky with clouds. */
	c.sky_clouds = _video_sky_clouds;
	c.cloud_density = _video_cloud_density;
	c.cloud_speed = _video_cloud_speed;
	c.sky_brightness = _video_sky_brightness;

	/* Depth-of-field blur. */
	c.depth_of_field = _video_depth_of_field;
	c.dof_focus_point = _video_dof_focus_point;
	c.dof_aperture = _video_dof_aperture;
	c.dof_range = _video_dof_range;

	return s;
}

/**
 * Restore all _video_* globals from a PPSettingsSnapshot.
 * This is the inverse of CapturePPSettings() and writes every field back
 * to the corresponding global so the next Paint() config sync picks them up.
 * @param s Snapshot to restore.
 */
void RestorePPSettings(const PPSettingsSnapshot &s)
{
	_video_post_processing = s.post_processing;
	_video_texture_filter = s.texture_filter;

	const PostProcessConfig &c = s.config;

	/* Core upscaling. */
	_video_render_scale = c.render_scale;
	_video_upscale_mode = static_cast<uint8_t>(c.upscale_mode);
	_video_sharpening = c.sharpening;

	/* Anti-aliasing. */
	_video_fxaa = c.fxaa;
	_video_fxaa_quality = c.fxaa_quality;
	_video_fxaa_threshold = c.fxaa_threshold;

	/* Effect toggles. */
	_video_night_mode = c.night_mode;
	_video_crt_filter = c.crt_filter;
	_video_vignette = c.vignette;
	_video_tiltshift = c.tiltshift;
	_video_film_grain = c.film_grain;

	/* Color grading. */
	_video_brightness = c.cg_brightness;
	_video_contrast = c.cg_contrast;
	_video_saturation = c.cg_saturation;
	_video_color_temperature = c.cg_temperature;

	/* Night mode sub-parameters. */
	_video_night_intensity = c.night_intensity;
	_video_night_blue_shift = c.night_blue_shift;

	/* CRT sub-parameters. */
	_video_crt_scanlines = c.crt_scanlines;
	_video_crt_curvature = c.crt_curvature;
	_video_crt_aberration = c.crt_aberration;

	/* Vignette sub-parameters. */
	_video_vignette_intensity = c.vignette_intensity;
	_video_vignette_radius = c.vignette_radius;
	_video_vignette_softness = c.vignette_softness;

	/* Tilt-shift sub-parameters. */
	_video_tiltshift_focus_y = c.tiltshift_focus_y;
	_video_tiltshift_focus_width = c.tiltshift_focus_width;
	_video_tiltshift_blur = c.tiltshift_blur;

	/* Film grain sub-parameter. */
	_video_grain_intensity = c.grain_intensity;

	/* Dynamic lighting. */
	_video_dynamic_lighting = c.dynamic_lighting;

	/* Bloom. */
	_video_bloom = c.bloom;
	_video_bloom_threshold = c.bloom_threshold;
	_video_bloom_intensity = c.bloom_intensity;

	/* Weather. */
	_video_weather_type = c.weather_type;
	_video_weather_intensity = c.weather_intensity;

	/* Pixel art smoothing. */
	_video_pixel_smoothing = c.pixel_smoothing;
	_video_pixel_smooth_amount = c.pixel_smooth_amount;

	/* Auto-supersample. */
	_video_auto_supersample = c.auto_supersample;

	/* Fake directional shadows. */
	_video_fake_shadows = c.fake_shadows;
	_video_shadow_intensity = c.shadow_intensity;
	_video_shadow_angle = c.shadow_angle;
	_video_shadow_length = c.shadow_length;
	_video_shadow_softness = c.shadow_softness;

	/* Water reflections. */
	_video_water_reflections = c.water_reflections;
	_video_reflection_intensity = c.reflection_intensity;
	_video_reflection_distortion = c.reflection_distortion;

	/* Screen-space ambient occlusion. */
	_video_ssao = c.ssao;
	_video_ssao_radius = c.ssao_radius;
	_video_ssao_intensity = c.ssao_intensity;
	_video_ssao_samples = c.ssao_samples;

	/* Terrain transition smoothing. */
	_video_terrain_smooth = c.terrain_smooth;
	_video_terrain_smooth_radius = c.terrain_smooth_radius;
	_video_terrain_smooth_strength = c.terrain_smooth_strength;

	/* Animated tree sway. */
	_video_tree_sway = c.tree_sway;
	_video_tree_sway_amount = c.tree_sway_amount;
	_video_tree_sway_speed = c.tree_sway_speed;

	/* Procedural sky with clouds. */
	_video_sky_clouds = c.sky_clouds;
	_video_cloud_density = c.cloud_density;
	_video_cloud_speed = c.cloud_speed;
	_video_sky_brightness = c.sky_brightness;

	/* Depth-of-field blur. */
	_video_depth_of_field = c.depth_of_field;
	_video_dof_focus_point = c.dof_focus_point;
	_video_dof_aperture = c.dof_aperture;
	_video_dof_range = c.dof_range;
}

void RequestPPScreenshot(const std::string &filename)
{
	PPScreenshotRequest req;
	req.filename = filename;
	req.settings = CapturePPSettings();
	_pending_pp_screenshots.push_back(std::move(req));
	Debug(misc, 0, "PP screenshot requested: {} (queue: {})", filename, _pending_pp_screenshots.size());
}

/**
 * Check if there are pending PP screenshots waiting to be captured.
 * Used by the exit handler to defer shutdown until all queued
 * screenshots have been processed (one per paint frame).
 * @return True if the queue is non-empty.
 */
bool HasPendingPPScreenshots()
{
	return !_pending_pp_screenshots.empty();
}

size_t GetPendingPPScreenshotCount()
{
	return _pending_pp_screenshots.size();
}

/**
 * Write a BMP file from raw RGBA pixel data.
 * @param filename Output path.
 * @param data RGBA pixel data (bottom-up).
 * @param width Image width.
 * @param height Image height.
 * @return True on success.
 */
static bool WriteBMP(const std::string &filename, const std::vector<uint8_t> &data, int width, int height)
{
	std::ofstream file(filename, std::ios::binary);
	if (!file.is_open()) return false;

	/* Use int64 to prevent overflow at high resolutions (e.g., 8K supersampled). */
	int64_t row_size = static_cast<int64_t>(width) * 3;
	int padding = static_cast<int>((4 - (row_size % 4)) % 4);
	int64_t padded_row = row_size + padding;
	int64_t pixel_data_size = padded_row * height;
	int64_t file_size = 54 + pixel_data_size;

	/* Sanity: reject if file would be > 2GB (BMP header uses int32). */
	if (file_size > INT32_MAX) return false;

	/* BMP Header. */
	uint8_t header[54] = {};
	header[0] = 'B'; header[1] = 'M';
	WriteLE32(&header[2], static_cast<int32_t>(file_size));
	WriteLE32(&header[10], 54); /* data offset */
	WriteLE32(&header[14], 40); /* info header size */
	WriteLE32(&header[18], width);
	WriteLE32(&header[22], height);
	WriteLE16(&header[26], 1);  /* planes */
	WriteLE16(&header[28], 24); /* bits per pixel */
	WriteLE32(&header[34], static_cast<int32_t>(pixel_data_size));

	file.write(reinterpret_cast<const char *>(header), 54);

	/* Write pixel rows (BMP is bottom-up, our data is already bottom-up from glReadPixels). */
	uint8_t pad_bytes[3] = {};
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			size_t src_idx = (static_cast<size_t>(y) * width + x) * 4;
			/* BMP uses BGR, OpenGL gives RGBA. */
			uint8_t bgr[3] = { data[src_idx + 2], data[src_idx + 1], data[src_idx] };
			file.write(reinterpret_cast<const char *>(bgr), 3);
		}
		if (padding > 0) file.write(reinterpret_cast<const char *>(pad_bytes), padding);
	}

	file.close();
	return true;
}

/**
 * Capture the current framebuffer if a screenshot was requested.
 * Must be called after all rendering (including cursor) is complete,
 * while the framebuffer is still bound.
 * @param width Current framebuffer width.
 * @param height Current framebuffer height.
 */
void ApplyNextPPScreenshotSettings()
{
	if (_pending_pp_screenshots.empty()) return;
	RestorePPSettings(_pending_pp_screenshots.front().settings);
}

void CapturePPScreenshotIfPending(int width, int height)
{
	if (_pending_pp_screenshots.empty()) return;

	/* Dequeue one screenshot per frame. Capture whatever the current
	 * frame rendered -- script commands set effects before each capture. */
	PPScreenshotRequest req = std::move(_pending_pp_screenshots.front());
	_pending_pp_screenshots.erase(_pending_pp_screenshots.begin());
	/* Settings were already applied at Paint() start via ApplyNextPPScreenshotSettings(). */
	std::string basename = req.filename;

	/* Sanitize filename: strip path separators and special characters. */
	std::string safe_name;
	for (char c : basename) {
		if (c != '/' && c != '\\' && c != ':' && c != '*' && c != '?' && c != '<' && c != '>' && c != '|') {
			safe_name += c;
		}
	}
	if (safe_name.empty()) safe_name = "pp_screenshot";

	/* Save to the screenshot directory. */
	std::string filename = fmt::format("{}{}.bmp", FiosGetScreenshotDir(), safe_name);

	if (width <= 0 || height <= 0) {
		Debug(misc, 0, "PP screenshot failed: invalid dimensions {}x{}", width, height);
		return;
	}

	/* Read pixels from the current framebuffer. */
	std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);

	/* We need glReadPixels -- it's a GL 1.0 function, should be loaded. */
	/* Since we can't call it directly without the function pointer, we'll
	 * use the OpenTTD GL function loading mechanism. For now, use the
	 * standard approach -- glReadPixels is always available in any GL context. */

	/* Note: This function is called from the video driver Paint() context
	 * where we have a valid GL context. glReadPixels reads from the currently
	 * bound read framebuffer (default = screen). */

	Debug(misc, 0, "PP screenshot: capturing {}x{} to {}", width, height, filename);

	/* The actual GL call would be:
	 * glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	 * But we need the function pointer via the OpenTTD loader.
	 * For now, mark this as needing the GL function pointer to be wired in. */

	/* Call the registered pixel reader function. */
	if (_pp_pixel_reader == nullptr) {
		Debug(misc, 0, "PP screenshot failed: no pixel reader registered");
		return;
	}
	_pp_pixel_reader(0, 0, width, height, pixels.data());

	if (WriteBMP(filename, pixels, width, height)) {
		Debug(misc, 0, "PP screenshot saved: {}", filename);
	} else {
		Debug(misc, 0, "PP screenshot failed to write: {}", filename);
	}
}
