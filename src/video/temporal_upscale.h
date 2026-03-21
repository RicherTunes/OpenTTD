/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file temporal_upscale.h Interface for temporal upscaling (FSR 2 / future DLSS). */

#ifndef VIDEO_TEMPORAL_UPSCALE_H
#define VIDEO_TEMPORAL_UPSCALE_H

#include "../core/geometry_type.hpp"

/**
 * Jitter sequence for temporal accumulation.
 * Uses Halton sequence base 2/3 for sub-pixel offsets.
 */
struct JitterSequence {
	uint32_t frame_index = 0; ///< Current frame index in the sequence.

	/** Advance to the next frame and return jitter offset in pixels. */
	void NextFrame(float &jitter_x, float &jitter_y);

	/** Reset the sequence (e.g., on scene cut). */
	void Reset() { this->frame_index = 0; }

	/** Halton sequence value for a given index and base. */
	static float Halton(uint32_t index, uint32_t base);
};

/**
 * Configuration for temporal upscaling dispatch.
 */
struct TemporalUpscaleParams {
	Dimension render_size;    ///< Input (render) resolution.
	Dimension display_size;   ///< Output (display) resolution.
	float jitter_x = 0.0f;   ///< Sub-pixel jitter X offset.
	float jitter_y = 0.0f;   ///< Sub-pixel jitter Y offset.
	float delta_time = 0.016f; ///< Frame time in seconds.
	bool reset = false;       ///< Force history reset (scene cut).
	/* GL texture handles (set by the caller). */
	uint32_t color_texture = 0;    ///< Input color texture (render resolution).
	uint32_t mv_texture = 0;       ///< Motion vector texture (render resolution).
	uint32_t depth_texture = 0;    ///< Depth texture (render resolution).
	uint32_t output_texture = 0;   ///< Output texture (display resolution).
};

/**
 * Determine whether temporal upscaling should use jitter.
 * Jitter is only applied when render_scale < 100% AND zoom level is Out2x or further.
 * At native resolution or zoomed in, pixel art must remain pixel-perfect.
 * @param render_scale Current render scale percentage (50-200).
 * @param zoom Current viewport zoom level (0=In4x, 2=Normal, 5=Out8x).
 * @return True if jitter should be applied.
 */
bool ShouldApplyJitter(uint8_t render_scale, int zoom_level);

extern JitterSequence _jitter_sequence;

#endif /* VIDEO_TEMPORAL_UPSCALE_H */
