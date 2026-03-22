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
	/* Wave 13: CPU viewport scaling requires FBO for two-pass compositing. */
	if (config.cpu_viewport_scaling) return true;
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
	if (config.pixel_smoothing) return true;
	if (config.dynamic_lighting) return true;
	if (config.bloom) return true;
	if (config.weather_type > 0) return true;
	if (config.fake_shadows) return true;
	if (config.water_reflections) return true;
	if (config.ssao) return true;
	if (config.terrain_smooth) return true;
	if (config.tree_sway) return true;
	if (config.sky_clouds) return true;
	if (config.depth_of_field) return true;
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

	/* Upscaling passes.
	 * FSR1 EASU only runs when render_scale < 100 (actual upscaling needed).
	 * At >= 100%, FSR1 mode falls back to CAS sharpening to avoid EASU blur. */
	if (config.upscale_mode == UpscaleMode::FSR1 && config.render_scale < 100) {
		passes += 2; /* EASU + RCAS */
	} else if (config.upscale_mode == UpscaleMode::FSR1 && config.render_scale >= 100) {
		if (config.sharpening > 0) passes += 1; /* CAS fallback for sharpening. */
	} else if (config.upscale_mode == UpscaleMode::Temporal) {
		passes += 1; /* Temporal accumulation pass. */
	} else if (config.upscale_mode == UpscaleMode::Plugin) {
		passes += 1; /* External plugin handles upscaling. */
	} else if (config.upscale_mode == UpscaleMode::Bilinear && config.render_scale < 100) {
		passes += 1; /* Bilinear blit or bicubic Catmull-Rom upscale. */
	}

	/* CAS standalone (not when FSR1, Temporal, or Plugin is active — those handle sharpening internally). */
	if (config.sharpening > 0 && config.upscale_mode != UpscaleMode::FSR1 && config.upscale_mode != UpscaleMode::Temporal && config.upscale_mode != UpscaleMode::Plugin) {
		passes += 1;
	}

	/* Effect passes (order matches RenderPostProcess execution). */
	if (config.sky_clouds) passes += 1;
	if (config.pixel_smoothing) passes += 1;
	if (config.terrain_smooth) passes += 1;
	if (config.tree_sway) passes += 1;
	if (config.water_reflections) passes += 1;
	if (config.ssao) passes += 1;
	if (config.fxaa) passes += 1;
	if (config.tiltshift) passes += 2; /* Horizontal + vertical blur */
	if (config.color_grading) passes += 1;
	if (config.night_mode) passes += 1;
	if (config.vignette) passes += 1;
	if (config.film_grain) passes += 1;
	if (config.dynamic_lighting) passes += 1;
	if (config.bloom) passes += 4; /* Threshold + blur H + blur V + composite. */
	if (config.depth_of_field) passes += 1;
	if (config.fake_shadows) passes += 1;
	if (config.crt_filter) passes += 1;
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

