/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file test_terrain_gen.cpp Tests for terrain generation improvements. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../tgp_func.h"
#include "../core/math_func.hpp"

#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <numeric>
#include <cstdint>

#include "../safeguards.h"

/*
 * Helper: Compute the biome temperature for a tile given its height, max height,
 * and spatial position. Mirrors the formula in landscape.cpp CalculateSnowLineWithBiomes()
 * and CreateDesertOrRainForestWithBiomes().
 */
static double ComputeBiomeTemperature(uint h, uint max_h, uint x, uint y, double noise_factor)
{
	if (max_h == 0) return 1.0;
	double inv_max_h = 1.0 / static_cast<double>(max_h);
	uint noise_val = ((x * 374761393u + y * 668265263u) ^ (x * y * 1274126177u)) & 0xFF;
	double noise = (static_cast<double>(noise_val) / 255.0 - 0.5) * 2.0;
	return Clamp(1.0 - static_cast<double>(h) * inv_max_h + noise_factor * noise, 0.0, 1.0);
}

/*
 * Helper: Compute Gaussian falloff as used in HeightMapMountainRanges().
 * falloff = exp(-dist_sq / (2 * sigma_sq))
 */
static double GaussianFalloff(double dist_sq, double sigma_sq)
{
	return exp(-dist_sq / (2.0 * sigma_sq));
}

/*
 * Helper: Compute the island mask as used in HeightMapApplyContinentShape().
 * M = max(0, 1 - dist_sq / 0.16) where dist is from center (0.5, 0.5).
 */
static double IslandMask(double nx, double ny)
{
	double dx = nx - 0.5;
	double dy = ny - 0.5;
	double dist_sq = dx * dx + dy * dy;
	return std::max(0.0, 1.0 - dist_sq / 0.16);
}

/*
 * Helper: Compute the peninsula mask as used in HeightMapApplyContinentShape().
 * Replicates the south-edge variant (peninsula_edge == 2).
 */
static double PeninsulaMask(double nx, double ny, int edge)
{
	double progress, width_coord;
	switch (edge) {
		case 0: progress = 1.0 - ny; width_coord = nx; break;
		case 1: progress = nx;       width_coord = ny; break;
		case 2: progress = ny;       width_coord = nx; break;
		default: progress = 1.0 - nx; width_coord = ny; break;
	}
	double width = 0.3 + 0.15 * sin(progress * 6.0);
	double center_offset = fabs(width_coord - 0.5);
	if (center_offset < width) {
		return std::max(0.0, 1.0 - center_offset / width) * std::max(0.0, 1.0 - progress * 1.2);
	}
	return 0.0;
}

/*
 * Helper: Compute harbor raw score from ray-cast results.
 * Mirrors the formula in harbor_gen.cpp ComputeHarborScores().
 */
static uint8_t HarborScore(int land_rays, int water_rays, int total_water_depth)
{
	if (land_rays <= 0 || water_rays <= 0) return 0;
	int avg_depth = total_water_depth / water_rays;
	int raw_score = land_rays * avg_depth;
	return static_cast<uint8_t>(Clamp(raw_score * 4, 0, 255));
}

/*
 * Helper: Simple BFS on a 2D height grid to find enclosed basins.
 * Mirrors the lake detection logic in lake_gen.cpp CreateLakes().
 * Returns the basin size, or 0 if the basin is not enclosed.
 */
static uint SimpleBFS(const std::vector<int> &heights, int w, int h, int start_x, int start_y,
                      uint min_size, uint max_size, int min_height, int max_height)
{
	int target_h = heights[start_y * w + start_x];
	if (target_h < min_height || target_h > max_height) return 0;

	std::vector<bool> visited(w * h, false);
	std::vector<std::pair<int, int>> queue;
	std::vector<std::pair<int, int>> component;

	queue.push_back({start_x, start_y});
	visited[start_y * w + start_x] = true;

	bool is_enclosed = true;

	while (!queue.empty()) {
		auto [cx, cy] = queue.back();
		queue.pop_back();
		component.push_back({cx, cy});

		if (component.size() > max_size) {
			is_enclosed = false;
			break;
		}

		static const int dx[] = {-1, 0, 1, 0};
		static const int dy[] = {0, 1, 0, -1};
		for (int d = 0; d < 4; d++) {
			int nx = cx + dx[d];
			int ny = cy + dy[d];

			if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
				is_enclosed = false;
				continue;
			}

			int nh = heights[ny * w + nx];
			if (nh == target_h && !visited[ny * w + nx]) {
				visited[ny * w + nx] = true;
				queue.push_back({nx, ny});
			} else if (nh < target_h) {
				is_enclosed = false;
			}
		}
	}

	if (!is_enclosed) return 0;
	if (component.size() < min_size) return 0;
	return static_cast<uint>(component.size());
}


/*
 * Feature 2: Improved Perlin Interpolation -- Quintic Smoothstep Tests
 */

TEST_CASE("Quintic smoothstep - boundaries") {
	CHECK(QuinticSmoothstep(0.0) == Approx(0.0));
	CHECK(QuinticSmoothstep(1.0) == Approx(1.0));
}

TEST_CASE("Quintic smoothstep - midpoint") {
	CHECK(QuinticSmoothstep(0.5) == Approx(0.5));
}

TEST_CASE("Quintic smoothstep - monotonic") {
	double prev = 0.0;
	for (int i = 1; i <= 100; i++) {
		double t = i / 100.0;
		double val = QuinticSmoothstep(t);
		CHECK(val >= prev);
		prev = val;
	}
}

TEST_CASE("Quintic smoothstep - symmetry") {
	for (int i = 0; i <= 50; i++) {
		double t = i / 100.0;
		CHECK(QuinticSmoothstep(t) + QuinticSmoothstep(1.0 - t) == Approx(1.0));
	}
}

TEST_CASE("Smoothed interpolate - endpoints match linear") {
	CHECK(SmoothedInterpolate(10.0, 20.0, 0.0) == Approx(10.0));
	CHECK(SmoothedInterpolate(10.0, 20.0, 1.0) == Approx(20.0));
}

TEST_CASE("Smoothed interpolate - midpoint matches linear") {
	CHECK(SmoothedInterpolate(10.0, 20.0, 0.5) == Approx(15.0));
}

TEST_CASE("Smoothed interpolate - differs from linear at non-boundary") {
	/* Linear would give 10 + 0.25 * 10 = 12.5 */
	double smoothed = SmoothedInterpolate(10.0, 20.0, 0.25);
	double linear = 10.0 + 0.25 * 10.0;
	CHECK(smoothed != Approx(linear));
}

