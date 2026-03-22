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
#include "core/random_func.hpp"

#include "safeguards.h"

static std::vector<TileIndex> _valid_tree_tiles;
static std::vector<TileIndex> _valid_town_tiles;

/**
 * Build precomputed lists of valid tiles for tree and town placement.
 * Called once during map generation after terrain and water are finalized.
 */
void BuildGenerationTileCache()
{
	_valid_tree_tiles.clear();
	_valid_town_tiles.clear();

	uint land_estimate = Map::Size() / 2;
	_valid_tree_tiles.reserve(land_estimate);
	_valid_town_tiles.reserve(land_estimate / 10);

	for (const auto tile : Map::Iterate()) {
		if (!IsValidTile(tile)) continue;

		TileType type = GetTileType(tile);

		/* Valid tree tiles: clear ground that isn't fields/rocks, or existing trees */
		if (type == TileType::Clear) {
			ClearGround ground = GetClearGround(tile);
			if (ground != ClearGround::Fields && ground != ClearGround::Rocks) {
				_valid_tree_tiles.push_back(tile);
			}
		} else if (type == TileType::Trees) {
			_valid_tree_tiles.push_back(tile);
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
 * Get a random valid tile for tree placement from the precomputed cache.
 * @return A random valid tree tile, or INVALID_TILE if cache is empty.
 */
TileIndex GetRandomValidTreeTile()
{
	if (_valid_tree_tiles.empty()) return INVALID_TILE;
	return _valid_tree_tiles[RandomRange(static_cast<uint>(_valid_tree_tiles.size()))];
}

/**
 * Get a random valid tile for town placement from the precomputed cache.
 * @return A random valid town tile, or INVALID_TILE if cache is empty.
 */
TileIndex GetRandomValidTownTile()
{
	if (_valid_town_tiles.empty()) return INVALID_TILE;
	return _valid_town_tiles[RandomRange(static_cast<uint>(_valid_town_tiles.size()))];
}

/**
 * Check whether the tree tile cache has entries.
 * @return true if there are valid tree tiles cached.
 */
bool HasValidTreeTiles()
{
	return !_valid_tree_tiles.empty();
}

/**
 * Check whether the town tile cache has entries.
 * @return true if there are valid town tiles cached.
 */
bool HasValidTownTiles()
{
	return !_valid_town_tiles.empty();
}
