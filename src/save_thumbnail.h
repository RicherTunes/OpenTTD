/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file save_thumbnail.h Save game thumbnail generation. */

#ifndef SAVE_THUMBNAIL_H
#define SAVE_THUMBNAIL_H

#include "genworld_preview.h"

#include <string>

inline constexpr uint16_t THUMBNAIL_WIDTH = 128;
inline constexpr uint16_t THUMBNAIL_HEIGHT = 96;

bool GenerateSaveThumbnail(MapPreviewData &out);
bool WriteThumbnailBMP(const MapPreviewData &preview, const std::string &filename);
std::string DerivePreviewPath(const std::string &save_path);

#endif /* SAVE_THUMBNAIL_H */