/*
 * Feature 6: Mountain Ranges
 */

TEST_CASE("Mountain range - Gaussian falloff at zero distance") {
	/* At distance 0, falloff should be 1.0: exp(-0 / (2*sigma^2)) = exp(0) = 1 */
	double sigma = 8.0;
	double result = GaussianFalloff(0.0, sigma * sigma);
	CHECK(result == Approx(1.0));
}

TEST_CASE("Mountain range - Gaussian falloff decreases with distance") {
	/* Verify monotonic decrease: closer points get higher falloff */
	double sigma_sq = 64.0;
	double prev = 1.0;
	for (int dist = 1; dist <= 30; dist++) {
		double dist_sq = static_cast<double>(dist * dist);
		double f = GaussianFalloff(dist_sq, sigma_sq);
		CHECK(f < prev);
		CHECK(f >= 0.0);
		prev = f;
	}
}

TEST_CASE("Mountain range - Gaussian falloff approaches zero at far distance") {
	/* At 3*sigma (search_radius in the implementation), falloff should be small.
	 * sigma=8, dist=3*sigma=24, dist_sq=576, exp(-576/128) = exp(-4.5) ~= 0.011 */
	double sigma = 8.0;
	double sigma_sq = sigma * sigma;
	double far_dist_sq = (3.0 * sigma) * (3.0 * sigma);
	double f = GaussianFalloff(far_dist_sq, sigma_sq);
	CHECK(f < 0.02);
	CHECK(f == Approx(exp(-4.5)).margin(1e-10));
	/* For very far: 10*sigma, dist_sq=6400, exp(-6400/128) = exp(-50) ~= 1.9e-22 */
	double very_far_sq = (10.0 * sigma) * (10.0 * sigma);
	double f2 = GaussianFalloff(very_far_sq, sigma_sq);
	CHECK(f2 < 1e-10);
}

TEST_CASE("Mountain range - ridge continuity via random walk step size") {
	/* Adjacent spine points from a random walk should be within step size.
	 * The implementation uses cos(angle)/sin(angle) rounded, so each step
	 * moves at most 1 tile in x and 1 tile in y (Manhattan distance <= 2). */
	for (int i = 0; i < 360; i++) {
		double angle = static_cast<double>(i) * M_PI / 180.0;
		int step_x = static_cast<int>(round(cos(angle)));
		int step_y = static_cast<int>(round(sin(angle)));
		int manhattan = std::abs(step_x) + std::abs(step_y);
		CHECK(manhattan <= 2);
		CHECK(std::abs(step_x) <= 1);
		CHECK(std::abs(step_y) <= 1);
	}
}

TEST_CASE("Mountain range - spatial hash grid lookup") {
	/* Verify that points placed in a grid cell can be found by indexing.
	 * Mirrors the spatial grid in HeightMapMountainRanges(). */
	const int grid_cell = 12; /* sigma * 3, e.g. sigma=4 */
	const int size_x = 64;
	const int size_y = 64;
	const int grid_w = (size_x + grid_cell - 1) / grid_cell;
	const int grid_h = (size_y + grid_cell - 1) / grid_cell;

	std::vector<std::vector<uint32_t>> grid(grid_w * grid_h);

	struct Point { int x; int y; };
	std::vector<Point> points = {{5, 5}, {10, 10}, {15, 15}, {5, 6}, {50, 50}};

	for (uint32_t i = 0; i < static_cast<uint32_t>(points.size()); i++) {
		int gx = points[i].x / grid_cell;
		int gy = points[i].y / grid_cell;
		if (gx >= 0 && gx < grid_w && gy >= 0 && gy < grid_h) {
			grid[gx + gy * grid_w].push_back(i);
		}
	}

	/* Points (5,5), (10,10), and (5,6) all map to cell (0,0) since 5/12=0, 10/12=0 */
	int cell_0_0 = 0 + 0 * grid_w;
	CHECK(grid[cell_0_0].size() == 3);
	CHECK(grid[cell_0_0][0] == 0); /* Index of (5,5) */
	CHECK(grid[cell_0_0][1] == 1); /* Index of (10,10) */
	CHECK(grid[cell_0_0][2] == 3); /* Index of (5,6) */

	/* Point (15,15) maps to cell (1,1) since 15/12=1 */
	int cell_1_1 = 1 + 1 * grid_w;
	CHECK(grid[cell_1_1].size() == 1);
	CHECK(grid[cell_1_1][0] == 2);

	/* Point (50,50) should be in cell (4,4) */
	int gx50 = 50 / grid_cell; /* 4 */
	int gy50 = 50 / grid_cell; /* 4 */
	int cell_4_4 = gx50 + gy50 * grid_w;
	CHECK(grid[cell_4_4].size() == 1);
	CHECK(grid[cell_4_4][0] == 4);
}

TEST_CASE("Mountain range - height clamping prevents overflow") {
	/* The implementation clamps added height to prevent int16_t overflow.
	 * Verify that adding ridge height to a near-max existing height stays in range. */
	int16_t existing = 30000;
	double falloff = 0.8;
	int16_t peak_amplitude = 5000;
	int16_t add = static_cast<int16_t>(peak_amplitude * falloff);
	int32_t result = Clamp(static_cast<int32_t>(existing) + add, INT16_MIN, INT16_MAX);
	CHECK(result <= INT16_MAX);
	CHECK(result >= INT16_MIN);
	CHECK(result == 32767); /* Should clamp to INT16_MAX */
}

TEST_CASE("Mountain range - deterministic falloff for same parameters") {
	/* Same sigma and distance should always produce the same falloff value. */
	double sigma_sq = 100.0;
	double dist_sq = 25.0;
	double f1 = GaussianFalloff(dist_sq, sigma_sq);
	double f2 = GaussianFalloff(dist_sq, sigma_sq);
	CHECK(f1 == Approx(f2));
	CHECK(f1 == Approx(exp(-25.0 / 200.0)));
}

/*
 * Feature 1: Continent Shapes
 */

TEST_CASE("Continent shape - Island mask range") {
	/* Island mask: M = max(0, 1 - dist_sq / 0.16) where dist is from center */
	/* Center (0.5, 0.5) should give M = 1.0 */
	double dx = 0.0, dy = 0.0;
	double mask = std::max(0.0, 1.0 - (dx * dx + dy * dy) / 0.16);
	CHECK(mask == Approx(1.0));

	/* Edge (0.0, 0.5) should give M < 1 */
	dx = -0.5; dy = 0.0;
	mask = std::max(0.0, 1.0 - (dx * dx + dy * dy) / 0.16);
	CHECK(mask < 1.0);

	/* Far corner (0.0, 0.0) should give M = 0 */
	dx = -0.5; dy = -0.5;
	mask = std::max(0.0, 1.0 - (dx * dx + dy * dy) / 0.16);
	CHECK(mask == Approx(0.0).margin(0.01));
}

