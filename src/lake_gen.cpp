/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file lake_gen.cpp Lake generation for map creation. */

#include "stdafx.h"
#include "lake_gen.h"
#include "landscape.h"
#include "genworld.h"
#include "settings_type.h"
#include "map_func.h"
#include "tile_map.h"
#include "water.h"
#include "water_map.h"
#include "clear_map.h"

#include <queue>
#include <vector>

#include "safeguards.h"

/**
 * Detect enclosed depressions above sea level and fill them as lakes.
 * Uses BFS flood-fill to find connected flat tiles at the same height,
 * then checks if the component is fully enclosed by higher terrain.
 */
void CreateLakes()
{
	uint8_t amount = _settings_game.game_creation.amount_of_lakes;
	if (amount == 0) return;

	/* Scale lake detection thresholds by setting */
	const uint min_lake_height = 2;
	const uint max_lake_height = 4 + amount * 2; /* 6, 8, 10 for Few/Normal/Many */
	const uint min_lake_size = 4;
	const uint max_lake_size = Map::ScaleBySize(64 << amount); /* Larger lakes for higher settings */

	std::vector<bool> visited(Map::Size(), false);
	std::queue<TileIndex> bfs_queue;
	std::vector<TileIndex> component;

	for (const auto start_tile : Map::Iterate()) {
		uint start_idx = (uint)start_tile;
		if (visited[start_idx]) continue;
		if (!IsValidTile(start_tile)) continue;

		uint h = TileHeight(start_tile);
		if (h < min_lake_height || h > max_lake_height) continue;
		if (!IsTileFlat(start_tile)) continue;
		if (IsTileType(start_tile, TileType::Water)) continue;

		/* BFS to find connected flat tiles at the same height */
		component.clear();
		TileIndex start_ti = start_tile;
		bfs_queue.push(start_ti);
		visited[start_idx] = true;

		bool is_enclosed = true;
		uint component_height = h;

		while (!bfs_queue.empty()) {
			TileIndex tile = bfs_queue.front();
			bfs_queue.pop();
			component.push_back(tile);

			/* Check if component is getting too large */
			if (component.size() > max_lake_size) {
				is_enclosed = false;
				break;
			}

			/* Check all 4 neighbors */
			for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
				TileIndex neighbor = tile + TileOffsByDiagDir(dir);
				if (!IsValidTile(neighbor)) {
					is_enclosed = false;
					continue;
				}

				uint nh = TileHeight(neighbor);

				if (nh == component_height && IsTileFlat(neighbor) && !IsTileType(neighbor, TileType::Water)) {
					if (!visited[neighbor.base()]) {
						visited[neighbor.base()] = true;
						bfs_queue.push(neighbor);
					}
				} else if (nh < component_height) {
					/* Lower neighbor means water can drain out */
					is_enclosed = false;
				}
				/* nh > component_height is fine -- it's a wall */
			}
		}

		/* Drain remaining queue if we broke out early */
		while (!bfs_queue.empty()) {
			visited[bfs_queue.front().base()] = true;
			bfs_queue.pop();
		}

		if (!is_enclosed) continue;
		if (component.size() < min_lake_size) continue;

		/* Fill the enclosed basin as a lake using river tiles */
		for (TileIndex tile : component) {
			if (IsTileType(tile, TileType::Clear) || IsTileType(tile, TileType::Trees)) {
				MakeRiver(tile, Random());
			}
		}
	}
}
