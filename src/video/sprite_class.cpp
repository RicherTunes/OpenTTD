/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file sprite_class.cpp Per-pixel sprite classification state. */

#include "../stdafx.h"
#include "sprite_class.h"

#include "../safeguards.h"

/** Global sprite classification state, accessed by blitters and viewport code. */
SpriteClassState _sprite_class;

/**
 * Get a human-readable name for a sprite classification.
 * @param cls The sprite class value.
 * @return Name string (never null, returns "unknown" for invalid values).
 */
std::string_view GetSpriteClassName(SpriteClass cls)
{
	switch (cls) {
		case SPRITE_CLASS_UNKNOWN:    return "unknown";
		case SPRITE_CLASS_TERRAIN:    return "terrain";
		case SPRITE_CLASS_WATER:      return "water";
		case SPRITE_CLASS_VEGETATION: return "vegetation";
		case SPRITE_CLASS_STRUCTURE:  return "structure";
		case SPRITE_CLASS_VEHICLE:    return "vehicle";
		case SPRITE_CLASS_EFFECT:     return "effect";
		case SPRITE_CLASS_UI:         return "ui";
		default:                      return "unknown";
	}
}