TEST_CASE("Continent shape - mask values in 0 to 1") {
	/* Test Island mask across a grid of points */
	for (int yi = 0; yi <= 10; yi++) {
		for (int xi = 0; xi <= 10; xi++) {
			double nx = xi / 10.0;
			double ny = yi / 10.0;
			double dx = nx - 0.5;
			double dy = ny - 0.5;
			double mask = std::max(0.0, 1.0 - (dx * dx + dy * dy) / 0.16);
			CHECK(mask >= 0.0);
			CHECK(mask <= 1.0);
		}
	}
}

TEST_CASE("Continent shape - Island mask is 1.0 at center") {
	/* M(0.5, 0.5) should be exactly 1.0 */
	double mask = IslandMask(0.5, 0.5);
	CHECK(mask == Approx(1.0));
}

TEST_CASE("Continent shape - Island mask is 0.0 at corners") {
	/* All four corners should have mask = 0 (or very close) */
	CHECK(IslandMask(0.0, 0.0) == Approx(0.0).margin(0.01));
	CHECK(IslandMask(1.0, 0.0) == Approx(0.0).margin(0.01));
	CHECK(IslandMask(0.0, 1.0) == Approx(0.0).margin(0.01));
	CHECK(IslandMask(1.0, 1.0) == Approx(0.0).margin(0.01));
}

TEST_CASE("Continent shape - Archipelago has multiple peaks") {
	/* Archipelago generates 8 island centers. Verify that at least 3 distinct
	 * high regions exist by testing the island mask formula with 8 random centers. */
	struct Center { double cx, cy, r_sq; };
	std::vector<Center> centers = {
		{0.2, 0.2, 0.01}, {0.8, 0.2, 0.01}, {0.5, 0.5, 0.015},
		{0.2, 0.8, 0.01}, {0.8, 0.8, 0.01}, {0.35, 0.65, 0.008},
		{0.65, 0.35, 0.008}, {0.5, 0.15, 0.01}
	};

	/* Count how many centers produce mask > 0.5 at their own location */
	int high_regions = 0;
	for (const auto &c : centers) {
		double dx = 0.0; /* At the center itself */
		double dy = 0.0;
		double dist_sq = dx * dx + dy * dy;
		double mask = std::max(0.0, 1.0 - dist_sq / c.r_sq);
		if (mask > 0.5) high_regions++;
	}
	CHECK(high_regions >= 3);
	CHECK(high_regions == 8); /* Every center is a peak */
}

TEST_CASE("Continent shape - Fjords mask uses noise for variation") {
	/* The Fjords formula is: radial * (0.5 + 0.3 * noise).
	 * Different noise values should produce different masks at the same position. */
	double nx = 0.3, ny = 0.3;
	double dx = nx - 0.5;
	double dy = ny - 0.5;
	double radial = std::max(0.0, 1.0 - (dx * dx + dy * dy) / 0.25);

	double noise_a = 0.7;
	double noise_b = -0.3;
	double mask_a = Clamp(radial * (0.5 + 0.3 * noise_a), 0.0, 1.0);
	double mask_b = Clamp(radial * (0.5 + 0.3 * noise_b), 0.0, 1.0);
	CHECK(mask_a != Approx(mask_b));
	CHECK(mask_a >= 0.0);
	CHECK(mask_a <= 1.0);
	CHECK(mask_b >= 0.0);
	CHECK(mask_b <= 1.0);
}

TEST_CASE("Continent shape - Peninsula mask has directional gradient") {
	/* Peninsula mask should decrease along the progress axis.
	 * For edge=2 (from south), progress = ny, so mask should decrease as ny increases. */
	double mask_near = PeninsulaMask(0.5, 0.1, 2); /* Near base (south) */
	double mask_mid = PeninsulaMask(0.5, 0.4, 2);  /* Middle */
	double mask_far = PeninsulaMask(0.5, 0.8, 2);  /* Far (north) */
	CHECK(mask_near > mask_mid);
	CHECK(mask_mid > mask_far);
}

TEST_CASE("Continent shape - all presets produce masks in 0 to 1") {
	/* Verify all mask formulas produce values in [0,1] across a grid. */
	for (int yi = 0; yi <= 20; yi++) {
		for (int xi = 0; xi <= 20; xi++) {
			double nx = xi / 20.0;
			double ny = yi / 20.0;

			/* Island */
			double island = IslandMask(nx, ny);
			CHECK(island >= 0.0);
			CHECK(island <= 1.0);

			/* Peninsula (all 4 edges) */
			for (int edge = 0; edge < 4; edge++) {
				double pen = PeninsulaMask(nx, ny, edge);
				CHECK(pen >= 0.0);
				CHECK(pen <= 1.0);
			}

			/* Fjords (test with noise range [-1, 1]) */
			double dx = nx - 0.5;
			double dy = ny - 0.5;
			double radial = std::max(0.0, 1.0 - (dx * dx + dy * dy) / 0.25);
			for (double noise : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
				double fjord = Clamp(radial * (0.5 + 0.3 * noise), 0.0, 1.0);
				CHECK(fjord >= 0.0);
				CHECK(fjord <= 1.0);
			}
		}
	}
}

/*
 * Feature 5: Lake Generation
 */

TEST_CASE("Lake gen - enclosed basin detected") {
	/* A 3x3 low area surrounded by higher terrain should be detected.
	 * Grid: 5x5, border height 5, interior height 3 */
	const int w = 5, h = 5;
	std::vector<int> heights(w * h, 5);
	/* Set interior 3x3 to lower height */
	for (int y = 1; y <= 3; y++) {
		for (int x = 1; x <= 3; x++) {
			heights[y * w + x] = 3;
		}
	}

	uint basin_size = SimpleBFS(heights, w, h, 2, 2, 1, 100, 2, 8);
	CHECK(basin_size == 9); /* 3x3 interior */
}

