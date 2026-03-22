/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file harbor_gen.cpp Natural harbor scoring for map generation. */

#include "stdafx.h"
#include "harbor_gen.h"
#include "map_func.h"
#include "tile_map.h"
#include "water_map.h"

#include <vector>

#include "safeguards.h"

static std::vector<uint8_t> _harbor_scores;

/**
 * Check if a tile is a water tile for harbor scoring purposes.
 * @param tile The tile to check.
 * @return True if the tile is water.
 */
static bool IsWaterForHarbor(TileIndex tile)
{
	if (!IsValidTile(tile)) return false;
	return IsTileType(tile, TileType::Water);
}

/**
 * Check if a tile is a land tile for harbor scoring purposes.
 * @param tile The tile to check.
 * @return True if the tile is land (not water, not void).
 */
static bool IsLandForHarbor(TileIndex tile)
{
	if (!IsValidTile(tile)) return false;
	return !IsTileType(tile, TileType::Water) && !IsTileType(tile, TileType::Void);
}

/** Direction offsets for 8-directional ray casting. */
static const int8_t _harbor_dx[] = { 0,  1,  1,  1,  0, -1, -1, -1};
static const int8_t _harbor_dy[] = {-1, -1,  0,  1,  1,  1,  0, -1};

/**
 * Compute harbor scores for all coastal tiles.
 * Score is based on coastline concavity -- sheltered bays score higher
 * than exposed coastline.
 *
 * For each coastal tile (land tile adjacent to water), cast 8 rays
 * outward up to 16 tiles. Count how many rays hit land (indicating
 * shelter) and measure average water depth for water rays.
 * Score = land_rays * avg_water_depth, normalized to [0, 255].
 */
void ComputeHarborScores()
{
	_harbor_scores.assign(Map::Size(), 0);

	const int max_ray_length = 16;

	for (const auto tile : Map::Iterate()) {
		if (!IsValidTile(tile)) continue;
		if (!IsLandForHarbor(tile)) continue;

		/* Check if this is a coastal tile (land adjacent to water) */
		bool is_coastal = false;
		for (int d = 0; d < 8; d++) {
			TileIndex neighbor = TileAddWrap(tile, _harbor_dx[d], _harbor_dy[d]);
			if (neighbor != INVALID_TILE && IsWaterForHarbor(neighbor)) {
				is_coastal = true;
				break;
			}
		}
		if (!is_coastal) continue;

		/* Cast 8 rays and score */
		int land_rays = 0;
		int total_water_depth = 0;
		int water_rays = 0;

		for (int d = 0; d < 8; d++) {
			bool hit_land = false;
			int water_length = 0;

			for (int dist = 1; dist <= max_ray_length; dist++) {
				TileIndex target = TileAddWrap(tile, _harbor_dx[d] * dist, _harbor_dy[d] * dist);
				if (target == INVALID_TILE) break;

				if (IsLandForHarbor(target)) {
					hit_land = true;
					break;
				}
				if (IsWaterForHarbor(target)) {
					water_length++;
				}
			}

			if (hit_land) {
				land_rays++;
			}
			if (water_length > 0) {
				water_rays++;
				total_water_depth += water_length;
			}
		}

		_harbor_scores[(uint)tile] = NormalizeHarborScore(land_rays, water_rays, total_water_depth, max_ray_length);
	}
}

/**
 * Normalize harbor shelter/depth measurements into a score in [0, 255].
 * Uses full ray-depth precision instead of truncating average depth early.
 *
 * @param land_rays Number of rays that hit land (0-8).
 * @param water_rays Number of rays that traversed water (0-8).
 * @param total_water_depth Sum of water ray lengths.
 * @param max_ray_length Maximum ray cast distance (default 16).
 * @return Normalized score in [0, 255].
 */
uint8_t NormalizeHarborScore(int land_rays, int water_rays, int total_water_depth, int max_ray_length)
{
	if (land_rays <= 0 || water_rays <= 0) return 0;

	/* Use full precision: (land_rays * total_water_depth * 255) / (water_rays * 8 * max_ray_length)
	 * This avoids truncating avg_depth before multiplication. */
	int max_possible = water_rays * 8 * max_ray_length;
	int normalized = (land_rays * total_water_depth * 255) / std::max(1, max_possible);
	return static_cast<uint8_t>(Clamp(normalized, 0, 255));
}

/**
 * Get the harbor score for a tile.
 * @param tile The tile to query.
 * @return Score in [0, 255], where 0 = unsuitable and 255 = ideal harbor.
 */
uint8_t GetHarborScore(TileIndex tile)
{
	if (_harbor_scores.empty()) return 0;
	if (tile.base() >= _harbor_scores.size()) return 0;
	return _harbor_scores[tile.base()];
}

/**
 * Check if harbor scores have been computed and are available.
 * @return True if harbor scores are available for querying.
 */
bool HasValidHarborScores()
{
	return !_harbor_scores.empty();
}

/**
 * Free the harbor scores array after generation is complete.
 */
void FreeHarborScores()
{
	_harbor_scores.clear();
	_harbor_scores.shrink_to_fit();
}
