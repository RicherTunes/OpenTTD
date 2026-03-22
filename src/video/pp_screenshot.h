/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file pp_screenshot.h Post-processing pipeline screenshot capture. */

#ifndef VIDEO_PP_SCREENSHOT_H
#define VIDEO_PP_SCREENSHOT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/** Maximum number of times a queued PP screenshot will retry capture before being dropped. */
static constexpr uint8_t PP_SCREENSHOT_MAX_CAPTURE_ATTEMPTS = 3;

/**
 * Request a screenshot of the current rendered frame sequence.
 * @param filename Output filename (without extension, .bmp added automatically).
 */
void RequestPPScreenshot(const std::string &filename);

/** Callback type for reading pixels from the framebuffer. */
using PPPixelReaderFunc = void (*)(int x, int y, int width, int height, void *data);
/** Callback type for writing captured pixel data to disk or a test sink. */
using PPImageWriterFunc = bool (*)(const std::string &filename, const uint8_t *data, int width, int height);

/** Register the pixel reader function (called by the OpenGL backend). */
void SetPPPixelReader(PPPixelReaderFunc reader);
/** Register the image writer function (optional test seam; null uses the built-in BMP writer). */
void SetPPImageWriter(PPImageWriterFunc writer);

/**
 * Check if a PP screenshot is pending and capture it if so.
 * Called from Paint() after all rendering is complete.
 * Reads pixels from the current framebuffer via the registered reader.
 * Failed captures are retried up to PP_SCREENSHOT_MAX_CAPTURE_ATTEMPTS times.
 * @param width Framebuffer width.
 * @param height Framebuffer height.
 * @return True if a screenshot was successfully written and consumed.
 */
bool CapturePPScreenshotIfPending(int width, int height);

/**
 * Check if there are pending PP screenshots waiting to be captured.
 * @return True if the queue is non-empty.
 */
bool HasPendingPPScreenshots();

/** Return the number of queued PP screenshots waiting to be captured. */
size_t GetPendingPPScreenshotCount();

/** Clear all queued screenshots and reset internal warmup state. */
void ClearPendingPPScreenshots();

/** Sanitize a requested screenshot basename for safe filesystem use. */
std::string SanitizePPScreenshotBasename(std::string_view basename);

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

/** Reset the internal PP screenshot warmup state. */
void ResetPPScreenshotWarmupState();

/**
 * Advance warmup state and return whether capture should happen this frame.
 * This keeps renderer timing policy inside the screenshot subsystem.
 * @param has_pending Whether the screenshot queue is non-empty.
 * @param pp_this_frame Whether PP rendered this frame.
 * @return True if a queued screenshot should be captured now.
 */
bool AdvanceAndShouldCapturePPScreenshotThisFrame(bool has_pending, bool pp_this_frame);

/**
 * Consume any queued PP screenshot failure notification.
 * Failures are only reported after a request is dropped permanently.
 * @param dropped_count Receives the number of requests dropped since the last consume.
 * @param summary Receives a short human-readable summary of the latest failure.
 * @return True if a new failure notification was available.
 */
bool ConsumePPScreenshotFailureNotification(size_t &dropped_count, std::string &summary);

/**
 * Get cumulative PP screenshot queue outcomes since the last clear/reset.
 * @param completed Receives the number of successfully written screenshots.
 * @param dropped Receives the number of permanently dropped requests.
 */
void GetPPScreenshotOutcomeTotals(size_t &completed, size_t &dropped);

/**
 * Query the current PP screenshot queue state for UI/console reporting.
 * @param pending Receives the number of queued requests still waiting to capture.
 * @param completed Receives the number of successfully written screenshots.
 * @param dropped Receives the number of permanently dropped requests.
 * @param next_basename Receives the sanitized basename of the next queued request, or empty if none.
 */
void GetPPScreenshotQueueState(size_t &pending, size_t &completed, size_t &dropped, std::string &next_basename);

#endif /* VIDEO_PP_SCREENSHOT_H */
