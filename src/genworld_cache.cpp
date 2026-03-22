/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file genworld_cache.cpp Precomputed valid tile caches for map generation. */

#include "stdafx.h"
#include "genworld_cache.h"
#include "map_func.h"
#include "tile_map.h"
#include "clear_map.h"
#include "tree_map.h"
#include "water_map.h"
#include "bridge_map.h"
#include "slope_func.h"
#include "core/random_func.hpp"

#include "safeguards.h"

static std::vector<TileIndex> _valid_tree_tiles;
static std::vector<TileIndex> _valid_town_tiles;

/**
 * Build precomputed lists of valid tiles for tree and town placement.
 * Called once during map generation after terrain and water are finalized.
 * Tree tiles: clear tiles (not fields/rocks) or coast tiles that can accept trees.
 * Town tiles: flat clear/tree tiles away from map edge.
 */
void BuildGenerationTileCache()
{
	_valid_tree_tiles.clear();
	_valid_town_tiles.clear();

	/* Reserve approximate capacity to avoid reallocation */
	uint land_estimate = Map::Size() / 2;
	_valid_tree_tiles.reserve(land_estimate);
	_valid_town_tiles.reserve(land_estimate / 10);

	for (const auto tile : Map::Iterate()) {
		if (!IsValidTile(tile)) continue;

		TileType type = GetTileType(tile);

		/* Valid tree tiles: mirror CanPlantTreesOnTile(tile, true) logic.
		 * Clear tiles that are not Fields or Rocks (Desert allowed with allow_desert=true).
		 * Coast tiles on water (no bridge, no single-corner-raised slope).
		 * No bridge above for either case. */
		if (type == TileType::Clear && !IsBridgeAbove(tile)) {
			ClearGround ground = GetClearGround(tile);
			if (ground != ClearGround::Fields && ground != ClearGround::Rocks) {
				_valid_tree_tiles.push_back(tile);
			}
		} else if (type == TileType::Water && !IsBridgeAbove(tile)) {
			if (IsCoast(tile) && !IsSlopeWithOneCornerRaised(GetTileSlope(tile))) {
				_valid_tree_tiles.push_back(tile);
			}
		}

		/* Valid town tiles: flat clear/tree tiles, 12+ tiles from edge */
		if ((type == TileType::Clear || type == TileType::Trees) && IsTileFlat(tile)) {
			if (DistanceFromEdge(tile) >= 12) {
				_valid_town_tiles.push_back(tile);
			}
		}
	}
}

/**
 * Free the generation tile caches after map generation is complete.
 */
void FreeGenerationTileCache()
{
	_valid_tree_tiles.clear();
	_valid_tree_tiles.shrink_to_fit();
	_valid_town_tiles.clear();
	_valid_town_tiles.shrink_to_fit();
}

/**
 * Get a random valid tile for tree placement.
 * @return A random tile from the precomputed valid tree tile list, or INVALID_TILE if empty.
 */
TileIndex GetRandomValidTreeTile()
{
	if (_valid_tree_tiles.empty()) return INVALID_TILE;
	return _valid_tree_tiles[RandomRange((uint)_valid_tree_tiles.size())];
}

/**
 * Get a random valid tile for town placement.
 * @return A random tile from the precomputed valid town tile list, or INVALID_TILE if empty.
 */
TileIndex GetRandomValidTownTile()
{
	if (_valid_town_tiles.empty()) return INVALID_TILE;
	return _valid_town_tiles[RandomRange((uint)_valid_town_tiles.size())];
}

/**
 * Check if valid tree tiles are available.
 * @return True if the cache has entries.
 */
bool HasValidTreeTiles()
{
	return !_valid_tree_tiles.empty();
}

/**
 * Check if valid town tiles are available.
 * @return True if the cache has entries.
 */
bool HasValidTownTiles()
{
	return !_valid_town_tiles.empty();
}
