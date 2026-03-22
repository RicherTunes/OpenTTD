/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file pp_screenshot.h Post-processing pipeline screenshot capture. */

#ifndef VIDEO_PP_SCREENSHOT_H
#define VIDEO_PP_SCREENSHOT_H

#include <string>
#include <cstdint>

/** Snapshot of video settings at the time of screenshot request. */
struct PPSettingsSnapshot {
	bool post_processing;
	uint8_t render_scale;
	uint8_t upscale_mode;
	uint8_t sharpening;
	uint8_t texture_filter;
	bool fxaa;
	bool night_mode;
	bool crt_filter;
	bool vignette;
	bool tiltshift;
	bool film_grain;
	int8_t brightness;
	uint8_t contrast;
	uint8_t saturation;
	int8_t color_temperature;
	uint8_t night_intensity;
	uint8_t night_blue_shift;
	uint8_t crt_scanlines;
	uint8_t crt_curvature;
	uint8_t crt_aberration;
	uint8_t vignette_intensity;
	uint8_t vignette_radius;
	uint8_t tiltshift_focus_y;
	uint8_t tiltshift_focus_width;
	uint8_t tiltshift_blur;
	uint8_t grain_intensity;
};

/** Capture current _video_* globals into a snapshot. */
PPSettingsSnapshot CapturePPSettings();

/** Restore _video_* globals from a snapshot. */
void RestorePPSettings(const PPSettingsSnapshot &snap);

/**
 * Request a screenshot with a settings snapshot.
 * The settings are restored before rendering each queued frame.
 * @param filename Output filename (without extension, .bmp added automatically).
 */
void RequestPPScreenshot(const std::string &filename);

/** Callback type for reading pixels from the framebuffer. */
using PPPixelReaderFunc = void (*)(int x, int y, int width, int height, void *data);

/** Register the pixel reader function (called by the OpenGL backend). */
void SetPPPixelReader(PPPixelReaderFunc reader);

/**
 * Check if a PP screenshot is pending and capture it if so.
 * Called from Paint() after all rendering is complete.
 * Reads pixels from the current framebuffer via glReadPixels.
 * @param width Framebuffer width.
 * @param height Framebuffer height.
 */
void CapturePPScreenshotIfPending(int width, int height);

/**
 * If screenshots are queued, restore the next screenshot's settings.
 * Call this at the START of Paint() before config sync.
 * This ensures the PP pipeline renders with the correct settings
 * for each queued screenshot.
 */
void ApplyNextPPScreenshotSettings();

#endif /* VIDEO_PP_SCREENSHOT_H */
