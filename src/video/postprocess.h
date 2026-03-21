/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file postprocess.h GPU post-processing pipeline configuration and logic. */

#ifndef VIDEO_POSTPROCESS_H
#define VIDEO_POSTPROCESS_H

#include "../core/geometry_type.hpp"

/** Upscale mode for post-processing pipeline. */
enum class UpscaleMode : uint8_t {
	None,      ///< No upscaling, render at display resolution.
	Bilinear,  ///< GL_LINEAR texture filtering for upscale.
	FSR1,      ///< AMD FSR 1.0 spatial upscaler (EASU + RCAS).
	Temporal,  ///< Temporal accumulation upscaling (TAA-style, uses motion vectors).
};

/** Configuration for the post-processing pipeline. */
struct PostProcessConfig {
	uint8_t render_scale = 100;        ///< Render resolution as percentage of display (50-200).
	UpscaleMode upscale_mode = UpscaleMode::None; ///< Upscaling algorithm.
	uint8_t sharpening = 0;            ///< CAS sharpening intensity (0-100). 0 = off.
	bool bilinear_filtering = false;   ///< Use GL_LINEAR instead of GL_NEAREST on framebuffer texture.
	bool bicubic_filtering = false;    ///< Use bicubic (Catmull-Rom) instead of bilinear for upscale texture filtering.
	bool fxaa = false;                 ///< Enable FXAA anti-aliasing.
	bool color_grading = false;        ///< Enable color grading.
	bool vignette = false;             ///< Enable vignette effect.
	bool tiltshift = false;            ///< Enable tilt-shift miniature effect.
	bool night_mode = false;           ///< Enable night mode.
	bool film_grain = false;           ///< Enable film grain.
	bool crt_filter = false;           ///< Enable CRT scanline filter.

	/* Color grading parameters. */
	int8_t cg_brightness = 0;          ///< Brightness offset (-50 to 50, mapped to -0.5..0.5).
	uint8_t cg_contrast = 100;         ///< Contrast (50-200, mapped to 0.5..2.0).
	uint8_t cg_saturation = 100;       ///< Saturation (0-200, mapped to 0.0..2.0).
	int8_t cg_temperature = 0;         ///< Color temperature (-100 to 100, mapped to -1.0..1.0).

	/* Vignette parameters. */
	uint8_t vignette_intensity = 30;   ///< Vignette darkness (0-100).
	uint8_t vignette_radius = 85;      ///< Vignette inner radius (50-150).

	/* Tilt-shift parameters. */
	uint8_t tiltshift_focus_y = 45;    ///< Focus band center (0-100, % from top).
	uint8_t tiltshift_focus_width = 25; ///< Focus band width (5-80, % of screen).
	uint8_t tiltshift_blur = 30;       ///< Blur intensity (10-60, scaled to shader units).

	/* Night mode parameters. */
	uint8_t night_intensity = 60;      ///< Darkness intensity (20-100).
	uint8_t night_blue_shift = 30;     ///< Blue tint strength (0-80).

	/* Film grain parameters. */
	uint8_t grain_intensity = 4;       ///< Grain intensity (1-20, mapped to 0.01..0.20).

	/* CRT parameters. */
	uint8_t crt_scanlines = 15;        ///< Scanline darkness (0-50, mapped to 0.0..0.5).
	uint8_t crt_curvature = 0;         ///< Screen curvature (0-50).
	uint8_t crt_aberration = 5;        ///< Chromatic aberration (0-30).

	/* Dynamic lighting (time-of-day). */
	bool dynamic_lighting = false;     ///< Enable time-of-day lighting cycle.
	float time_of_day = 0.5f;          ///< Current time (0.0 = midnight, 0.5 = noon, 1.0 = midnight).

	/* Bloom for lights. */
	bool bloom = false;                ///< Enable bloom glow on bright pixels.
	uint8_t bloom_threshold = 70;      ///< Luminance threshold (0-100, mapped to 0.0..1.0). Above this = bloom.
	uint8_t bloom_intensity = 30;      ///< Bloom blend strength (0-100).

	/* Weather effects. */
	uint8_t weather_type = 0;          ///< Weather overlay: 0=none, 1=rain, 2=snow.
	uint8_t weather_intensity = 30;    ///< Weather effect strength (0-100).

	bool operator==(const PostProcessConfig &) const = default;
};

/** Computed dimensions for the post-processing pipeline. */
struct PostProcessDimensions {
	Dimension display;  ///< Display/window resolution.
	Dimension render;   ///< Internal render resolution (may be smaller).
};

PostProcessDimensions CalculatePostProcessDimensions(int display_w, int display_h, uint8_t render_scale);
bool PostProcessNeedsFBO(const PostProcessConfig &config);
int PostProcessPassCount(const PostProcessConfig &config);
bool IsBlitterCompatibleWithPostProcess();

float MapSharpeningToFsrRcas(uint8_t user_value);
float MapSharpeningToCas(uint8_t user_value);

void ComputeFsrEasuConstants(
	float con0[4], float con1[4], float con2[4], float con3[4],
	float input_w, float input_h,
	float input_viewport_w, float input_viewport_h,
	float output_w, float output_h);

void ComputeFsrRcasConstant(float con[4], float sharpness);
void ComputeCasConstant(float con[4], float sharpening_pct, float input_w, float input_h);

#endif /* VIDEO_POSTPROCESS_H */