/** Central registry of all post-processing effects. */
static const PPEffectDescriptor _pp_effect_registry[] = {
	/* Core shipping (Medium+ tier, default candidates) */
	{"fxaa",          "fxaa",          true,  EffectCategory::Core,         true,  true,  false, true,  true,  1},
	{"sharpening",    "sharpening",    false, EffectCategory::Core,         true,  true,  false, true,  true,  1},
	{"color_grading", "color_grading", false, EffectCategory::Core,         true,  true,  false, true,  true,  1},
	{"vignette",      "vignette",      true,  EffectCategory::Core,         true,  true,  false, true,  true,  1},
	{"pixel_smooth",  "smooth",        true,  EffectCategory::Core,         true,  true,  false, true,  true,  1},
	{"supersample",   "supersample",   true,  EffectCategory::Core,         true,  true,  false, true,  true,  2},

	/* Presentation extras (High/Photo tier) */
	{"bloom",         "bloom",         true,  EffectCategory::Presentation, true,  true,  false, true,  false, 2},
	{"lighting",      "lighting",      true,  EffectCategory::Presentation, true,  true,  false, true,  false, 2},
	{"weather",       "weather",       true,  EffectCategory::Presentation, true,  true,  false, true,  false, 2},
	{"night",         "night",         true,  EffectCategory::Presentation, true,  true,  false, true,  false, 2},
	{"tiltshift",     "tiltshift",     true,  EffectCategory::Presentation, true,  true,  false, true,  false, 2},

	/* Novelty (Photo tier only) */
	{"crt",           "crt",           true,  EffectCategory::Novelty,      true,  true,  false, true,  false, 3},
	{"grain",         "grain",         true,  EffectCategory::Novelty,      true,  true,  false, true,  false, 3},

	/* Lab effects (quarantined, console-only) */
	{"shadows",       "shadows",       true,  EffectCategory::Lab,          false, false, true,  true,  false, 3},
	{"water",         "water",         true,  EffectCategory::Lab,          false, false, true,  true,  false, 3},
	{"ssao",          "ssao",          true,  EffectCategory::Lab,          false, false, true,  false, false, 3},
	{"terrain_smooth","terrain_smooth",true,  EffectCategory::Lab,          false, false, true,  true,  false, 3},
	{"tree_sway",     "tree_sway",     true,  EffectCategory::Lab,          false, false, true,  true,  false, 3},
	{"sky",           "sky",           true,  EffectCategory::Lab,          false, false, false, true,  false, 3},
	{"dof",           "dof",           true,  EffectCategory::Lab,          false, false, true,  false, false, 3},

	/* Research-only */
	{"temporal",      "temporal",      false, EffectCategory::Research,     false, false, false, true,  false, 3},
	{"plugin",        "plugin",        false, EffectCategory::Research,     false, false, false, true,  false, 3},

	/* Pipeline mode */
	{"cpu_scale",     "cpu_scale",     true,  EffectCategory::PipelineMode, true,  false, false, true,  false, 0},
};

/** Console aliases accepted by the `pp enable/disable` command. */
static constexpr std::pair<std::string_view, std::string_view> _pp_effect_aliases[] = {
	{"dynamic_lighting",   "lighting"},
	{"pixel_smooth",       "smooth"},
	{"cpu_viewport_scaling","cpu_scale"},
	{"fake_shadows",       "shadows"},
	{"water_reflections",  "water"},
	{"sway",               "tree_sway"},
	{"sky_clouds",         "sky"},
	{"depth_of_field",     "dof"},
};

/**
 * Look up a post-processing effect descriptor by key or console name.
 * @param key Internal key or console name to search for.
 * @return Pointer to the descriptor, or nullptr if not found.
 */
const PPEffectDescriptor *GetPPEffectDescriptor(std::string_view key)
{
	for (const auto &desc : _pp_effect_registry) {
		if (desc.key == key || desc.console_name == key) return &desc;
	}

	for (const auto &[alias, canonical] : _pp_effect_aliases) {
		if (alias != key) continue;
		for (const auto &desc : _pp_effect_registry) {
			if (desc.key == canonical) return &desc;
		}
		break;
	}

	return nullptr;
}

/**
 * Enumerate all registered post-processing effects.
 * @return Read-only span over the registry entries.
 */
std::span<const PPEffectDescriptor> GetPPEffectDescriptors()
{
	return _pp_effect_registry;
}

/**
 * Check whether an effect is in the Lab category (quarantined heuristic effects).
 * @param key Internal key or console name.
 * @return True if the effect exists and is classified as Lab.
 */
bool IsLabEffect(std::string_view key)
{
	const PPEffectDescriptor *desc = GetPPEffectDescriptor(key);
	return desc != nullptr && desc->category == EffectCategory::Lab;
}

/**
 * Check whether an effect is eligible for inclusion in quality presets.
 * @param key Internal key or console name.
 * @return True if the effect exists and is preset-eligible.
 */
