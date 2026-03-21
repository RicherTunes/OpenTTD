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

/**
 * Request a screenshot of the current framebuffer after post-processing.
 * The screenshot is captured at the end of the next Paint() call.
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

#endif /* VIDEO_PP_SCREENSHOT_H */
