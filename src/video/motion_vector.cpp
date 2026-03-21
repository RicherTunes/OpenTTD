/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file motion_vector.cpp Motion vector generation for temporal upscaling. */

#include "../stdafx.h"
#include "motion_vector.h"
#include "../core/math_func.hpp"
#include "../zoom_func.h"

#include "../safeguards.h"

/** Global motion vector state, shared between viewport rendering and the GPU backend. */
MotionVectorState _motion_vectors;

/** Maximum world coordinate diagonal for depth normalization.
 * Depth = x + y + z*2. Max tile coords: MAX_MAP_SIZE-1 each axis.
 * Max z = MAX_TILE_HEIGHT * TILE_HEIGHT = 255 * 8 = 2040.
 * Max raw depth = 4095 + 4095 + 2040*2 = 12270. Round up for safety. */
static constexpr int32_t MAX_WORLD_DIAGONAL = 13000;

/**
 * Clear the command buffer for a new frame.
 */
void MotionVectorState::BeginFrame()
{
	this->commands.clear();
	/* Shrink capacity if it has grown far beyond the hard cap (e.g. after a spike). */
	if (this->commands.capacity() > MAX_COMMANDS * 2) this->commands.shrink_to_fit();
	this->scroll_dx = 0;
	this->scroll_dy = 0;
}

/**
 * Record a draw command from a parent sprite with world coordinates.
 * Called from ViewportDrawParentSprites() for each visible sprite.
 * @param screen_x Screen X position of sprite.
 * @param screen_y Screen Y position of sprite.
 * @param width Width of sprite on screen.
 * @param height Height of sprite on screen.
 * @param world_x World X coordinate (from ParentSpriteToDraw::xmin).
 * @param world_y World Y coordinate (from ParentSpriteToDraw::ymin).
 * @param world_z World Z coordinate (from ParentSpriteToDraw::zmin).
 */
void MotionVectorState::RecordSprite(int screen_x, int screen_y, int width, int height,
                                     int32_t world_x, int32_t world_y, int32_t world_z)
{
	if (width <= 0 || height <= 0) return;
	if (this->commands.size() >= MAX_COMMANDS) return;

	DrawCommand cmd;
	cmd.screen_x = static_cast<int16_t>(Clamp(screen_x, -32768, 32767));
	cmd.screen_y = static_cast<int16_t>(Clamp(screen_y, -32768, 32767));
	cmd.width = static_cast<uint16_t>(std::min(width, 65535));
	cmd.height = static_cast<uint16_t>(std::min(height, 65535));
	/* Default motion is the global viewport scroll. Per-object motion
	 * would require caching previous frame positions (future work). */
	cmd.motion_x = this->scroll_dx;
	cmd.motion_y = this->scroll_dy;
	cmd.depth = ComputeDepth(world_x, world_y, world_z);
	cmd.padding = 0;

	this->commands.push_back(cmd);
}

/**
 * Update viewport scroll delta from current viewport position.
 * Must be called once per frame before recording sprites.
 * @param virtual_left Current viewport virtual X position.
 * @param virtual_top Current viewport virtual Y position.
 * @param zoom Current viewport zoom level.
 */
void MotionVectorState::UpdateScrollDelta(int32_t virtual_left, int32_t virtual_top, ZoomLevel zoom)
{
	/* Compute pixel-space scroll delta, accounting for zoom level. */
	int32_t raw_dx = this->prev_scroll_x - virtual_left;
	int32_t raw_dy = this->prev_scroll_y - virtual_top;

	/* Convert virtual units to screen pixels using zoom. */
	int16_t pixel_dx = static_cast<int16_t>(Clamp(UnScaleByZoom(raw_dx, zoom) * 8, -32768, 32767));
	int16_t pixel_dy = static_cast<int16_t>(Clamp(UnScaleByZoom(raw_dy, zoom) * 8, -32768, 32767));

	this->scroll_dx = pixel_dx;
	this->scroll_dy = pixel_dy;

	/* Store current position for next frame's delta. */
	this->prev_scroll_x = virtual_left;
	this->prev_scroll_y = virtual_top;
}

/**
 * Allocate tile bins for the given screen size.
 */
void TileBin::Resize(int screen_w, int screen_h)
{
	this->tiles_x = std::max(1, (screen_w + TILE_SIZE - 1) / TILE_SIZE);
	this->tiles_y = std::max(1, (screen_h + TILE_SIZE - 1) / TILE_SIZE);
	this->data.resize(this->BufferSize(), 0);
}

/**
 * Bin draw commands into tiles.
 */
void TileBin::Build(const std::vector<DrawCommand> &commands)
{
	int stride = MAX_CMDS_PER_TILE + 1;
	int total_tiles = this->tiles_x * this->tiles_y;

	/* Clear all tile counts. */
	for (int t = 0; t < total_tiles; t++) {
		this->data[t * stride] = 0;
	}

	for (size_t i = 0; i < commands.size(); i++) {
		const DrawCommand &cmd = commands[i];
		if (cmd.width == 0 || cmd.height == 0) continue;

		int tx0 = Clamp<int>(cmd.screen_x / TILE_SIZE, 0, this->tiles_x - 1);
		int ty0 = Clamp<int>(cmd.screen_y / TILE_SIZE, 0, this->tiles_y - 1);
		int tx1 = Clamp<int>((cmd.screen_x + cmd.width - 1) / TILE_SIZE, 0, this->tiles_x - 1);
		int ty1 = Clamp<int>((cmd.screen_y + cmd.height - 1) / TILE_SIZE, 0, this->tiles_y - 1);

		for (int ty = ty0; ty <= ty1; ty++) {
			for (int tx = tx0; tx <= tx1; tx++) {
				int base = (ty * this->tiles_x + tx) * stride;
				int32_t &count = this->data[base];
				if (count < MAX_CMDS_PER_TILE) {
					this->data[base + 1 + count] = static_cast<int32_t>(i);
					count++;
				}
			}
		}
	}
}

/**
 * Compute synthetic depth from world coordinates.
 * Higher values = closer to camera in isometric projection.
 * @param world_x World X coordinate.
 * @param world_y World Y coordinate.
 * @param world_z World Z coordinate.
 * @return Normalized depth value (0-65535).
 */
/* static */ uint16_t MotionVectorState::ComputeDepth(int32_t world_x, int32_t world_y, int32_t world_z)
{
	/* In isometric view, depth increases with x+y+z*2.
	 * Normalize to 0-65535 range for GPU texture storage. */
	int64_t raw_depth = static_cast<int64_t>(world_x) + world_y + world_z * 2;
	int64_t normalized = Clamp<int64_t>(raw_depth * 65535 / MAX_WORLD_DIAGONAL, 0, 65535);
	return static_cast<uint16_t>(normalized);
}