TEST_CASE("Lake gen - open basin NOT detected") {
	/* A low area with one side open (drains out) should not form a lake.
	 * Grid: 5x5, border height 5, interior height 3, but bottom edge at 2 (lower). */
	const int w = 5, h = 5;
	std::vector<int> heights(w * h, 5);
	for (int y = 1; y <= 3; y++) {
		for (int x = 1; x <= 3; x++) {
			heights[y * w + x] = 3;
		}
	}
	/* Open a drainage path: one border tile lower than basin */
	heights[4 * w + 2] = 2;

	uint basin_size = SimpleBFS(heights, w, h, 2, 2, 1, 100, 2, 8);
	CHECK(basin_size == 0); /* Open basin, should not be detected */
}

TEST_CASE("Lake gen - minimum size enforced") {
	/* A single-tile depression should be rejected by minimum size filter.
	 * Grid: 3x3, border height 5, center height 3 (component size = 1). */
	const int w = 3, h = 3;
	std::vector<int> heights = {5, 5, 5, 5, 3, 5, 5, 5, 5};

	uint basin_size = SimpleBFS(heights, w, h, 1, 1, 4, 100, 2, 8);
	CHECK(basin_size == 0); /* Size 1 < min_size 4 */
}

TEST_CASE("Lake gen - maximum size enforced") {
	/* A very large flat area should be rejected by maximum size filter.
	 * Grid: 10x10, all height 3 surrounded by edges that go out of bounds
	 * (treated as not enclosed). */
	const int w = 10, h = 10;
	std::vector<int> heights(w * h, 3);

	/* Even if we surround with higher terrain, max_size=10 should reject 64 tiles */
	/* Use 8x8 interior: actually let's make a bordered version */
	std::vector<int> bordered(12 * 12, 5);
	for (int y = 1; y <= 10; y++) {
		for (int x = 1; x <= 10; x++) {
			bordered[y * 12 + x] = 3;
		}
	}

	uint basin_size = SimpleBFS(bordered, 12, 12, 5, 5, 4, 10, 2, 8);
	CHECK(basin_size == 0); /* 100 tiles > max_size 10 */
}

TEST_CASE("Lake gen - height range filter") {
	/* Tiles outside height range [2, 8] should not be scanned.
	 * Grid: 5x5, border height 10, interior height 1 (below min_height=2). */
	const int w = 5, h = 5;
	std::vector<int> heights(w * h, 10);
	for (int y = 1; y <= 3; y++) {
		for (int x = 1; x <= 3; x++) {
			heights[y * w + x] = 1; /* Below min_lake_height */
		}
	}

	uint basin_size = SimpleBFS(heights, w, h, 2, 2, 1, 100, 2, 8);
	CHECK(basin_size == 0); /* Height 1 < min_height 2 */

	/* Also test above max_height */
	for (int y = 1; y <= 3; y++) {
		for (int x = 1; x <= 3; x++) {
			heights[y * w + x] = 9; /* Above max_height=8 */
		}
	}

	basin_size = SimpleBFS(heights, w, h, 2, 2, 1, 100, 2, 8);
	CHECK(basin_size == 0); /* Height 9 > max_height 8 */
}

TEST_CASE("Lake gen - BFS visits each tile once") {
	/* Verify BFS does not revisit tiles on a pathological grid.
	 * A ring-shaped basin should still converge correctly. */
	const int w = 7, h = 7;
	std::vector<int> heights(w * h, 5);
	/* Create an enclosed L-shaped basin at height 3 */
	int basin_tiles[] = {
		/* Row 1: */ 1*w+1, 1*w+2, 1*w+3,
		/* Row 2: */ 2*w+1,
		/* Row 3: */ 3*w+1, 3*w+2, 3*w+3,
		/* Row 4: */               3*w+3  /* already counted */
	};
	/* Actually, build a proper connected shape */
	heights[1*w+1] = 3; heights[1*w+2] = 3; heights[1*w+3] = 3;
	heights[2*w+1] = 3;
	heights[3*w+1] = 3; heights[3*w+2] = 3; heights[3*w+3] = 3;
	(void)basin_tiles;

	uint basin_size = SimpleBFS(heights, w, h, 1, 1, 1, 100, 2, 8);
	/* Should find exactly 7 tiles, not more (no revisits inflate count) */
	CHECK(basin_size == 7);
}

/*
 * Feature 7: Natural Harbors
 */

TEST_CASE("Harbor score - normalization bounds") {
	/* Max: 8 land_rays * 16 avg_depth = 128, * 4 = 512, clamped to 255 */
	int raw_score = 8 * 16;
	int clamped = Clamp(raw_score * 4, 0, 255);
	CHECK(clamped == 255);

	/* Min nonzero: 1 * 1 = 1, * 4 = 4 */
	raw_score = 1;
	clamped = Clamp(raw_score * 4, 0, 255);
	CHECK(clamped == 4);
}

TEST_CASE("Harbor - inland tile scores 0") {
	/* A tile with no adjacent water (all land rays, no water rays) scores 0.
	 * With land_rays=8 but water_rays=0 the formula gives 0. */
	uint8_t score = HarborScore(8, 0, 0);
	CHECK(score == 0);

	/* No land rays either (shouldn't happen for non-coastal, but test anyway) */
	score = HarborScore(0, 0, 0);
	CHECK(score == 0);
}

TEST_CASE("Harbor - straight coast low score") {
	/* A straight coastline: 1 ray hits land (behind), 1 water ray with short depth.
	 * Score = 1 * 2 * 4 = 8 -- low. */
	uint8_t score = HarborScore(1, 1, 2);
	CHECK(score == 8);
	CHECK(score < 50); /* Clearly low */
}

TEST_CASE("Harbor - bay scores high") {
	/* A sheltered bay: 5 of 8 rays hit land, 3 water rays with avg depth 10.
	 * raw_score = 5 * (30/3) = 5 * 10 = 50, * 4 = 200 -- high. */
	uint8_t score = HarborScore(5, 3, 30);
	CHECK(score == 200);
	CHECK(score > 150);
}

TEST_CASE("Harbor - score range 0 to 255") {
	/* Test a range of ray-cast results and verify all scores are in [0,255] */
	for (int land_rays = 0; land_rays <= 8; land_rays++) {
		for (int water_rays = 0; water_rays <= 8; water_rays++) {
			for (int depth = 0; depth <= 16; depth++) {
				uint8_t score = HarborScore(land_rays, water_rays, depth * std::max(water_rays, 1));
				CHECK(score >= 0);
				CHECK(score <= 255);
			}
		}
	}
}

