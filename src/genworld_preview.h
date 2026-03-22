/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file genworld_preview.h Map preview generation for the world generation window. */

#ifndef GENWORLD_PREVIEW_H
#define GENWORLD_PREVIEW_H

#include "heightmap.h"

#include <vector>
#include <cstdint>

/** Result of a map preview generation. */
struct MapPreviewData {
	std::vector<uint8_t> pixels;   ///< Palette colour indices, row-major (width * height).
	uint16_t width = 0;            ///< Preview width in pixels.
	uint16_t height = 0;           ///< Preview height in pixels.
	uint32_t seed = 0;             ///< Generation seed used for this preview.
	uint8_t map_x = 0;             ///< Log2 map X size used.
	uint8_t map_y = 0;             ///< Log2 map Y size used.
};

bool GenerateMapPreview(MapPreviewData &out, uint32_t seed, uint8_t map_x, uint8_t map_y,
	uint16_t preview_w, uint16_t preview_h);

bool GenerateHeightmapPreview(MapPreviewData &out, const std::vector<uint8_t> &greyscale,
	uint src_w, uint src_h, uint16_t preview_w, uint16_t preview_h, HeightmapRotation rotation = HM_COUNTER_CLOCKWISE);

uint8_t HarborScoreToColour(uint8_t score);
uint8_t HeightToPreviewColour(int height, int max_height, bool is_water);

#endif /* GENWORLD_PREVIEW_H */
