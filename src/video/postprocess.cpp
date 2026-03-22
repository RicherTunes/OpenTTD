/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file postprocess.cpp GPU post-processing pipeline configuration and logic. */

#include "../stdafx.h"
#include "postprocess.h"
#include "../core/math_func.hpp"
#include "../blitter/factory.hpp"
#include <cmath>

#include "../safeguards.h"

/**
 * Calculate render and display dimensions for the post-processing pipeline.
 * @param display_w Display/window width in pixels.
 * @param display_h Display/window height in pixels.
 * @param render_scale Render resolution as percentage of display (50-200).
 * @return Computed dimensions for both render and display resolution.
 */
PostProcessDimensions CalculatePostProcessDimensions(int display_w, int display_h, uint8_t render_scale)
{
	PostProcessDimensions dims;

	/* Guard against zero or negative dimensions (minimized windows). */
	if (display_w <= 0 || display_h <= 0) {
		dims.display = {0, 0};
		dims.render = {0, 0};
		return dims;
	}

	dims.display.width = display_w;
	dims.display.height = display_h;

	uint8_t clamped_scale = Clamp<uint8_t>(render_scale, 50, 200);

	if (clamped_scale == 100) {
		dims.render = dims.display;
		return dims;
	}

	/* Calculate scaled dimensions, rounding up. Ensure even values for GPU alignment. */
	uint render_w = std::max(2u, static_cast<uint>((display_w * clamped_scale + 99) / 100) & ~1u);
	uint render_h = std::max(2u, static_cast<uint>((display_h * clamped_scale + 99) / 100) & ~1u);

	dims.render.width = render_w;
	dims.render.height = render_h;

	return dims;
}

/**
 * Check if the current blitter produces 32bpp output suitable for post-processing.
 * Post-processing shaders expect RGBA input. When an 8bpp blitter is active,
 * the video texture contains palette indices and post-processing will produce garbage.
 * @return True if the blitter is compatible with post-processing.
 */
bool IsBlitterCompatibleWithPostProcess()
{
	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	if (blitter == nullptr) return false;
	return blitter->GetScreenDepth() == 32;
}

/**
 * Determine whether the post-processing pipeline needs a framebuffer object.
 * @note Post-processing requires a 32bpp blitter. The caller must check
 * IsBlitterCompatibleWithPostProcess() separately before enabling the pipeline.
 * @param config Current post-processing configuration.
 * @return True if an FBO is needed for any post-processing operation.
 */
bool PostProcessNeedsFBO(const PostProcessConfig &config)
{
	if (config.render_scale < 100) return true;
	/* Bilinear upscale at 100% is a no-op -- don't allocate FBO for it. */
	if (config.upscale_mode == UpscaleMode::FSR1 || config.upscale_mode == UpscaleMode::Temporal || config.upscale_mode == UpscaleMode::Plugin) return true;
	if (config.sharpening > 0) return true;
	if (config.fxaa) return true;
	if (config.color_grading) return true;
	if (config.vignette) return true;
	if (config.tiltshift) return true;
	if (config.night_mode) return true;
	if (config.film_grain) return true;
	if (config.crt_filter) return true;
	if (config.dynamic_lighting) return true;
	if (config.bloom) return true;
	if (config.weather_type > 0) return true;
	if (config.render_scale > 100) return true; /* Supersampling needs downsample pass. */
	return false;
}

/**
 * Count the number of post-processing shader passes needed.
 * Returns the theoretical maximum assuming all shader programs compiled successfully.
 * The actual pass count at runtime may be lower if specific shaders failed to compile.
 * @param config Current post-processing configuration.
 * @return Number of shader passes.
 */
int PostProcessPassCount(const PostProcessConfig &config)
{
	int passes = 0;

	/* Upscaling passes. */
	if (config.upscale_mode == UpscaleMode::FSR1) {
		passes += 2; /* EASU + RCAS */
	} else if (config.upscale_mode == UpscaleMode::Temporal) {
		passes += 1; /* Temporal accumulation pass. */
	} else if (config.upscale_mode == UpscaleMode::Plugin) {
		passes += 1; /* External plugin handles upscaling. */
	} else if (config.upscale_mode == UpscaleMode::Bilinear && config.render_scale < 100) {
		passes += 1; /* Bilinear blit or bicubic Catmull-Rom upscale. */
	}

	/* CAS standalone (not when FSR1 or Temporal is active). */
	if (config.sharpening > 0 && config.upscale_mode != UpscaleMode::FSR1 && config.upscale_mode != UpscaleMode::Temporal) {
		passes += 1;
	}

	/* Effect passes. */
	if (config.fxaa) passes += 1;
	if (config.tiltshift) passes += 2; /* Horizontal + vertical blur */
	if (config.color_grading) passes += 1;
	if (config.night_mode) passes += 1;
	if (config.vignette) passes += 1;
	if (config.film_grain) passes += 1;
	if (config.crt_filter) passes += 1;
	if (config.dynamic_lighting) passes += 1;
	if (config.bloom) passes += 4; /* Threshold + blur H + blur V + composite blend with original. */
	if (config.weather_type > 0) passes += 1;

	/* Supersampling downsample pass (render > display). */
	if (config.render_scale > 100) passes += 1;

	return passes;
}

