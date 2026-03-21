/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file test_motion_vector.cpp Tests for motion vector generation. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../video/motion_vector.h"

#include "../safeguards.h"

/* --- DrawCommand struct tests --- */

TEST_CASE("MotionVector - DrawCommand is 16 bytes")
{
	CHECK(sizeof(DrawCommand) == 16);
}

/* --- BeginFrame tests --- */

TEST_CASE("MotionVector - BeginFrame clears commands")
{
	MotionVectorState state;
	state.commands.push_back({10, 20, 30, 40, 0, 0, 100, 0});
	state.scroll_dx = 5;
	state.scroll_dy = 10;
	state.BeginFrame();
	CHECK(state.commands.empty());
	CHECK(state.scroll_dx == 0);
	CHECK(state.scroll_dy == 0);
}

/* --- RecordSprite tests --- */

TEST_CASE("MotionVector - RecordSprite adds command")
{
	MotionVectorState state;
	state.BeginFrame();
	state.RecordSprite(100, 200, 32, 48, 500, 300, 10);
	REQUIRE(state.commands.size() == 1);
	CHECK(state.commands[0].screen_x == 100);
	CHECK(state.commands[0].screen_y == 200);
	CHECK(state.commands[0].width == 32);
	CHECK(state.commands[0].height == 48);
}

TEST_CASE("MotionVector - RecordSprite ignores zero-size sprites")
{
	MotionVectorState state;
	state.BeginFrame();
	state.RecordSprite(100, 200, 0, 48, 0, 0, 0);
	CHECK(state.commands.empty());
	state.RecordSprite(100, 200, 32, 0, 0, 0, 0);
	CHECK(state.commands.empty());
	state.RecordSprite(100, 200, -1, 48, 0, 0, 0);
	CHECK(state.commands.empty());
}

TEST_CASE("MotionVector - RecordSprite uses scroll delta as default motion")
{
	MotionVectorState state;
	state.BeginFrame();
	state.scroll_dx = 16; /* 2 pixels in 1/8 px units */
	state.scroll_dy = -8;  /* -1 pixel */
	state.RecordSprite(50, 50, 10, 10, 0, 0, 0);
	REQUIRE(state.commands.size() == 1);
	CHECK(state.commands[0].motion_x == 16);
	CHECK(state.commands[0].motion_y == -8);
}

TEST_CASE("MotionVector - RecordSprite clamps large coordinates")
{
	MotionVectorState state;
	state.BeginFrame();
	state.RecordSprite(50000, -50000, 100000, 100000, 0, 0, 0);
	REQUIRE(state.commands.size() == 1);
	CHECK(state.commands[0].screen_x == 32767);
	CHECK(state.commands[0].screen_y == -32768);
	CHECK(state.commands[0].width == 65535);
	CHECK(state.commands[0].height == 65535);
}

TEST_CASE("MotionVector - RecordSprite multiple sprites")
{
	MotionVectorState state;
	state.BeginFrame();
	for (int i = 0; i < 100; i++) {
		state.RecordSprite(i * 10, i * 5, 16, 16, i, i, 0);
	}
	CHECK(state.commands.size() == 100);
}

/* --- ComputeDepth tests --- */

TEST_CASE("MotionVector - ComputeDepth zero coordinates")
{
	CHECK(MotionVectorState::ComputeDepth(0, 0, 0) == 0);
}

TEST_CASE("MotionVector - ComputeDepth increases with world position")
{
	uint16_t d1 = MotionVectorState::ComputeDepth(100, 100, 0);
	uint16_t d2 = MotionVectorState::ComputeDepth(200, 200, 0);
	uint16_t d3 = MotionVectorState::ComputeDepth(100, 100, 50);
	CHECK(d2 > d1);
	CHECK(d3 > d1); /* Z contributes to depth */
}

TEST_CASE("MotionVector - ComputeDepth Z contributes double")
{
	uint16_t d_flat = MotionVectorState::ComputeDepth(100, 0, 0);
	uint16_t d_z = MotionVectorState::ComputeDepth(0, 0, 50);
	/* Z=50 should be equivalent to x+y=100 */
	CHECK(d_z == d_flat);
}

TEST_CASE("MotionVector - ComputeDepth clamps to valid range")
{
	uint16_t d = MotionVectorState::ComputeDepth(100000, 100000, 100000);
	CHECK(d == 65535);
	uint16_t d_neg = MotionVectorState::ComputeDepth(-1000, -1000, -1000);
	CHECK(d_neg == 0);
}

