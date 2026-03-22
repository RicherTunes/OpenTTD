/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file genworld_preview.cpp Lightweight map preview generation for the world generation window. */

#include "stdafx.h"
#include "genworld_preview.h"
#include "settings_type.h"
#include "core/random_func.hpp"
#include "core/math_func.hpp"
#include "debug.h"

#include <chrono>
#include <cmath>
#include <algorithm>

#include "safeguards.h"

/**
 * Simple Perlin noise for preview generation.
 * Uses the same algorithm as TGP but self-contained to avoid interfering
 * with the global _height_map state.
 */
static double PreviewIntNoise(long x, long y, uint32_t seed)
{
	long n = x + y * 7919 + seed;
	n = (n << 13) ^ n;
	return 1.0 - (double)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0;
}

static double PreviewInterpolatedNoise(double x, double y, uint32_t seed)
{
	int ix = (int)x;
	int iy = (int)y;
	double fx = x - ix;
	double fy = y - iy;

	/* Quintic smoothstep for smoother terrain */
	fx = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0);
	fy = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0);

	double v1 = PreviewIntNoise(ix,     iy,     seed);
	double v2 = PreviewIntNoise(ix + 1, iy,     seed);
	double v3 = PreviewIntNoise(ix,     iy + 1, seed);
	double v4 = PreviewIntNoise(ix + 1, iy + 1, seed);

	double i1 = v1 + fx * (v2 - v1);
	double i2 = v3 + fx * (v4 - v3);
	return i1 + fy * (i2 - i1);
}

static double PreviewPerlinNoise(double x, double y, int octaves, double persistence, uint32_t seed)
{
	double total = 0;
	double amplitude = 1.0;
	double frequency = 1.0;
	double max_val = 0;

	for (int i = 0; i < octaves; i++) {
		total += PreviewInterpolatedNoise(x * frequency, y * frequency, seed + i * 31337) * amplitude;
		max_val += amplitude;
		amplitude *= persistence;
		frequency *= 2.0;
	}

	if (max_val <= 0.0) return 0.0;
	return total / max_val;
}

/** Simple height-to-palette-colour mapping for terrain preview.
 * Uses a gradient from deep blue (water) through green (lowland) to brown/white (mountains). */
static uint8_t HeightToPreviewColour(int height, int max_height, bool is_water)
{
	if (is_water) return 0xCA; /* Deep blue water colour (palette index) */

	if (max_height <= 0) return 0x58; /* Default green */

	/* Normalize height to [0, 1] */
	double t = Clamp((double)height / max_height, 0.0, 1.0);

	/* Palette gradient: green (low) -> brown (mid) -> grey/white (high)
	 * These are OpenTTD DOS palette indices for terrain colours */
	if (t < 0.15) return 0x58; /* Dark green (lowland) */
	if (t < 0.30) return 0x59; /* Green */
	if (t < 0.45) return 0x5A; /* Light green */
	if (t < 0.55) return 0x5B; /* Yellow-green */
	if (t < 0.65) return 0x22; /* Brown (hills) */
	if (t < 0.75) return 0x23; /* Dark brown */
	if (t < 0.85) return 0x0A; /* Grey (mountains) */
	if (t < 0.95) return 0x0B; /* Light grey */
	return 0x0F; /* White (snow/peaks) */
}

/**
 * Generate a lightweight map preview using self-contained Perlin noise.
 * This produces a quick terrain preview without allocating the full tile map
 * or modifying global generation state.
 *
 * @param out Output preview data (pixels, dimensions).
 * @param seed Random seed for terrain generation.
 * @param map_x Log2 of map X size (e.g., 8 for 256 tiles).
 * @param map_y Log2 of map Y size (e.g., 8 for 256 tiles).
 * @param preview_w Width of preview image in pixels.
 * @param preview_h Height of preview image in pixels.
 * @return True if preview was generated successfully.
 */
