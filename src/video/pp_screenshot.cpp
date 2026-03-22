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
 * Request a screenshot after the next Paint().
 * Multiple requests are queued and each captures on a separate frame.
 * @param filename Output filename (without extension).
 */
PPSettingsSnapshot CapturePPSettings()
{
	PPSettingsSnapshot s;
	s.post_processing = _video_post_processing;
	s.render_scale = _video_render_scale;
	s.upscale_mode = _video_upscale_mode;
	s.sharpening = _video_sharpening;
	s.texture_filter = _video_texture_filter;
	s.fxaa = _video_fxaa;
	s.night_mode = _video_night_mode;
	s.crt_filter = _video_crt_filter;
	s.vignette = _video_vignette;
	s.tiltshift = _video_tiltshift;
	s.film_grain = _video_film_grain;
	s.brightness = _video_brightness;
	s.contrast = _video_contrast;
	s.saturation = _video_saturation;
	s.color_temperature = _video_color_temperature;
	s.night_intensity = _video_night_intensity;
	s.night_blue_shift = _video_night_blue_shift;
	s.crt_scanlines = _video_crt_scanlines;
	s.crt_curvature = _video_crt_curvature;
	s.crt_aberration = _video_crt_aberration;
	s.vignette_intensity = _video_vignette_intensity;
	s.vignette_radius = _video_vignette_radius;
	s.tiltshift_focus_y = _video_tiltshift_focus_y;
	s.tiltshift_focus_width = _video_tiltshift_focus_width;
	s.tiltshift_blur = _video_tiltshift_blur;
	s.grain_intensity = _video_grain_intensity;
	return s;
}

void RestorePPSettings(const PPSettingsSnapshot &s)
{
	_video_post_processing = s.post_processing;
	_video_render_scale = s.render_scale;
	_video_upscale_mode = s.upscale_mode;
	_video_sharpening = s.sharpening;
	_video_texture_filter = s.texture_filter;
	_video_fxaa = s.fxaa;
	_video_night_mode = s.night_mode;
	_video_crt_filter = s.crt_filter;
	_video_vignette = s.vignette;
	_video_tiltshift = s.tiltshift;
	_video_film_grain = s.film_grain;
	_video_brightness = s.brightness;
	_video_contrast = s.contrast;
	_video_saturation = s.saturation;
	_video_color_temperature = s.color_temperature;
	_video_night_intensity = s.night_intensity;
	_video_night_blue_shift = s.night_blue_shift;
	_video_crt_scanlines = s.crt_scanlines;
	_video_crt_curvature = s.crt_curvature;
	_video_crt_aberration = s.crt_aberration;
	_video_vignette_intensity = s.vignette_intensity;
	_video_vignette_radius = s.vignette_radius;
	_video_tiltshift_focus_y = s.tiltshift_focus_y;
	_video_tiltshift_focus_width = s.tiltshift_focus_width;
	_video_tiltshift_blur = s.tiltshift_blur;
	_video_grain_intensity = s.grain_intensity;
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
			int src_idx = (y * width + x) * 4;
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

	/* Dequeue one screenshot per frame. Restore its settings snapshot
	 * so the PP pipeline renders with the correct effect state. */
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
	std::vector<uint8_t> pixels(width * height * 4);

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