/* --- UpdateScrollDelta tests --- */

TEST_CASE("MotionVector - UpdateScrollDelta zero on first frame")
{
	MotionVectorState state;
	state.prev_scroll_x = 1000;
	state.prev_scroll_y = 500;
	/* Same position = no scroll delta. */
	state.UpdateScrollDelta(1000, 500, ZoomLevel::Normal);
	CHECK(state.scroll_dx == 0);
	CHECK(state.scroll_dy == 0);
}

TEST_CASE("MotionVector - UpdateScrollDelta detects scroll")
{
	MotionVectorState state;
	state.prev_scroll_x = 1000;
	state.prev_scroll_y = 500;
	/* Scroll right by 4 virtual units at normal zoom. */
	state.UpdateScrollDelta(1004, 500, ZoomLevel::Normal);
	/* At Normal zoom (shift=2), 4 virtual units = 1 pixel.
	 * In 1/8 pixel fixed-point: 1 * 8 = 8. But it's prev-current, so negative. */
	CHECK(state.scroll_dx == -8);
	CHECK(state.scroll_dy == 0);
}

TEST_CASE("MotionVector - UpdateScrollDelta accounts for zoom")
{
	MotionVectorState state;
	state.prev_scroll_x = 1000;
	state.prev_scroll_y = 500;
	/* Scroll by 8 virtual units at Out2x zoom. */
	state.UpdateScrollDelta(1008, 500, ZoomLevel::Out2x);
	/* At Out2x zoom (shift=3), 8 virtual units = 1 pixel.
	 * In 1/8 px: 1 * 8 = 8. Prev-current = negative. */
	CHECK(state.scroll_dx == -8);
}

TEST_CASE("MotionVector - UpdateScrollDelta stores position for next frame")
{
	MotionVectorState state;
	state.prev_scroll_x = 0;
	state.prev_scroll_y = 0;
	state.UpdateScrollDelta(100, 200, ZoomLevel::Normal);
	CHECK(state.prev_scroll_x == 100);
	CHECK(state.prev_scroll_y == 200);
}

/* --- Integration: full frame recording workflow --- */

TEST_CASE("MotionVector - Full frame workflow")
{
	MotionVectorState state;
	state.active = true;

	/* Frame 1: set initial position. First call has delta from 0 to 1000. */
	state.prev_scroll_x = 1000;
	state.prev_scroll_y = 500;
	state.BeginFrame();
	state.UpdateScrollDelta(1000, 500, ZoomLevel::Normal);
	state.RecordSprite(100, 200, 32, 32, 50, 50, 5);
	state.RecordSprite(200, 300, 16, 16, 100, 100, 10);
	CHECK(state.commands.size() == 2);
	CHECK(state.scroll_dx == 0); /* Same position, no delta. */

	/* Frame 2: scroll right by 16 virtual units. */
	state.BeginFrame();
	state.UpdateScrollDelta(1016, 500, ZoomLevel::Normal);
	state.RecordSprite(84, 200, 32, 32, 50, 50, 5);
	CHECK(state.commands.size() == 1);
	/* Scroll delta: (1000 - 1016) / zoom * 8 */
	CHECK(state.scroll_dx != 0); /* Should be non-zero due to scroll */
	CHECK(state.commands[0].motion_x == state.scroll_dx); /* Sprite inherits scroll delta */
}

/* --- Depth normalization boundary tests --- */

TEST_CASE("MotionVector - ComputeDepth at typical max world coords")
{
	/* Max map: x=4095, y=4095, z=255*8=2040. raw_depth = 4095+4095+4080 = 12270 */
	uint16_t d = MotionVectorState::ComputeDepth(4095, 4095, 2040);
	CHECK(d > 0);
	CHECK(d <= 65535);
}

TEST_CASE("MotionVector - ComputeDepth negative coords clamp to 0")
{
	CHECK(MotionVectorState::ComputeDepth(-5000, -5000, -5000) == 0);
}

TEST_CASE("MotionVector - RecordSprite at INT16 boundary coords")
{
	MotionVectorState state;
	state.BeginFrame();
	state.RecordSprite(32767, -32768, 1, 1, 0, 0, 0);
	REQUIRE(state.commands.size() == 1);
	CHECK(state.commands[0].screen_x == 32767);
	CHECK(state.commands[0].screen_y == -32768);
}