bool IsPresetEligible(std::string_view key)
{
	const PPEffectDescriptor *desc = GetPPEffectDescriptor(key);
	return desc != nullptr && desc->preset_eligible;
}

/**
 * Calculate viewport scratch buffer dimensions for CPU-side render scaling.
 * Uses integer zoom steps: render_scale <= 50 gives zoom+1 (half-size),
 * render_scale <= 25 gives zoom+2 (quarter-size).
 * @param vp_width  Viewport width in display pixels.
 * @param vp_height Viewport height in display pixels.
 * @param render_scale Render scale percentage (25-100).
 * @return Scratch buffer dimensions and extra zoom steps.
 */
ViewportScratchDimensions CalculateViewportScratchDimensions(int vp_width, int vp_height, uint8_t render_scale)
{
	ViewportScratchDimensions dims{};
	if (vp_width <= 0 || vp_height <= 0) return dims;

	if (render_scale <= 25) {
		dims.extra_zoom_steps = 2;
	} else if (render_scale <= 50) {
		dims.extra_zoom_steps = 1;
	} else {
		/* No CPU scaling for render_scale > 50. */
		dims.extra_zoom_steps = 0;
		return dims;
	}

	int divisor = 1 << dims.extra_zoom_steps;
	dims.width = std::max(1, vp_width / divisor);
	dims.height = std::max(1, vp_height / divisor);
	dims.pitch = dims.width;
	return dims;
}

/**
 * Estimate the quality tier required by the current PP configuration.
 * Returns the highest quality_tier_min of any active effect in the registry.
 * @param config Current post-processing configuration.
 * @return Estimated quality tier.
 */
QualityTier EstimateQualityTier(const PostProcessConfig &config)
{
	/* Map config bools to registry keys. */
	struct EffectMapping {
		std::string_view key;
		bool active;
	};
	const EffectMapping mappings[] = {
		{"fxaa",           config.fxaa},
		{"sharpening",     config.sharpening > 0},
		{"color_grading",  config.color_grading},
		{"vignette",       config.vignette},
		{"pixel_smooth",   config.pixel_smoothing},
		{"supersample",    config.render_scale > 100},
		{"bloom",          config.bloom},
		{"lighting",       config.dynamic_lighting},
		{"weather",        config.weather_type > 0},
		{"night",          config.night_mode},
		{"tiltshift",      config.tiltshift},
		{"crt",            config.crt_filter},
		{"grain",          config.film_grain},
		{"shadows",        config.fake_shadows},
		{"water",          config.water_reflections},
		{"ssao",           config.ssao},
		{"terrain_smooth", config.terrain_smooth},
		{"tree_sway",      config.tree_sway},
		{"sky",            config.sky_clouds},
		{"dof",            config.depth_of_field},
	};

	uint8_t max_tier = 0;
	for (const auto &m : mappings) {
		if (!m.active) continue;
		const PPEffectDescriptor *desc = GetPPEffectDescriptor(m.key);
		if (desc != nullptr && desc->quality_tier_min > max_tier) {
			max_tier = desc->quality_tier_min;
		}
	}
	return static_cast<QualityTier>(max_tier);
}

/**
 * Get the maximum number of shader passes allowed for a quality tier.
 * @param tier Quality tier to query.
 * @return Pass budget (999 means unlimited).
 */
int GetTierPassBudget(QualityTier tier)
{
	switch (tier) {
		case QualityTier::Low:    return 0;
		case QualityTier::Medium: return 4;
		case QualityTier::High:   return 10;
		case QualityTier::Photo:  return 999;
		default: return 0;
	}
}

/**
 * Get a human-readable name for a quality tier.
 * @param tier Quality tier to name.
 * @return Short string name.
 */
std::string_view GetTierName(QualityTier tier)
{
	switch (tier) {
		case QualityTier::Low:    return "Low";
		case QualityTier::Medium: return "Medium";
		case QualityTier::High:   return "High";
		case QualityTier::Photo:  return "Photo";
		default: return "Unknown";
	}
}