bool GenerateMapPreview(MapPreviewData &out, uint32_t seed, uint8_t map_x, uint8_t map_y,
	uint16_t preview_w, uint16_t preview_h)
{
	auto t_start = std::chrono::steady_clock::now();

	if (preview_w == 0 || preview_h == 0) return false;
	if (map_x < 6 || map_y < 6) return false; /* Too small to preview */

	int map_size_x = 1 << map_x;
	int map_size_y = 1 << map_y;

	out.width = preview_w;
	out.height = preview_h;
	out.seed = seed;
	out.map_x = map_x;
	out.map_y = map_y;
	out.pixels.resize(preview_w * preview_h);

	/* Generate height values using multi-octave Perlin noise.
	 * This mirrors TGP's HeightMapGenerate but at preview resolution. */
	std::vector<double> heights(preview_w * preview_h);
	double max_h = 0;

	int octaves = std::min(6, (int)std::min(map_x, map_y) - 2);
	double smoothness = 0.55 - 0.05 * _settings_newgame.game_creation.tgen_smoothness;

	for (int y = 0; y < preview_h; y++) {
		for (int x = 0; x < preview_w; x++) {
			/* Map preview pixel to world coordinate */
			double wx = (double)x / preview_w * map_size_x;
			double wy = (double)y / preview_h * map_size_y;

			double h = PreviewPerlinNoise(wx * 0.01, wy * 0.01, octaves, smoothness, seed);
			h = (h + 1.0) * 0.5; /* Normalize from [-1,1] to [0,1] */

			/* Apply continent shape mask if configured */
			ContinentShape shape = _settings_newgame.game_creation.continent_shape;
			if (shape != ContinentShape::None) {
				double nx = (double)x / preview_w;
				double ny = (double)y / preview_h;
				double mask = 1.0;

				switch (shape) {
					case ContinentShape::Island: {
						double dx = nx - 0.5, dy = ny - 0.5;
						mask = std::max(0.0, 1.0 - (dx * dx + dy * dy) / 0.16);
						break;
					}
					case ContinentShape::Volcanic: {
						double dx = nx - 0.5, dy = ny - 0.5;
						double dist = sqrt(dx * dx + dy * dy);
						double peak = std::max(0.0, 1.0 - dist * 3.0);
						peak = peak * peak;
						double angle = atan2(dy, dx);
						double ridges = 0.5 + 0.5 * sin(angle * 6.0 + dist * 20.0);
						mask = std::max(peak, std::max(0.0, 1.0 - dist / 0.4) * (0.5 + 0.5 * ridges));
						break;
					}
					case ContinentShape::Fjords: {
						double dx = nx - 0.5, dy = ny - 0.5;
						double radial = std::max(0.0, 1.0 - (dx * dx + dy * dy) / 0.25);
						double noise = PreviewPerlinNoise(nx * 8.0, ny * 8.0, 3, 0.6, seed + 9999);
						mask = Clamp(radial * (0.5 + 0.3 * noise), 0.0, 1.0);
						break;
					}
					case ContinentShape::Peninsula: {
						double progress = ny;
						double width = 0.3 + 0.15 * sin(progress * 6.0);
						double center_offset = fabs(nx - 0.5);
						if (center_offset < width) {
							mask = std::max(0.0, 1.0 - center_offset / width) * std::max(0.0, 1.0 - progress * 1.2);
						} else {
							mask = 0.0;
						}
						break;
					}
					default:
						break;
				}
				h *= mask;
			}

			heights[y * preview_w + x] = h;
			if (h > max_h) max_h = h;
		}
	}

	/* Determine water level based on sea_level setting */
	double water_threshold = 0.35;
	if (_settings_newgame.difficulty.quantity_sea_lakes < 4) {
		static const double water_levels[] = {0.05, 0.25, 0.35, 0.45};
		water_threshold = water_levels[_settings_newgame.difficulty.quantity_sea_lakes];
	}

	/* Apply terrain height setting */
	int terrain_max = 15;
	GenworldMaxHeight mh = _settings_newgame.difficulty.terrain_type;
	switch (mh) {
		case GenworldMaxHeight::VeryFlat: terrain_max = 3; break;
		case GenworldMaxHeight::Flat:     terrain_max = 6; break;
		case GenworldMaxHeight::Hilly:    terrain_max = 10; break;
		case GenworldMaxHeight::Mountainous: terrain_max = 15; break;
		case GenworldMaxHeight::Alpinist: terrain_max = 25; break;
		default: terrain_max = 15; break;
	}

	/* Convert heights to palette colours */
	for (int y = 0; y < preview_h; y++) {
		for (int x = 0; x < preview_w; x++) {
			double h = heights[y * preview_w + x];
			bool is_water = (h < water_threshold * max_h) || (max_h <= 0);
			int tile_h = is_water ? 0 : (int)((h / std::max(max_h, 0.001)) * terrain_max);
			out.pixels[y * preview_w + x] = HeightToPreviewColour(tile_h, terrain_max, is_water);
		}
	}

	auto t_end = std::chrono::steady_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
	Debug(map, 2, "MapPreview: Generated {}x{} preview in {}ms (seed={})", preview_w, preview_h, ms, seed);

	return true;
}

