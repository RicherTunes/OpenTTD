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
	Plugin,    ///< External plugin (DLSS, FSR 2/3) via C ABI.
};

/** Configuration for the post-processing pipeline. */
struct PostProcessConfig {
	uint8_t render_scale = 100;        ///< Render resolution as percentage of display (50-200).
	UpscaleMode upscale_mode = UpscaleMode::None; ///< Upscaling algorithm.
	uint8_t sharpening = 0;            ///< CAS sharpening intensity (0-100). 0 = off.
	bool bilinear_filtering = false;   ///< Use GL_LINEAR instead of GL_NEAREST on framebuffer texture.
	bool bicubic_filtering = false;    ///< Use bicubic (Catmull-Rom) instead of bilinear for upscale texture filtering.
	bool fxaa = false;                 ///< Enable FXAA anti-aliasing.
	uint8_t fxaa_quality = 75;         ///< FXAA sub-pixel quality (0-100, mapped to 0.0..1.0).
	uint8_t fxaa_threshold = 13;       ///< FXAA edge threshold (1-50, mapped to 0.01..0.50).
	bool color_grading = false;        ///< Enable color grading.
	bool vignette = false;             ///< Enable vignette effect.
	bool tiltshift = false;            ///< Enable tilt-shift miniature effect.
	bool night_mode = false;           ///< Enable night mode.
	bool film_grain = false;           ///< Enable film grain.
	bool crt_filter = false;           ///< Enable CRT scanline filter.
	bool pixel_smoothing = false;      ///< Enable pixel art smoothing at zoom-in levels.
	uint8_t pixel_smooth_amount = 70;  ///< Smoothing intensity (0-100, mapped to 0.0..1.0).

	/* Color grading parameters. */
	int8_t cg_brightness = 0;          ///< Brightness offset (-50 to 50, mapped to -0.5..0.5).
	uint8_t cg_contrast = 100;         ///< Contrast (50-200, mapped to 0.5..2.0).
	uint8_t cg_saturation = 100;       ///< Saturation (0-200, mapped to 0.0..2.0).
	int8_t cg_temperature = 0;         ///< Color temperature (-100 to 100, mapped to -1.0..1.0).

	/* Vignette parameters. */
	uint8_t vignette_intensity = 30;   ///< Vignette darkness (0-100).
	uint8_t vignette_radius = 85;      ///< Vignette inner radius (50-150).
	uint8_t vignette_softness = 45;    ///< Vignette feather softness (10-80, mapped to 0.1..0.8).

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

	/* Fake directional shadows. */
	bool fake_shadows = false;         ///< Enable fake directional shadows from buildings/terrain.
	uint8_t shadow_intensity = 40;     ///< Shadow darkness (0-100, mapped to 0.0..1.0).
	uint16_t shadow_angle = 45;        ///< Shadow angle in degrees (0-359). 0=right, 90=down.
	uint8_t shadow_length = 8;         ///< Shadow length in pixels (1-30).
	uint8_t shadow_softness = 3;       ///< Shadow edge softness (1-10, number of blur samples).

	/* Water reflections. */
	bool water_reflections = false;    ///< Enable screen-space water reflections.
	uint8_t reflection_intensity = 30; ///< Reflection strength (0-100, mapped to 0.0..1.0).
	uint8_t reflection_distortion = 5; ///< Wave distortion amplitude (0-20).

	/* Screen-space ambient occlusion. */
	bool ssao = false;                 ///< Enable screen-space ambient occlusion.
	uint8_t ssao_radius = 4;           ///< Sample radius in pixels (1-15).
	uint8_t ssao_intensity = 50;       ///< Occlusion darkness (0-100).
	uint8_t ssao_samples = 8;          ///< Number of samples per pixel (4-16).

	/* Terrain transition smoothing. */
	bool terrain_smooth = false;       ///< Enable terrain transition smoothing.
	uint8_t terrain_smooth_radius = 2; ///< Smoothing kernel radius (1-5).
	uint8_t terrain_smooth_strength = 50; ///< Smoothing strength (0-100).

	/* Animated tree/vegetation sway. */
	bool tree_sway = false;            ///< Enable animated tree/vegetation sway.
	uint8_t tree_sway_amount = 3;      ///< Sway amplitude in pixels (1-10).
	uint8_t tree_sway_speed = 50;      ///< Animation speed (10-100).

	/* Procedural sky with clouds. */
	bool sky_clouds = false;           ///< Enable procedural sky with animated clouds.
	uint8_t cloud_density = 50;        ///< Cloud coverage density (0-100).
	uint8_t cloud_speed = 30;          ///< Cloud drift speed (0-100).
	uint8_t sky_brightness = 70;       ///< Sky background brightness (0-100).

	/* Depth-of-field blur. */
	bool depth_of_field = false;       ///< Enable depth-of-field blur.
	uint8_t dof_focus_point = 50;      ///< Focus distance (0-100, % of depth range).
	uint8_t dof_aperture = 30;         ///< Aperture / blur strength (0-100).
	uint8_t dof_range = 40;            ///< Focus range width (0-100).

	bool auto_supersample = false;     ///< Automatically enable supersampling at zoom-in levels.

	/**
	 * Compare configurations for topology changes.
	 * Intentionally excludes time_of_day which changes every tick
	 * and should not trigger FBO rebuilds or temporal resets.
	 */
	bool operator==(const PostProcessConfig &o) const
	{
		/* Compare all fields except time_of_day. Any field that changes the
		 * shader topology or requires FBO rebuild should be compared here. */
		return this->render_scale == o.render_scale &&
			this->upscale_mode == o.upscale_mode &&
			this->sharpening == o.sharpening &&
			this->bilinear_filtering == o.bilinear_filtering &&
			this->bicubic_filtering == o.bicubic_filtering &&
			this->fxaa == o.fxaa &&
			this->fxaa_quality == o.fxaa_quality &&
			this->fxaa_threshold == o.fxaa_threshold &&
			this->color_grading == o.color_grading &&
			this->vignette == o.vignette &&
			this->tiltshift == o.tiltshift &&
			this->night_mode == o.night_mode &&
			this->film_grain == o.film_grain &&
			this->crt_filter == o.crt_filter &&
			this->pixel_smoothing == o.pixel_smoothing &&
			this->dynamic_lighting == o.dynamic_lighting &&
			this->bloom == o.bloom &&
			this->weather_type == o.weather_type &&
			this->fake_shadows == o.fake_shadows &&
			this->water_reflections == o.water_reflections &&
			this->auto_supersample == o.auto_supersample &&
			this->ssao == o.ssao &&
			this->terrain_smooth == o.terrain_smooth &&
			this->tree_sway == o.tree_sway &&
			this->sky_clouds == o.sky_clouds &&
			this->depth_of_field == o.depth_of_field;
	}
};

/** Computed dimensions for the post-processing pipeline. */
struct PostProcessDimensions {
	Dimension display;  ///< Display/window resolution.
	Dimension render;   ///< Internal render resolution (may be smaller or larger for supersampling).
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