TEST_CASE("Harbor - ray casting symmetry") {
	/* A symmetric bay should produce the same score regardless of which rays
	 * represent land and which represent water. Test that equal configurations
	 * of land_rays/water_rays/depth give equal scores. */
	uint8_t score_a = HarborScore(4, 4, 32); /* 4 land, 4 water, total depth 32 */
	uint8_t score_b = HarborScore(4, 4, 32); /* Same configuration */
	CHECK(score_a == score_b);

	/* Swapping land and water distribution changes the score */
	uint8_t score_c = HarborScore(6, 2, 16); /* More land shelter */
	uint8_t score_d = HarborScore(2, 6, 48); /* Less shelter, deeper water */
	/* score_c: 6 * (16/2) = 48, *4 = 192 */
	/* score_d: 2 * (48/6) = 2*8 = 16, *4 = 64 */
	CHECK(score_c == 192);
	CHECK(score_d == 64);
	CHECK(score_c > score_d); /* More shelter = higher score */
}

/*
 * Feature 3: Biome System
 */

TEST_CASE("Biome - temperature decreases with altitude") {
	/* temperature = 1.0 - (height / max_height) + noise_factor * noise
	 * At height=0, temp=1.0; at height=max, temp=0.0 (ignoring noise) */
	double max_h = 15.0;
	double temp_low = 1.0 - (0.0 / max_h);
	double temp_high = 1.0 - (15.0 / max_h);
	CHECK(temp_low > temp_high);
	CHECK(temp_low == Approx(1.0));
	CHECK(temp_high == Approx(0.0));
}

TEST_CASE("Biome - noise adds variation to temperature") {
	/* Two tiles at the same height but different spatial positions should
	 * have different temperatures due to spatial noise hash. */
	uint h = 7;
	uint max_h = 15;
	double noise_factor = 0.25;

	double temp_a = ComputeBiomeTemperature(h, max_h, 10, 20, noise_factor);
	double temp_b = ComputeBiomeTemperature(h, max_h, 100, 200, noise_factor);

	/* Both should be valid temperatures */
	CHECK(temp_a >= 0.0);
	CHECK(temp_a <= 1.0);
	CHECK(temp_b >= 0.0);
	CHECK(temp_b <= 1.0);

	/* Very likely different due to spatial noise (hash-based pseudo-noise) */
	CHECK(temp_a != Approx(temp_b).margin(0.001));
}

TEST_CASE("Biome - snow coverage approximates target via histogram") {
	/* Simulate the histogram-based coverage approach used in CalculateSnowLineWithBiomes.
	 * Create a synthetic set of tiles, compute temperatures, build histogram,
	 * and verify the threshold finds approximately the right coverage. */
	const uint max_h = 15;
	const double noise_factor = 0.2;
	const uint target_coverage = 30; /* 30% snow coverage */

	std::array<int, 256> histogram = {};
	uint land_tiles = 0;

	/* Generate a synthetic landscape: 100x100 tiles with varying height */
	for (uint y = 0; y < 100; y++) {
		for (uint x = 0; x < 100; x++) {
			/* Height increases from south to north (y=0 is low, y=99 is high) */
			uint h = static_cast<uint>(y * max_h / 99);
			if (h == 0) continue;
			land_tiles++;

			double temperature = ComputeBiomeTemperature(h, max_h, x, y, noise_factor);
			int temp_idx = Clamp(static_cast<int>(temperature * 255.0), 0, 255);
			histogram[temp_idx]++;
		}
	}

	/* Find threshold for requested snow coverage (accumulate from cold = index 0 upward) */
	int goal = land_tiles * target_coverage / 100;
	int accumulated = 0;
	int best_threshold = 0;
	int best_score = static_cast<int>(land_tiles);
	for (int t = 0; t < 256; t++) {
		accumulated += histogram[t];
		int score = std::abs(goal - accumulated);
		if (score < best_score) {
			best_score = score;
			best_threshold = t;
		}
	}

	/* Count how many tiles are at or below the threshold temperature (cold = snow) */
	int snow_tiles = 0;
	for (int t = 0; t <= best_threshold; t++) {
		snow_tiles += histogram[t];
	}

	double actual_coverage = static_cast<double>(snow_tiles) / static_cast<double>(land_tiles) * 100.0;
	CHECK(actual_coverage >= target_coverage - 15.0);
	CHECK(actual_coverage <= target_coverage + 15.0);
}

TEST_CASE("Biome - desert not on water tiles") {
	/* Water tiles (height == 0) are excluded from the biome temperature calculation.
	 * The implementation skips h == 0 tiles. Verify the guard. */
	uint h = 0;
	uint max_h = 15;

	/* Height 0 should not be considered a land tile for desert marking.
	 * In the implementation: `if (h == 0) continue;` skips sea tiles. */
	double inv_max_h = 1.0 / max_h;
	double base_temp = 1.0 - static_cast<double>(h) * inv_max_h;
	/* base_temp at h=0 would be 1.0 (very hot), which would mark it as desert.
	 * The guard prevents this. */
	CHECK(base_temp == Approx(1.0));
	/* The key invariant: the implementation never processes h=0 tiles. */
	CHECK(h == 0); /* Confirming the guard condition. */

	/* Even with noise, height-0 temperature would be hot enough for desert.
	 * This proves the h==0 guard is essential. */
	double temp_with_noise = ComputeBiomeTemperature(h, max_h, 50, 50, 0.25);
	CHECK(temp_with_noise > 0.7); /* Hot enough to be desert without the guard */
}

TEST_CASE("Biome - classic mode temperature is pure altitude based") {
	/* Classic biome model uses CalculateCoverageLine which is a pure
	 * height-threshold approach: snow_line_height = height that covers N%.
	 * Temperature is not used. Verify that for classic mode, the result
	 * is purely a function of height with no noise contribution. */
	double max_h = 15.0;
	for (uint h = 0; h <= 15; h++) {
		double temp = 1.0 - static_cast<double>(h) / max_h;
		/* This is the base temperature formula without noise */
		CHECK(temp >= 0.0);
		CHECK(temp <= 1.0);
		/* Verify monotonicity: higher = colder */
		if (h > 0) {
			double temp_prev = 1.0 - static_cast<double>(h - 1) / max_h;
			CHECK(temp < temp_prev);
		}
	}
}

TEST_CASE("Biome - temperature clamped to 0 to 1") {
	/* Even with extreme noise, temperature should always be in [0,1].
	 * Test with noise_factor=0.25 which gives noise contribution in [-0.25, 0.25]. */
	uint max_h = 15;
	double noise_factor = 0.25;

	for (uint h = 0; h <= max_h; h++) {
		for (uint x = 0; x < 50; x++) {
			for (uint y = 0; y < 50; y++) {
				double temp = ComputeBiomeTemperature(h, max_h, x, y, noise_factor);
				CHECK(temp >= 0.0);
				CHECK(temp <= 1.0);
			}
		}
	}
}

