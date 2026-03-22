/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file temporal_upscale.cpp Temporal upscaling support (jitter, FSR 2 integration). */

#include "../stdafx.h"
#include "temporal_upscale.h"
#include "../core/math_func.hpp"
#include "../zoom_type.h"

#include "../safeguards.h"

JitterSequence _jitter_sequence;

/**
 * Compute a value in the Halton sequence for a given index and base.
 * @param index Sequence index (0-based).
 * @param base Prime base (2 or 3 typically).
 * @return Value in [0, 1).
 */
/* static */ float JitterSequence::Halton(uint32_t index, uint32_t base)
{
	assert(base >= 2);
	float result = 0.0f;
	float f = 1.0f / static_cast<float>(base);
	uint32_t i = index + 1; /* 1-based for better distribution. */
	while (i > 0) {
		result += f * static_cast<float>(i % base);
		i /= base;
		f /= static_cast<float>(base);
	}
	return result;
}

/**
 * Advance to the next frame and compute jitter offsets.
 * Returns sub-pixel offsets in [-0.5, 0.5] range.
 * @param[out] jitter_x Horizontal jitter in pixels.
 * @param[out] jitter_y Vertical jitter in pixels.
 */
void JitterSequence::NextFrame(float &jitter_x, float &jitter_y)
{
	jitter_x = Halton(this->frame_index, 2) - 0.5f;
	jitter_y = Halton(this->frame_index, 3) - 0.5f;
	this->frame_index = (this->frame_index + 1) % 64; /* 64-frame cycle. */
}

/**
 * Determine whether temporal jitter should be applied.
 * @param render_scale Render scale percentage (50-200).
 * @param zoom_level Zoom level as integer (0=In4x, 2=Normal, 5=Out8x).
 * @return True if jitter is appropriate for current settings.
 */
bool ShouldApplyJitter(uint8_t render_scale, int zoom_level)
{
	/* No jitter at native resolution -- pixel art must be pixel-perfect. */
	if (render_scale >= 100) return false;

	/* No jitter at Normal or zoomed-in levels where individual pixels are large. */
	if (zoom_level <= to_underlying(ZoomLevel::Normal)) return false;

	return true;
}
