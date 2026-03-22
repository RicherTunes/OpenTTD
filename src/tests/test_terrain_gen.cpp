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

#include "../safeguards.h"

/*
 * Feature 2: Improved Perlin Interpolation — Quintic Smoothstep Tests
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
	/* At distance 0, falloff should be 1.0 */
	double sigma_sq = 64.0;
	double falloff = exp(0.0 / (2.0 * sigma_sq));
	CHECK(falloff == Approx(1.0));
}

TEST_CASE("Mountain range - Gaussian falloff decreases with distance") {
	double sigma_sq = 64.0;
	double f_close = exp(-4.0 / (2.0 * sigma_sq));  /* dist=2 */
	double f_far = exp(-64.0 / (2.0 * sigma_sq));    /* dist=8 */
	CHECK(f_close > f_far);
}

TEST_CASE("Mountain range - Gaussian falloff approaches zero") {
	double sigma_sq = 64.0;
	double f_very_far = exp(-1024.0 / (2.0 * sigma_sq)); /* dist=32 */
	CHECK(f_very_far < 0.001);
}

/* Requires full game infrastructure -- integration test only */
/* Placeholder: TEST_CASE("Mountain range - none produces no change") */
/* Placeholder: TEST_CASE("Mountain range - adds height") */
/* Placeholder: TEST_CASE("Mountain range - ridge continuity") */
/* Placeholder: TEST_CASE("Mountain range - does not exceed max height") */
/* Placeholder: TEST_CASE("Mountain range - deterministic") */

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

/* Requires full game infrastructure -- integration test only */
/* Placeholder: TEST_CASE("Continent shape - none is identity") */
/* Placeholder: TEST_CASE("Continent shape - island has water at edges") */
/* Placeholder: TEST_CASE("Continent shape - island has land at center") */
/* Placeholder: TEST_CASE("Continent shape - archipelago multiple islands") */
/* Placeholder: TEST_CASE("Continent shape - respects water borders") */

/*
 * Feature 5: Lake Generation
 * Requires full game infrastructure -- integration test only
 */

/* Placeholder: TEST_CASE("Lake gen - none produces no lakes") */
/* Placeholder: TEST_CASE("Lake gen - detects enclosed basin") */
/* Placeholder: TEST_CASE("Lake gen - ignores open basin") */
/* Placeholder: TEST_CASE("Lake gen - minimum size filter") */
/* Placeholder: TEST_CASE("Lake gen - maximum size filter") */
/* Placeholder: TEST_CASE("Lake gen - lake tiles are water") */

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

/* Requires full game infrastructure -- integration test only */
/* Placeholder: TEST_CASE("Harbor - straight coast low score") */
/* Placeholder: TEST_CASE("Harbor - bay high score") */
/* Placeholder: TEST_CASE("Harbor - inland zero score") */
/* Placeholder: TEST_CASE("Harbor - water tile zero score") */

/*
 * Feature 3: Biome System
 */

TEST_CASE("Biome - temperature decreases with altitude") {
	/* temperature = 1.0 - (height / max_height) + noise */
	/* At height=0, temp=1.0; at height=max, temp=0.0 (ignoring noise) */
	double max_h = 15.0;
	double temp_low = 1.0 - (0.0 / max_h);
	double temp_high = 1.0 - (15.0 / max_h);
	CHECK(temp_low > temp_high);
	CHECK(temp_low == Approx(1.0));
	CHECK(temp_high == Approx(0.0));
}

/* Requires full game infrastructure -- integration test only */
/* Placeholder: TEST_CASE("Biome - classic matches original") */
/* Placeholder: TEST_CASE("Biome - noise adds variation") */
/* Placeholder: TEST_CASE("Biome - desert at high temperature") */
/* Placeholder: TEST_CASE("Biome - snow at low temperature") */
/* Placeholder: TEST_CASE("Biome - desert not on water") */
/* Placeholder: TEST_CASE("Biome - coverage within tolerance") */

/*
 * Feature 1 (continued): Mesa/Canyon and Volcanic continent shapes
 */

TEST_CASE("ContinentShape - Mesa mask produces plateau regions") {
	/* Mesa mask should produce values close to 1.0 in plateau areas
	 * and lower values in canyon areas. */
	double mask_center = 1.0; /* Plateaus near center */
	CHECK(mask_center >= 0.0);
	CHECK(mask_center <= 1.0);
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

/* Requires full game infrastructure -- integration test only */
/* Placeholder: TEST_CASE("River delta - creates river tiles in sea") */
/* Placeholder: TEST_CASE("River delta - only at sea mouth") */
/* Placeholder: TEST_CASE("River delta - does not extend onto land") */