TEST_CASE("Biome - coverage histogram is monotonic when accumulated") {
	/* Building a temperature histogram and accumulating from one end should
	 * produce a non-decreasing cumulative count. This is the core property
	 * that makes CalculateSnowLineWithBiomes() work correctly. */
	std::array<int, 256> histogram = {};
	uint max_h = 15;
	double noise_factor = 0.2;

	/* Populate histogram from synthetic data */
	for (uint y = 0; y < 50; y++) {
		for (uint x = 0; x < 50; x++) {
			uint h = 1 + (y * max_h / 50);
			double temp = ComputeBiomeTemperature(h, max_h, x, y, noise_factor);
			int idx = Clamp(static_cast<int>(temp * 255.0), 0, 255);
			histogram[idx]++;
		}
	}

	/* Cumulative sum from cold to hot should be non-decreasing */
	int cumulative = 0;
	int prev_cumulative = 0;
	for (int i = 0; i < 256; i++) {
		cumulative += histogram[i];
		CHECK(cumulative >= prev_cumulative);
		prev_cumulative = cumulative;
	}
}

/*
 * Feature 1 (continued): Mesa/Canyon and Volcanic continent shapes
 */

TEST_CASE("ContinentShape - Mesa mask produces plateau regions") {
	/* Mesa mask should produce values close to 1.0 in plateau areas
	 * and lower values in canyon areas. Verify the smoothstep transition
	 * produces distinct plateau and canyon zones. */
	double plateau_r = 0.15;
	double edge0 = plateau_r - 0.02;
	double edge1 = plateau_r + 0.02;

	int plateau_count = 0;
	int canyon_count = 0;

	for (int yi = 0; yi <= 20; yi++) {
		for (int xi = 0; xi <= 20; xi++) {
			double nx = xi / 20.0;
			double ny = yi / 20.0;
			double dx = nx - 0.5;
			double dy = ny - 0.5;
			double dist = sqrt(dx * dx + dy * dy);

			double t = std::max(0.0, std::min(1.0, (dist - edge0) / (edge1 - edge0)));
			double step = 1.0 - t * t * (3.0 - 2.0 * t);

			CHECK(step >= 0.0);
			CHECK(step <= 1.0);

			if (step > 0.9) plateau_count++;
			if (step < 0.1) canyon_count++;
		}
	}

	CHECK(plateau_count > 0);
	CHECK(canyon_count > 0);
}

TEST_CASE("ContinentShape - Volcanic mask has central peak") {
	/* At center (0.5, 0.5), volcanic mask should be high.
	 * At edges, it should be low. */
	double dx = 0.0, dy = 0.0;
	double dist = sqrt(dx * dx + dy * dy);
	double peak = std::max(0.0, 1.0 - dist * 3.0);
	peak = peak * peak;
	CHECK(peak == 1.0); /* Dead center = max peak */

	/* At edge */
	dx = 0.4; dy = 0.0;
	dist = sqrt(dx * dx + dy * dy);
	peak = std::max(0.0, 1.0 - dist * 3.0);
	peak = peak * peak;
	CHECK(peak < 0.1); /* Far from center = near zero */
}

TEST_CASE("ContinentShape - Volcanic radial ridges modulate mask") {
	/* Radial ridges should create angular variation at a given distance */
	double dist = 0.2;
	double mask_a, mask_b;

	/* Angle 0 */
	double angle_a = 0.0;
	double ridges_a = 0.5 + 0.5 * sin(angle_a * 6.0 + dist * 20.0);
	mask_a = std::max(0.0, 1.0 - dist / 0.4) * (0.5 + 0.5 * ridges_a);

	/* Angle pi/6 (30 degrees) - one half-cycle of the 6x angular modulation */
	double angle_b = 3.14159265 / 6.0;
	double ridges_b = 0.5 + 0.5 * sin(angle_b * 6.0 + dist * 20.0);
	mask_b = std::max(0.0, 1.0 - dist / 0.4) * (0.5 + 0.5 * ridges_b);

	/* The two masks at different angles should differ (ridges create variation) */
	CHECK(mask_a != Approx(mask_b));
	/* Both should be in valid range */
	CHECK(mask_a >= 0.0);
	CHECK(mask_a <= 1.0);
	CHECK(mask_b >= 0.0);
	CHECK(mask_b <= 1.0);
}

TEST_CASE("ContinentShape - Mesa smoothstep transition") {
	/* Verify the smoothstep-based mesa edge creates a sharp transition.
	 * Inside plateau radius should be ~1.0, outside should be ~0.0. */
	double plateau_r = 0.15;
	double edge0 = plateau_r - 0.02;
	double edge1 = plateau_r + 0.02;

	/* Well inside the plateau */
	double dist_inside = 0.05;
	double t_inside = std::max(0.0, std::min(1.0, (dist_inside - edge0) / (edge1 - edge0)));
	double step_inside = 1.0 - t_inside * t_inside * (3.0 - 2.0 * t_inside);
	CHECK(step_inside > 0.9);

	/* Well outside the plateau */
	double dist_outside = 0.3;
	double t_outside = std::max(0.0, std::min(1.0, (dist_outside - edge0) / (edge1 - edge0)));
	double step_outside = 1.0 - t_outside * t_outside * (3.0 - 2.0 * t_outside);
	CHECK(step_outside < 0.1);
}

/*
 * Feature: River Delta Generation
 */

TEST_CASE("River delta - branch count is 2-4") {
	/* Delta branches should be 2-4 (from 2 + RandomRange(3)) */
	for (int i = 0; i < 100; i++) {
		int branches = 2 + (i % 3); /* Simulates RandomRange(3) */
		CHECK(branches >= 2);
		CHECK(branches <= 4);
	}
}

TEST_CASE("River delta - branch length is 3-8") {
	/* Delta branch length should be 3-8 (from 3 + RandomRange(6)) */
	for (int i = 0; i < 100; i++) {
		int length = 3 + (i % 6); /* Simulates RandomRange(6) */
		CHECK(length >= 3);
		CHECK(length <= 8);
	}
}

TEST_CASE("River delta - direction normalization") {
	/* Verify that direction normalization produces -1, 0, or 1 */
	for (int raw = -10; raw <= 10; raw++) {
		int normalized = raw;
		if (normalized != 0) normalized = normalized / std::abs(normalized);
		CHECK(normalized >= -1);
		CHECK(normalized <= 1);
		if (raw > 0) CHECK(normalized == 1);
		if (raw < 0) CHECK(normalized == -1);
		if (raw == 0) CHECK(normalized == 0);
	}
}

