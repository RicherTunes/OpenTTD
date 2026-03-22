/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file sprite_class.h Per-pixel sprite classification for metadata-driven post-processing. */

#ifndef VIDEO_SPRITE_CLASS_H
#define VIDEO_SPRITE_CLASS_H

#include "../stdafx.h"
#include <string_view>

/** Sprite classification categories written per-pixel during CPU compositing.
 * TileType-based classification is an MVP and expected to be coarse for
 * mixed-content visuals (coastlines, bridges, stations, overlapping sprites).
 * It is sufficient for first metadata-backed experiments, not a full scene model. */
enum SpriteClass : uint8_t {
	SPRITE_CLASS_UNKNOWN    = 0, ///< UI, unclassified, or outside viewport.
	SPRITE_CLASS_TERRAIN    = 1, ///< Ground, clear land, fields.
	SPRITE_CLASS_WATER      = 2, ///< Water surface tiles.
	SPRITE_CLASS_VEGETATION = 3, ///< Trees, bushes, vegetation.
	SPRITE_CLASS_STRUCTURE  = 4, ///< Buildings, houses, industry, objects.
	SPRITE_CLASS_VEHICLE    = 5, ///< Trains, trucks, ships, aircraft.
	SPRITE_CLASS_EFFECT     = 6, ///< Smoke, sparks, signs, text effects.
	SPRITE_CLASS_UI         = 7, ///< UI elements rendered over viewport.
	SPRITE_CLASS_COUNT      = 8, ///< Number of classification categories.
};

/** Global state for the sprite classification system.
 * Set active=true and class_buf pointer before CPU draw phase.
 * The blitter writes current_class for every non-transparent pixel. */
struct SpriteClassState {
	uint8_t *class_buf = nullptr;   ///< Classification buffer (same dimensions as _screen).
	int buf_pitch = 0;              ///< Buffer pitch in pixels per row.
	SpriteClass current_class = SPRITE_CLASS_UNKNOWN; ///< Active classification for next draw.
	bool active = false;            ///< Whether classification writing is enabled.
};

extern SpriteClassState _sprite_class;

std::string_view GetSpriteClassName(SpriteClass cls);

#endif /* VIDEO_SPRITE_CLASS_H */
