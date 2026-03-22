/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file pp_screenshot.cpp Post-processing pipeline screenshot capture via glReadPixels. */

#include "../stdafx.h"
#include "pp_screenshot.h"
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

/** Queued screenshot request. */
struct PPScreenshotRequest {
	std::string filename;
	uint8_t failed_attempts = 0;
};

static std::vector<PPScreenshotRequest> _pending_pp_screenshots;
static PPPixelReaderFunc _pp_pixel_reader = nullptr;
static int _pp_active_frames = 0; ///< Consecutive frames with PP active for warmup gating.
static size_t _pp_dropped_since_last_consume = 0;
static std::string _pp_last_failure_summary;
static size_t _pp_completed_total = 0;
static size_t _pp_dropped_total = 0;

static void DropNextPPScreenshot(std::string_view reason)
{
	if (_pending_pp_screenshots.empty()) return;

	const PPScreenshotRequest &req = _pending_pp_screenshots.front();
	_pp_dropped_since_last_consume++;
	_pp_dropped_total++;
	_pp_last_failure_summary = fmt::format("'{}' ({})", SanitizePPScreenshotBasename(req.filename), reason);
	Debug(misc, 0, "PP screenshot dropped after {} failed attempt(s): {} ({})", req.failed_attempts, req.filename, reason);
	_pending_pp_screenshots.erase(_pending_pp_screenshots.begin());
}

static bool HandleCaptureFailure(std::string_view reason)
{
	if (_pending_pp_screenshots.empty()) return false;

	PPScreenshotRequest &req = _pending_pp_screenshots.front();
	req.failed_attempts++;
	Debug(misc, 0, "PP screenshot capture failed (attempt {}/{}): {} ({})",
		req.failed_attempts, PP_SCREENSHOT_MAX_CAPTURE_ATTEMPTS, req.filename, reason);

	if (req.failed_attempts >= PP_SCREENSHOT_MAX_CAPTURE_ATTEMPTS) {
		DropNextPPScreenshot(reason);
	}
	return false;
}

void SetPPPixelReader(PPPixelReaderFunc reader)
{
	_pp_pixel_reader = reader;
}

void RequestPPScreenshot(const std::string &filename)
{
	_pending_pp_screenshots.push_back({filename, 0});
	Debug(misc, 0, "PP screenshot requested: {} (queue: {})", filename, _pending_pp_screenshots.size());
}

bool HasPendingPPScreenshots()
{
	return !_pending_pp_screenshots.empty();
}

size_t GetPendingPPScreenshotCount()
{
	return _pending_pp_screenshots.size();
}

void ClearPendingPPScreenshots()
{
	_pending_pp_screenshots.clear();
	ResetPPScreenshotWarmupState();
	_pp_dropped_since_last_consume = 0;
	_pp_last_failure_summary.clear();
	_pp_completed_total = 0;
	_pp_dropped_total = 0;
}

std::string SanitizePPScreenshotBasename(std::string_view basename)
{
	std::string safe_name;
	safe_name.reserve(basename.size());
	for (char c : basename) {
		if (c != '/' && c != '\\' && c != ':' && c != '*' && c != '?' && c != '<' && c != '>' && c != '|') {
			safe_name += c;
		}
	}
	if (safe_name.empty()) safe_name = "pp_screenshot";
	return safe_name;
}

int GetNextPPActiveFrameCount(bool pp_this_frame, int previous_frames)
{
	return pp_this_frame ? (previous_frames + 1) : 0;
}

bool ShouldCapturePPScreenshotThisFrame(bool has_pending, bool pp_this_frame, int pp_active_frames)
{
	if (!has_pending) return false;
	if (!pp_this_frame) return true;
	return pp_active_frames >= 2;
}

void ResetPPScreenshotWarmupState()
{
	_pp_active_frames = 0;
}

bool AdvanceAndShouldCapturePPScreenshotThisFrame(bool has_pending, bool pp_this_frame)
{
	_pp_active_frames = GetNextPPActiveFrameCount(pp_this_frame, _pp_active_frames);
	return ShouldCapturePPScreenshotThisFrame(has_pending, pp_this_frame, _pp_active_frames);
}

bool ConsumePPScreenshotFailureNotification(size_t &dropped_count, std::string &summary)
{
	if (_pp_dropped_since_last_consume == 0) return false;

	dropped_count = _pp_dropped_since_last_consume;
	summary = _pp_last_failure_summary;
	_pp_dropped_since_last_consume = 0;
	_pp_last_failure_summary.clear();
	return true;
}

void GetPPScreenshotOutcomeTotals(size_t &completed, size_t &dropped)
{
	completed = _pp_completed_total;
	dropped = _pp_dropped_total;
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

bool CapturePPScreenshotIfPending(int width, int height)
{
	if (_pending_pp_screenshots.empty()) return false;

	const PPScreenshotRequest &req = _pending_pp_screenshots.front();
	std::string safe_name = SanitizePPScreenshotBasename(req.filename);
	std::string filename = fmt::format("{}{}.bmp", FiosGetScreenshotDir(), safe_name);

	if (width <= 0 || height <= 0) {
		return HandleCaptureFailure(fmt::format("invalid dimensions {}x{}", width, height));
	}

	if (_pp_pixel_reader == nullptr) {
		return HandleCaptureFailure("no pixel reader registered");
	}

	std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);

	Debug(misc, 0, "PP screenshot: capturing {}x{} to {}", width, height, filename);
	_pp_pixel_reader(0, 0, width, height, pixels.data());

	if (!WriteBMP(filename, pixels, width, height)) {
		return HandleCaptureFailure(fmt::format("failed to write {}", filename));
	}

	_pending_pp_screenshots.erase(_pending_pp_screenshots.begin());
	_pp_completed_total++;
	Debug(misc, 0, "PP screenshot saved: {}", filename);
	return true;
}