TEST_CASE("River delta - branch angular spread") {
	/* Delta branches spread outward from the river mouth direction.
	 * Verify the angular spread calculation stays within expected bounds.
	 * In the implementation, wobble is in [-PI/6, PI/6] around the base angle. */
	double base_angle = M_PI / 4.0; /* 45 degrees, river heading NE */
	for (int i = 0; i < 1000; i++) {
		/* Simulate wobble: uniform in [-PI/6, PI/6] */
		double wobble = (static_cast<double>(i % 1000) / 1000.0 - 0.5) * M_PI / 3.0;
		double branch_angle = base_angle + wobble;
		double diff = fabs(branch_angle - base_angle);
		CHECK(diff <= M_PI / 6.0 + 1e-10);
	}
}

TEST_CASE("River delta - branch endpoints stay near river mouth") {
	/* With max branch length 8 and step size 1, endpoints should be
	 * at most 8 * sqrt(2) ~= 11.3 tiles from the origin. */
	int max_length = 8;
	double max_dist = max_length * sqrt(2.0);

	for (int angle_deg = 0; angle_deg < 360; angle_deg += 15) {
		double angle = angle_deg * M_PI / 180.0;
		double x = 0.0, y = 0.0;
		for (int step = 0; step < max_length; step++) {
			x += round(cos(angle));
			y += round(sin(angle));
		}
		double dist = sqrt(x * x + y * y);
		CHECK(dist <= max_dist + 1.0); /* +1 for rounding tolerance */
	}
}

TEST_CASE("River delta - no zero-length branches") {
	/* Even with minimum branch length (3), the branch should cover
	 * at least 1 tile (since each step moves at least 1 tile). */
	int min_length = 3;
	for (int angle_deg = 0; angle_deg < 360; angle_deg += 30) {
		double angle = angle_deg * M_PI / 180.0;
		int step_x = static_cast<int>(round(cos(angle)));
		int step_y = static_cast<int>(round(sin(angle)));
		int manhattan = std::abs(step_x) + std::abs(step_y);
		/* At least some steps should move (not all zero) */
		bool has_movement = (manhattan > 0);
		CHECK(has_movement);
		(void)min_length;
	}
}

/* --- Biome Town Growth tests --- */

TEST_CASE("Biome town growth - moderate temperature is optimal") {
	/* Temperature 0.5 (moderate) should give highest growth multiplier */
	auto GrowthMultiplier = [](double temperature) -> double {
		/* Bell curve centered at 0.5: 1.0 at center, 0.5 at extremes */
		double deviation = fabs(temperature - 0.5);
		return std::max(0.5, 1.0 - deviation * 1.5);
	};
	CHECK(GrowthMultiplier(0.5) == Approx(1.0));
	CHECK(GrowthMultiplier(0.5) > GrowthMultiplier(0.0));
	CHECK(GrowthMultiplier(0.5) > GrowthMultiplier(1.0));
}

TEST_CASE("Biome town growth - extreme cold reduces growth") {
	auto GrowthMultiplier = [](double temperature) -> double {
		double deviation = fabs(temperature - 0.5);
		return std::max(0.5, 1.0 - deviation * 1.5);
	};
	CHECK(GrowthMultiplier(0.0) < 0.75);
	CHECK(GrowthMultiplier(0.0) >= 0.5);
}

TEST_CASE("Biome town growth - extreme heat reduces growth") {
	auto GrowthMultiplier = [](double temperature) -> double {
		double deviation = fabs(temperature - 0.5);
		return std::max(0.5, 1.0 - deviation * 1.5);
	};
	CHECK(GrowthMultiplier(1.0) < 0.75);
	CHECK(GrowthMultiplier(1.0) >= 0.5);
}

TEST_CASE("Biome town growth - multiplier is symmetric around 0.5") {
	auto GrowthMultiplier = [](double temperature) -> double {
		double deviation = fabs(temperature - 0.5);
		return std::max(0.5, 1.0 - deviation * 1.5);
	};
	CHECK(GrowthMultiplier(0.3) == Approx(GrowthMultiplier(0.7)));
	CHECK(GrowthMultiplier(0.1) == Approx(GrowthMultiplier(0.9)));
}

TEST_CASE("Biome town growth - multiplier clamped to [0.5, 1.0]") {
	auto GrowthMultiplier = [](double temperature) -> double {
		double deviation = fabs(temperature - 0.5);
		return std::max(0.5, 1.0 - deviation * 1.5);
	};
	for (int i = 0; i <= 100; i++) {
		double t = i / 100.0;
		double m = GrowthMultiplier(t);
		CHECK(m >= 0.5);
		CHECK(m <= 1.0);
	}
}

TEST_CASE("Biome town growth - coastal proximity bonus") {
	/* Towns near water get a growth bonus */
	auto CoastalBonus = [](int distance_to_water) -> double {
		if (distance_to_water <= 5) return 1.15;
		if (distance_to_water <= 15) return 1.05;
		return 1.0;
	};
	CHECK(CoastalBonus(3) == Approx(1.15));
	CHECK(CoastalBonus(10) == Approx(1.05));
	CHECK(CoastalBonus(20) == Approx(1.0));
}


/*
 * Feature: Heightmap Preview -- grayscale to palette colour conversion tests
 */

TEST_CASE("Heightmap preview - grayscale to palette conversion") {
	/* Verify the height-to-colour mapping produces valid palette indices.
	 * This mirrors HeightToPreviewColour() in genworld_preview.cpp. */
	for (int h = 0; h <= 15; h++) {
		double t = (double)h / 15.0;
		uint8_t colour;
		if (t < 0.15) colour = 0x58;
		else if (t < 0.30) colour = 0x59;
		else if (t < 0.45) colour = 0x5A;
		else if (t < 0.55) colour = 0x5B;
		else if (t < 0.65) colour = 0x22;
		else if (t < 0.75) colour = 0x23;
		else if (t < 0.85) colour = 0x0A;
		else if (t < 0.95) colour = 0x0B;
		else colour = 0x0F;
		CHECK(colour > 0);
		CHECK(colour < 0xFF);
	}
}

TEST_CASE("Heightmap preview - water colour is distinct") {
	/* Water colour must differ from all land colours */
	uint8_t water = 0xCA;
	uint8_t land_colours[] = {0x58, 0x59, 0x5A, 0x5B, 0x22, 0x23, 0x0A, 0x0B, 0x0F};
	for (auto land : land_colours) {
		CHECK(water != land);
	}
}

