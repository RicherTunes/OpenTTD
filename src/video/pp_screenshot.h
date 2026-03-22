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
#include "postprocess.h"

/**
 * Snapshot of all post-processing settings at the time of screenshot request.
 * Uses PostProcessConfig directly so every field is captured without manual sync.
 * The extra fields (post_processing, texture_filter) track the master toggle
 * and raw texture filter global that live outside PostProcessConfig.
 */
struct PPSettingsSnapshot {
	bool post_processing;          ///< Master PP toggle (_video_post_processing).
	uint8_t texture_filter;        ///< Raw texture filter global (_video_texture_filter).
	PostProcessConfig config;      ///< Full post-processing configuration.
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
 * Check if there are pending PP screenshots waiting to be captured.
 * @return True if the queue is non-empty.
 */
bool HasPendingPPScreenshots();

/** Return the number of queued PP screenshots waiting to be captured. */
size_t GetPendingPPScreenshotCount();

/**
 * Advance the PP-active frame counter used for screenshot warmup gating.
 * When PP is inactive the counter resets to zero.
 * @param pp_this_frame Whether PP rendered this frame.
 * @param previous_frames Previous consecutive PP-active frame count.
 * @return Updated consecutive PP-active frame count.
 */
int GetNextPPActiveFrameCount(bool pp_this_frame, int previous_frames);

/**
 * Decide whether a queued PP screenshot can be captured this frame.
 * PP-on captures intentionally wait until the second consecutive active frame
 * so the FBO topology has had one full render cycle to populate.
 * PP-off captures can happen immediately.
 * @param has_pending Whether the screenshot queue is non-empty.
 * @param pp_this_frame Whether PP rendered this frame.
 * @param pp_active_frames Current consecutive PP-active frame count.
 * @return True if a capture should proceed this frame.
 */
bool ShouldCapturePPScreenshotThisFrame(bool has_pending, bool pp_this_frame, int pp_active_frames);

/**
 * If screenshots are queued, restore the next screenshot's settings.
 * Call this at the START of Paint() before config sync.
 * This ensures the PP pipeline renders with the correct settings
 * for each queued screenshot.
 */
void ApplyNextPPScreenshotSettings();

#endif /* VIDEO_PP_SCREENSHOT_H */
