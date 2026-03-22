/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file save_thumbnail.cpp Save game thumbnail generation from live map data. */

#include "stdafx.h"
#include "save_thumbnail.h"
#include "genworld_preview.h"
#include "map_func.h"
#include "tile_map.h"
#include "palette_func.h"
#include "gfx_func.h"
#include "debug.h"
#include "fileio_func.h"

#include <fstream>
#include <algorithm>

#include "safeguards.h"

/**
 * Generate a save game thumbnail from the current map state.
 * Creates a small palette-indexed preview by sampling tile heights.
 *
 * @param out Output preview data (pixels, dimensions).
 * @return True if preview was generated successfully.
 */
bool GenerateSaveThumbnail(MapPreviewData &out)
{
	out.width = THUMBNAIL_WIDTH;
	out.height = THUMBNAIL_HEIGHT;
	out.pixels.resize(THUMBNAIL_WIDTH * THUMBNAIL_HEIGHT);

	uint map_sx = Map::SizeX();
	uint map_sy = Map::SizeY();
	if (map_sx == 0 || map_sy == 0) return false;

	/* Quick scan for max height */
	uint max_h = 0;
	for (uint y = 0; y < map_sy; y += 4) {
		for (uint x = 0; x < map_sx; x += 4) {
			TileIndex tile = TileXY(x, y);
			uint h = TileHeight(tile);
			if (h > max_h) max_h = h;
		}
	}

	/* Sample tiles at preview resolution */
	for (int py = 0; py < THUMBNAIL_HEIGHT; py++) {
		for (int px = 0; px < THUMBNAIL_WIDTH; px++) {
			uint tx = (uint)((uint64_t)px * map_sx / THUMBNAIL_WIDTH);
			uint ty = (uint)((uint64_t)py * map_sy / THUMBNAIL_HEIGHT);
			tx = std::min(tx, map_sx - 1);
			ty = std::min(ty, map_sy - 1);

			TileIndex tile = TileXY(tx, ty);
			uint h = TileHeight(tile);
			bool is_water = IsTileType(tile, TileType::Water);

			int tile_h = is_water ? 0 : (max_h > 0 ? (int)((double)h / max_h * 15) : 0);
			out.pixels[py * THUMBNAIL_WIDTH + px] = HeightToPreviewColour(tile_h, 15, is_water);
		}
	}

	Debug(misc, 2, "SaveThumbnail: Generated {}x{} preview", THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
	return true;
}

/**
 * Derive the preview thumbnail path from a save game path.
 * Simply appends ".preview.bmp" to the save path.
 *
 * @param save_path The path to the save game file.
 * @return The derived preview file path.
 */
std::string DerivePreviewPath(const std::string &save_path)
{
	return save_path + ".preview.bmp";
}

/**
 * Write a little-endian 16-bit value to a buffer.
 * @param buf Target buffer.
 * @param val Value to write.
 */
static void WriteLE16(uint8_t *buf, uint16_t val)
{
	buf[0] = val & 0xFF;
	buf[1] = (val >> 8) & 0xFF;
}

/**
 * Write a little-endian 32-bit value to a buffer.
 * @param buf Target buffer.
 * @param val Value to write.
 */
static void WriteLE32(uint8_t *buf, uint32_t val)
{
	buf[0] = val & 0xFF;
	buf[1] = (val >> 8) & 0xFF;
	buf[2] = (val >> 16) & 0xFF;
	buf[3] = (val >> 24) & 0xFF;
}

/**
 * Write a MapPreviewData as an 8bpp BMP file using the current game palette.
 *
 * @param preview Preview data with palette-indexed pixels.
 * @param filename Output file path.
 * @return True if file was written successfully.
 */
bool WriteThumbnailBMP(const MapPreviewData &preview, const std::string &filename)
{
	if (preview.width == 0 || preview.height == 0) return false;
	if (preview.pixels.size() < (size_t)preview.width * preview.height) return false;

	uint32_t row_stride = (preview.width + 3) & ~3; /* Rows padded to 4-byte boundary */
	uint32_t pixel_data_size = row_stride * preview.height;
	uint32_t palette_size = 256 * 4; /* 256 RGBQUAD entries */
	uint32_t header_size = 14 + 40; /* BMP file header + DIB header */
	uint32_t pixel_offset = header_size + palette_size;
	uint32_t file_size = pixel_offset + pixel_data_size;

	std::vector<uint8_t> bmp(file_size, 0);

	/* BMP file header (14 bytes) */
	bmp[0] = 'B';
	bmp[1] = 'M';
	WriteLE32(&bmp[2], file_size);
	/* Reserved fields at [6..9] are already 0 */
	WriteLE32(&bmp[10], pixel_offset);

	/* DIB header (BITMAPINFOHEADER, 40 bytes) */
	WriteLE32(&bmp[14], 40); /* Header size */
	WriteLE32(&bmp[18], preview.width);
	WriteLE32(&bmp[22], preview.height); /* Positive = bottom-up */
	WriteLE16(&bmp[26], 1); /* Colour planes */
	WriteLE16(&bmp[28], 8); /* Bits per pixel */
	/* Compression (0 = BI_RGB) at [30..33] already 0 */
	WriteLE32(&bmp[34], pixel_data_size);
	WriteLE32(&bmp[38], 2835); /* Horizontal resolution (72 DPI in pixels/meter) */
	WriteLE32(&bmp[42], 2835); /* Vertical resolution */
	WriteLE32(&bmp[46], 256); /* Colours in palette */
	/* Important colours at [50..53] already 0 (all important) */

	/* Write palette from current game palette (BGRA order for BMP) */
	for (int i = 0; i < 256; i++) {
		uint32_t offset = header_size + i * 4;
		bmp[offset + 0] = _cur_palette.palette[i].b;
		bmp[offset + 1] = _cur_palette.palette[i].g;
		bmp[offset + 2] = _cur_palette.palette[i].r;
		bmp[offset + 3] = 0; /* Reserved */
	}

	/* Write pixel data (BMP is bottom-up) */
	for (int y = 0; y < preview.height; y++) {
		int src_row = preview.height - 1 - y; /* Flip vertically */
		uint32_t dst_offset = pixel_offset + y * row_stride;
		for (int x = 0; x < preview.width; x++) {
			bmp[dst_offset + x] = preview.pixels[src_row * preview.width + x];
		}
	}

	/* Write file */
	std::ofstream file(filename, std::ios::binary);
	if (!file.is_open()) {
		Debug(misc, 0, "SaveThumbnail: Failed to open '{}' for writing", filename);
		return false;
	}

	file.write(reinterpret_cast<const char *>(bmp.data()), bmp.size());
	if (!file.good()) {
		Debug(misc, 0, "SaveThumbnail: Failed to write '{}' ({} bytes)", filename, bmp.size());
		return false;
	}

	Debug(misc, 1, "SaveThumbnail: Wrote {}x{} BMP to '{}' ({} bytes)", preview.width, preview.height, filename, file_size);
	return true;
}