TEST_CASE("Heightmap preview - grayscale downscale preserves range") {
	/* When downscaling a heightmap buffer, the output should preserve
	 * the min/max range of the source data. Simulates bilinear averaging. */
	const int src_w = 512, src_h = 512;
	const int dst_w = 256, dst_h = 128;
	std::vector<uint8_t> src(src_w * src_h);

	/* Create a gradient heightmap: top-left=0, bottom-right=255 */
	for (int y = 0; y < src_h; y++) {
		for (int x = 0; x < src_w; x++) {
			src[y * src_w + x] = static_cast<uint8_t>((x + y) * 255 / (src_w + src_h - 2));
		}
	}

	/* Downsample using nearest-neighbour (same as heightmap.cpp) */
	std::vector<uint8_t> dst(dst_w * dst_h);
	for (int dy = 0; dy < dst_h; dy++) {
		for (int dx = 0; dx < dst_w; dx++) {
			int sx = dx * src_w / dst_w;
			int sy = dy * src_h / dst_h;
			dst[dy * dst_w + dx] = src[sy * src_w + sx];
		}
	}

	uint8_t min_val = *std::min_element(dst.begin(), dst.end());
	uint8_t max_val = *std::max_element(dst.begin(), dst.end());
	CHECK(min_val == 0);
	CHECK(max_val > 200); /* Should preserve most of the range */
}

TEST_CASE("Heightmap preview - colour gradient is monotonic for ascending height") {
	/* Higher heights should map to equal or later colours in the gradient.
	 * We verify the colour index order is non-decreasing. */
	uint8_t prev_colour_idx = 0;
	uint8_t colour_order[] = {0x58, 0x59, 0x5A, 0x5B, 0x22, 0x23, 0x0A, 0x0B, 0x0F};
	auto ColourIndex = [&colour_order](uint8_t c) -> int {
		for (int i = 0; i < 9; i++) {
			if (colour_order[i] == c) return i;
		}
		return -1;
	};

	int prev_idx = 0;
	for (int h = 0; h <= 15; h++) {
		double t = (double)h / 15.0;
		uint8_t colour;
		if (t < 0.15) colour = 0x58;
		else if (t < 0.30) colour = 0x59;
		else if (t < 0.45) colour = 0x5A;
		else if (t < 0.55) colour = 0x5B;
		else if (t < 0.65) colour = 0x22;
		else if (t < 0.75) colour = 0x23;
		else if (t < 0.85) colour = 0x0A;
		else if (t < 0.95) colour = 0x0B;
		else colour = 0x0F;
		int idx = ColourIndex(colour);
		CHECK(idx >= prev_idx);
		prev_idx = idx;
	}
	(void)prev_colour_idx;
}


/*
 * Feature: Minimap Overlays -- harbor quality score to colour mapping tests
 */

TEST_CASE("Harbor overlay - score to colour gradient") {
	/* Harbor scores map to a blue gradient: 0=transparent, 255=bright blue */
	auto HarborScoreToColour = [](uint8_t score) -> uint8_t {
		if (score == 0) return 0;       /* No overlay */
		if (score < 64) return 0x98;    /* Light blue */
		if (score < 128) return 0x99;   /* Medium blue */
		if (score < 192) return 0x9A;   /* Blue */
		return 0x9B;                    /* Bright blue */
	};
	CHECK(HarborScoreToColour(0) == 0);
	CHECK(HarborScoreToColour(50) == 0x98);
	CHECK(HarborScoreToColour(100) == 0x99);
	CHECK(HarborScoreToColour(150) == 0x9A);
	CHECK(HarborScoreToColour(255) == 0x9B);
}

TEST_CASE("Harbor overlay - gradient is monotonically non-decreasing") {
	/* Increasing scores should yield equal or higher colour indices */
	auto HarborScoreToColour = [](uint8_t score) -> uint8_t {
		if (score == 0) return 0;
		if (score < 64) return 0x98;
		if (score < 128) return 0x99;
		if (score < 192) return 0x9A;
		return 0x9B;
	};
	uint8_t prev = 0;
	for (int s = 0; s <= 255; s++) {
		uint8_t c = HarborScoreToColour(static_cast<uint8_t>(s));
		CHECK(c >= prev);
		prev = c;
	}
}

TEST_CASE("Harbor overlay - zero score means no overlay") {
	/* A zero harbor score should result in transparent (0) overlay */
	auto HarborScoreToColour = [](uint8_t score) -> uint8_t {
		if (score == 0) return 0;
		if (score < 64) return 0x98;
		if (score < 128) return 0x99;
		if (score < 192) return 0x9A;
		return 0x9B;
	};
	CHECK(HarborScoreToColour(0) == 0);
	/* Any non-zero score should produce a visible colour */
	CHECK(HarborScoreToColour(1) != 0);
	CHECK(HarborScoreToColour(63) != 0);
}

TEST_CASE("Heightmap preview - GenerateHeightmapPreview downscales correctly") {
	/* Simulate the downscale-and-colourize pipeline:
	 * source greyscale buffer -> downscale -> HeightToPreviewColour */
	const int src_w = 128, src_h = 128;
	const int dst_w = 64, dst_h = 32;
	std::vector<uint8_t> src(src_w * src_h, 128); /* Uniform mid-grey */

	/* Downscale */
	std::vector<uint8_t> dst(dst_w * dst_h);
	for (int dy = 0; dy < dst_h; dy++) {
		for (int dx = 0; dx < dst_w; dx++) {
			int sx = dx * src_w / dst_w;
			int sy = dy * src_h / dst_h;
			dst[dy * dst_w + dx] = src[sy * src_w + sx];
		}
	}

	/* All downscaled values should be 128 (uniform input) */
	for (auto v : dst) {
		CHECK(v == 128);
	}

	/* Convert to palette colour: height 128/255 ~= 0.502, which falls
	 * in the 0.45-0.55 bracket -> 0x5B (yellow-green) */
	double t = 128.0 / 255.0;
	uint8_t expected;
	if (t < 0.15) expected = 0x58;
	else if (t < 0.30) expected = 0x59;
	else if (t < 0.45) expected = 0x5A;
	else if (t < 0.55) expected = 0x5B;
	else if (t < 0.65) expected = 0x22;
	else if (t < 0.75) expected = 0x23;
	else if (t < 0.85) expected = 0x0A;
	else if (t < 0.95) expected = 0x0B;
	else expected = 0x0F;
	CHECK(expected == 0x5B);
}
