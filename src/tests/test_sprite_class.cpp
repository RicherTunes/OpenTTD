/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file test_sprite_class.cpp Tests for sprite classification buffer. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../video/sprite_class.h"

#include "../safeguards.h"

/* --- SpriteClass enum tests --- */

TEST_CASE("SpriteClass - enum values are sequential from 0")
{
	CHECK(SPRITE_CLASS_UNKNOWN == 0);
	CHECK(SPRITE_CLASS_TERRAIN == 1);
	CHECK(SPRITE_CLASS_WATER == 2);
	CHECK(SPRITE_CLASS_VEGETATION == 3);
	CHECK(SPRITE_CLASS_STRUCTURE == 4);
	CHECK(SPRITE_CLASS_VEHICLE == 5);
	CHECK(SPRITE_CLASS_EFFECT == 6);
	CHECK(SPRITE_CLASS_UI == 7);
}

TEST_CASE("SpriteClass - all values fit in uint8_t")
{
	CHECK(SPRITE_CLASS_COUNT <= 256);
}

TEST_CASE("SpriteClass - initial state is inactive")
{
	/* Global state should default to inactive. */
	CHECK(!_sprite_class.active);
	CHECK(_sprite_class.class_buf == nullptr);
	CHECK(_sprite_class.buf_pitch == 0);
	CHECK(_sprite_class.current_class == SPRITE_CLASS_UNKNOWN);
}

TEST_CASE("SpriteClass - GetSpriteClassName returns valid strings")
{
	CHECK(GetSpriteClassName(SPRITE_CLASS_UNKNOWN) == "unknown");
	CHECK(GetSpriteClassName(SPRITE_CLASS_TERRAIN) == "terrain");
	CHECK(GetSpriteClassName(SPRITE_CLASS_WATER) == "water");
	CHECK(GetSpriteClassName(SPRITE_CLASS_VEGETATION) == "vegetation");
	CHECK(GetSpriteClassName(SPRITE_CLASS_STRUCTURE) == "structure");
	CHECK(GetSpriteClassName(SPRITE_CLASS_VEHICLE) == "vehicle");
	CHECK(GetSpriteClassName(SPRITE_CLASS_EFFECT) == "effect");
	CHECK(GetSpriteClassName(SPRITE_CLASS_UI) == "ui");
}

TEST_CASE("SpriteClass - GetSpriteClassName returns unknown for invalid values")
{
	CHECK(GetSpriteClassName(static_cast<SpriteClass>(SPRITE_CLASS_COUNT)) == "unknown");
	CHECK(GetSpriteClassName(static_cast<SpriteClass>(255)) == "unknown");
}

TEST_CASE("SpriteClass - COUNT equals 8")
{
	CHECK(SPRITE_CLASS_COUNT == 8);
}

TEST_CASE("SpriteClass - SpriteClassState default construction")
{
	SpriteClassState state;
	CHECK(state.class_buf == nullptr);
	CHECK(state.buf_pitch == 0);
	CHECK(state.current_class == SPRITE_CLASS_UNKNOWN);
	CHECK(!state.active);
}

TEST_CASE("SpriteClass - SpriteClassState can be activated with buffer")
{
	SpriteClassState state;
	uint8_t buf[64] = {};
	state.class_buf = buf;
	state.buf_pitch = 8;
	state.active = true;
	state.current_class = SPRITE_CLASS_WATER;

	CHECK(state.class_buf == buf);
	CHECK(state.buf_pitch == 8);
	CHECK(state.active);
	CHECK(state.current_class == SPRITE_CLASS_WATER);
}

TEST_CASE("SpriteClass - enum underlying type is uint8_t")
{
	/* Verify the enum fits in a single byte for per-pixel storage. */
	static_assert(sizeof(SpriteClass) == 1, "SpriteClass must be uint8_t for per-pixel buffer");
	CHECK(sizeof(SpriteClass) == 1);
}
