/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file motion_vector.h Motion vector generation for temporal upscaling. */

#ifndef VIDEO_MOTION_VECTOR_H
#define VIDEO_MOTION_VECTOR_H

#include "../core/geometry_type.hpp"
#include "../zoom_type.h"
#include <vector>

/**
 * A recorded draw command capturing sprite position and motion.
 * These are generated during viewport rendering and consumed by the GPU
 * to rasterize per-pixel motion vector and depth textures.
 */
struct DrawCommand {
	int16_t screen_x;    ///< Screen X position of sprite bounding box.
	int16_t screen_y;    ///< Screen Y position of sprite bounding box.
	uint16_t width;      ///< Width of sprite bounding box on screen.
	uint16_t height;     ///< Height of sprite bounding box on screen.
	int16_t motion_x;    ///< Horizontal motion in 1/8 pixel units (fixed-point).
	int16_t motion_y;    ///< Vertical motion in 1/8 pixel units (fixed-point).
	uint16_t depth;      ///< Synthetic depth (higher = closer to camera).
	uint16_t padding;    ///< Padding for GPU alignment (32 bytes total).
};
static_assert(sizeof(DrawCommand) == 16, "DrawCommand must be 16 bytes for GPU upload");

/**
 * Motion vector recording state for a single frame.
 * Draw commands are recorded during ViewportDrawParentSprites()
 * and consumed during Paint() by the GPU MV rasterization shader.
 */
struct MotionVectorState {
	static constexpr size_t MAX_COMMANDS = 16384; ///< Hard cap on draw commands per frame to prevent unbounded growth.
	std::vector<DrawCommand> commands;  ///< Recorded draw commands this frame.
	int16_t scroll_dx = 0;              ///< Global viewport scroll delta X (1/8 px).
	int16_t scroll_dy = 0;              ///< Global viewport scroll delta Y (1/8 px).
	int32_t prev_scroll_x = 0;          ///< Previous frame viewport scroll X.
	int32_t prev_scroll_y = 0;          ///< Previous frame viewport scroll Y.
	bool active = false;                ///< Whether MV recording is enabled.

	/** Clear the command buffer for a new frame. */
	void BeginFrame();

	/** Record a draw command from a parent sprite with world coordinates. */
	void RecordSprite(int screen_x, int screen_y, int width, int height,
	                  int32_t world_x, int32_t world_y, int32_t world_z);

	/** Update viewport scroll delta from current viewport position. */
	void UpdateScrollDelta(int32_t virtual_left, int32_t virtual_top, ZoomLevel zoom);

	/** Compute synthetic depth from world coordinates. */
	static uint16_t ComputeDepth(int32_t world_x, int32_t world_y, int32_t world_z);
};

extern MotionVectorState _motion_vectors;

/** Tile-based spatial bin for GPU MV rasterization dispatch. */
struct TileBin {
	static constexpr int TILE_SIZE = 16;        ///< Tile size in pixels.
	static constexpr int MAX_CMDS_PER_TILE = 64; ///< Max commands per tile.

	int tiles_x = 0;                           ///< Number of tiles horizontally.
	int tiles_y = 0;                           ///< Number of tiles vertically.
	std::vector<int32_t> data;                  ///< Flat buffer: [count, idx0..idx63] per tile.

	/** Allocate tile bins for the given screen size. */
	void Resize(int screen_w, int screen_h);

	/** Bin draw commands into tiles. */
	void Build(const std::vector<DrawCommand> &commands);

	/** Get the total buffer size in int32_t elements. */
	size_t BufferSize() const { return static_cast<size_t>(this->tiles_x) * this->tiles_y * (MAX_CMDS_PER_TILE + 1); }
};

#endif /* VIDEO_MOTION_VECTOR_H */