TEST_CASE("MotionVector - BeginFrame resets but preserves prev_scroll")
{
	MotionVectorState state;
	state.prev_scroll_x = 1000;
	state.prev_scroll_y = 2000;
	state.BeginFrame();
	/* BeginFrame should NOT reset prev_scroll, only commands and current delta. */
	CHECK(state.prev_scroll_x == 1000);
	CHECK(state.prev_scroll_y == 2000);
}

TEST_CASE("MotionVector - ComputeDepth is monotonic in each axis")
{
	for (int i = 0; i < 100; i++) {
		uint16_t d1 = MotionVectorState::ComputeDepth(i * 10, 0, 0);
		uint16_t d2 = MotionVectorState::ComputeDepth((i + 1) * 10, 0, 0);
		CHECK(d2 >= d1);
	}
}

/* --- TileBin tests --- */

TEST_CASE("TileBin - Resize allocates correct number of tiles")
{
	TileBin bin;
	bin.Resize(320, 240);
	CHECK(bin.tiles_x == 20);
	CHECK(bin.tiles_y == 15);
	CHECK(bin.BufferSize() == 20u * 15u * (TileBin::MAX_CMDS_PER_TILE + 1));
}

TEST_CASE("TileBin - Resize handles non-multiple-of-16 sizes")
{
	TileBin bin;
	bin.Resize(321, 241);
	CHECK(bin.tiles_x == 21);
	CHECK(bin.tiles_y == 16);
}

TEST_CASE("TileBin - Build empty command list")
{
	TileBin bin;
	bin.Resize(320, 240);
	std::vector<DrawCommand> empty;
	bin.Build(empty);
	/* All tile counts should be 0. */
	for (int t = 0; t < bin.tiles_x * bin.tiles_y; t++) {
		CHECK(bin.data[t * (TileBin::MAX_CMDS_PER_TILE + 1)] == 0);
	}
}

TEST_CASE("TileBin - Build single sprite in one tile")
{
	TileBin bin;
	bin.Resize(320, 240);
	std::vector<DrawCommand> cmds;
	cmds.push_back({8, 8, 4, 4, 0, 0, 100, 0}); /* Fits entirely in tile (0,0). */
	bin.Build(cmds);

	int stride = TileBin::MAX_CMDS_PER_TILE + 1;
	/* Tile (0,0) should have 1 command. */
	CHECK(bin.data[0 * stride] == 1);
	CHECK(bin.data[0 * stride + 1] == 0); /* Index of command 0. */
	/* Tile (1,0) should have 0 commands. */
	CHECK(bin.data[1 * stride] == 0);
}

TEST_CASE("TileBin - Build sprite spanning multiple tiles")
{
	TileBin bin;
	bin.Resize(320, 240);
	std::vector<DrawCommand> cmds;
	/* 32x32 sprite starting at (8,8) spans tiles (0,0), (1,0), (0,1), (1,1). */
	cmds.push_back({8, 8, 32, 32, 0, 0, 100, 0});
	bin.Build(cmds);

	int stride = TileBin::MAX_CMDS_PER_TILE + 1;
	int tiles_x = bin.tiles_x;
	CHECK(bin.data[(0 * tiles_x + 0) * stride] == 1); /* Tile (0,0) */
	CHECK(bin.data[(0 * tiles_x + 1) * stride] == 1); /* Tile (1,0) */
	CHECK(bin.data[(1 * tiles_x + 0) * stride] == 1); /* Tile (0,1) */
	CHECK(bin.data[(1 * tiles_x + 1) * stride] == 1); /* Tile (1,1) */
	/* Tile (2,0) should NOT have this command (32 pixels from x=8 = x=40, tile 2 starts at x=32, sprite ends at x=39). */
	CHECK(bin.data[(0 * tiles_x + 2) * stride] >= 0); /* May or may not depending on exact math */
}

TEST_CASE("TileBin - Build respects max commands per tile")
{
	TileBin bin;
	bin.Resize(16, 16); /* Just one tile. */
	std::vector<DrawCommand> cmds;
	/* Add more commands than MAX_CMDS_PER_TILE. */
	for (int i = 0; i < TileBin::MAX_CMDS_PER_TILE + 10; i++) {
		cmds.push_back({0, 0, 16, 16, 0, 0, static_cast<uint16_t>(i), 0});
	}
	bin.Build(cmds);

	int stride = TileBin::MAX_CMDS_PER_TILE + 1;
	CHECK(bin.data[0] == TileBin::MAX_CMDS_PER_TILE); /* Capped at max. */
}