/**
 * Generate a preview image from raw heightmap greyscale data.
 * Downscales the source image to preview resolution and applies the
 * same height-to-palette-colour mapping used by the TGP preview.
 *
 * @param out Output preview data (pixels, dimensions).
 * @param greyscale Source greyscale buffer (0-255), row-major (src_w * src_h).
 * @param src_w Width of the source heightmap in pixels.
 * @param src_h Height of the source heightmap in pixels.
 * @param preview_w Desired preview width in pixels.
 * @param preview_h Desired preview height in pixels.
 * @return True if preview was generated successfully.
 */
bool GenerateHeightmapPreview(MapPreviewData &out, const std::vector<uint8_t> &greyscale,
	uint src_w, uint src_h, uint16_t preview_w, uint16_t preview_h, HeightmapRotation rotation)
{
	if (preview_w == 0 || preview_h == 0) return false;
	if (src_w == 0 || src_h == 0) return false;
	if (greyscale.size() < (size_t)src_w * src_h) return false;

	out.width = preview_w;
	out.height = preview_h;
	out.seed = 0;
	out.map_x = 0;
	out.map_y = 0;
	out.pixels.resize(preview_w * preview_h);

	/* Find the maximum greyscale value for normalization */
	uint8_t max_grey = 0;
	for (size_t i = 0; i < (size_t)src_w * src_h; i++) {
		if (greyscale[i] > max_grey) max_grey = greyscale[i];
	}

	int terrain_max = 15;

	/* Downscale and convert to palette colours using nearest-neighbour sampling */
	for (int dy = 0; dy < preview_h; dy++) {
		for (int dx = 0; dx < preview_w; dx++) {
			/* Match the heightmap import orientation so the preview reflects
			 * the world layout the user will actually generate. */
			uint sx = 0;
			uint sy = 0;
			switch (rotation) {
				case HM_COUNTER_CLOCKWISE:
					sx = (uint)((uint64_t)(preview_w - 1 - dx) * src_w / preview_w);
					sy = (uint)((uint64_t)dy * src_h / preview_h);
					break;

				case HM_CLOCKWISE:
					sx = (uint)((uint64_t)dy * src_w / preview_h);
					sy = (uint)((uint64_t)dx * src_h / preview_w);
					break;

				default:
					NOT_REACHED();
			}
			if (sx >= src_w) sx = src_w - 1;
			if (sy >= src_h) sy = src_h - 1;

			uint8_t grey = greyscale[sy * src_w + sx];

			/* Height 0 in heightmap is water (sea level) */
			bool is_water = (grey == 0);
			int tile_h = is_water ? 0 : (max_grey > 0 ? (int)((double)grey / max_grey * terrain_max) : 0);

			out.pixels[dy * preview_w + dx] = HeightToPreviewColour(tile_h, terrain_max, is_water);
		}
	}

	Debug(map, 2, "HeightmapPreview: Generated {}x{} preview from {}x{} source (rotation={})", preview_w, preview_h, src_w, src_h, rotation);

	return true;
}

/**
 * Convert a harbor quality score to a palette colour for minimap overlay.
 * @param score Harbor quality score (0-255). Zero means no harbor potential.
 * @return Palette colour index: 0 for no overlay, blue gradient for harbor quality.
 */
uint8_t HarborScoreToColour(uint8_t score)
{
	if (score == 0) return 0;       /* No overlay */
	if (score < 64) return 0x98;    /* Light blue */
	if (score < 128) return 0x99;   /* Medium blue */
	if (score < 192) return 0x9A;   /* Blue */
	return 0x9B;                    /* Bright blue */
}