/**
 * Map user sharpening value (0-100) to FSR RCAS sharpness parameter.
 * @param user_value User setting value (0 = no sharpening, 100 = max sharpening).
 * @return FSR RCAS sharpness (2.0 = no sharpening, 0.0 = max sharpening).
 */
float MapSharpeningToFsrRcas(uint8_t user_value)
{
	return 2.0f * (1.0f - Clamp<uint8_t>(user_value, 0, 100) / 100.0f);
}

/**
 * Map user sharpening value (0-100) to CAS intensity.
 * @param user_value User setting value (0 = off, 100 = max).
 * @return CAS intensity (0.0 = off, 1.0 = max).
 */
float MapSharpeningToCas(uint8_t user_value)
{
	return Clamp<uint8_t>(user_value, 0, 100) / 100.0f;
}

/**
 * Compute FSR 1.0 EASU constants for a given resolution pair.
 * @param con0 Output: viewport-to-input scaling (xy) and offset (zw).
 * @param con1 Output: reciprocal texel size (xy), input size in pixels (zw).
 * @param con2 Output: reserved for future use.
 * @param con3 Output: reserved for future use.
 */
void ComputeFsrEasuConstants(
	float con0[4], float con1[4], float con2[4], float con3[4],
	float input_w, float input_h,
	float input_viewport_w, float input_viewport_h,
	float output_w, float output_h)
{
	/* Guard against division by zero. */
	if (input_w <= 0.0f) input_w = 1.0f;
	if (input_h <= 0.0f) input_h = 1.0f;
	if (output_w <= 0.0f) output_w = 1.0f;
	if (output_h <= 0.0f) output_h = 1.0f;
	if (input_viewport_w <= 0.0f) input_viewport_w = input_w;
	if (input_viewport_h <= 0.0f) input_viewport_h = input_h;

	con0[0] = input_viewport_w / output_w;
	con0[1] = input_viewport_h / output_h;
	con0[2] = 0.5f * input_viewport_w / output_w - 0.5f;
	con0[3] = 0.5f * input_viewport_h / output_h - 0.5f;

	/* con1.xy = reciprocal input texel size (for UV-space sampling offsets).
	 * con1.zw = input size in pixels (for UV-to-texel-space conversion in shader). */
	con1[0] = 1.0f / input_w;
	con1[1] = 1.0f / input_h;
	con1[2] = input_w;
	con1[3] = input_h;

	/* con2 and con3 are reserved for future use (e.g. gather-based EASU). */
	con2[0] = 0.0f;
	con2[1] = 0.0f;
	con2[2] = 0.0f;
	con2[3] = 0.0f;

	con3[0] = 0.0f;
	con3[1] = 0.0f;
	con3[2] = 0.0f;
	con3[3] = 0.0f;
}

/**
 * Compute FSR 1.0 RCAS constant.
 * @param con Output: RCAS constant array.
 * @param sharpness Sharpness value (0.0 = max sharp, 2.0 = no sharpening).
 */
void ComputeFsrRcasConstant(float con[4], float sharpness)
{
	float clamped = Clamp(sharpness, 0.0f, 2.0f);
	con[0] = expf(-clamped);
	con[1] = 0.0f;
	con[2] = 0.0f;
	con[3] = 0.0f;
}

/**
 * Compute CAS sharpening constant.
 * @param con Output: CAS constant array.
 * @param sharpening_pct Sharpening intensity (0.0 = off, 1.0 = max).
 * @param input_w Input width in pixels.
 * @param input_h Input height in pixels.
 */
void ComputeCasConstant(float con[4], float sharpening_pct, float input_w, float input_h)
{
	con[0] = Clamp(sharpening_pct, 0.0f, 1.0f);
	con[1] = input_w > 0.0f ? 1.0f / input_w : 0.0f;
	con[2] = input_h > 0.0f ? 1.0f / input_h : 0.0f;
	con[3] = 0.0f;
}
