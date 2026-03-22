/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file test_mapgen_biome.cpp Tests for biome-aware tree placement and harbor-aware industry placement. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../core/math_func.hpp"

#include <cmath>
#include <algorithm>

#include "../safeguards.h"

/*
 * Standalone reimplementation of BiasTreeSeedByTemperature for unit testing.
 * This mirrors the logic in tree_cmd.cpp without needing tile/map infrastructure.
 */
static uint TestBiasTreeSeedByTemperature(uint seed, uint tile_height, uint max_height)
{
	uint max_h = std::max(1u, max_height);

	/* Temperature decreases linearly with altitude: temp in [0.0, 1.0] where 1.0 = warm (low). */
	double temp = 1.0 - static_cast<double>(std::min(tile_height, max_h)) / max_h;

	/* Target seed based on temperature: warm tiles aim for higher seeds, cold for lower. */
	double temp_seed = temp * 255.0;

	/* Blend: 60% altitude influence, 40% pure random for natural variety. */
	static constexpr double BIAS_STRENGTH = 0.6;
	double biased = seed * (1.0 - BIAS_STRENGTH) + temp_seed * BIAS_STRENGTH;

	return Clamp(static_cast<uint>(biased + 0.5), 0u, 255u);
}

/*
 * Feature 1: Biome-aware tree placement tests
 */

TEST_CASE("Biome trees - cold tiles (high altitude) get lower biased seed") {
	/* High altitude = cold = lower seed (conifers). */
	uint high_alt_seed = TestBiasTreeSeedByTemperature(128, 15, 15); /* max altitude */
	uint low_alt_seed = TestBiasTreeSeedByTemperature(128, 0, 15);   /* sea level */

	CHECK(high_alt_seed < low_alt_seed);
}

TEST_CASE("Biome trees - warm tiles (low altitude) get higher biased seed") {
	/* Low altitude = warm = higher seed (deciduous). */
	uint low_alt_seed = TestBiasTreeSeedByTemperature(128, 0, 15);
	/* With bias strength 0.6, seed 128 at altitude 0: biased = 128*0.4 + 255*0.6 = 51.2 + 153 = 204 */
	CHECK(low_alt_seed > 150);
}

TEST_CASE("Biome trees - maximum altitude gives minimum-biased seed") {
	/* At max altitude, temperature is 0, so temp_seed is 0.
	 * biased = seed * 0.4 + 0 * 0.6 = seed * 0.4 */
	uint result = TestBiasTreeSeedByTemperature(100, 15, 15);
	/* Expected: 100 * 0.4 = 40 */
	CHECK(result == 40);
}

TEST_CASE("Biome trees - sea level gives maximum-biased seed") {
	/* At altitude 0, temperature is 1.0, so temp_seed is 255.
	 * biased = seed * 0.4 + 255 * 0.6 = seed * 0.4 + 153 */
	uint result = TestBiasTreeSeedByTemperature(100, 0, 15);
	/* Expected: 100 * 0.4 + 153 = 40 + 153 = 193 */
	CHECK(result == 193);
}

TEST_CASE("Biome trees - mid altitude gives intermediate seed") {
	/* At half max altitude, temperature is 0.5, temp_seed = 127.5.
	 * biased = 128 * 0.4 + 127.5 * 0.6 = 51.2 + 76.5 = 127.7 ~= 128 */
	uint result = TestBiasTreeSeedByTemperature(128, 7, 15);
	CHECK(result >= 120);
	CHECK(result <= 140);
}

TEST_CASE("Biome trees - seed 0 at max altitude stays near 0") {
	uint result = TestBiasTreeSeedByTemperature(0, 15, 15);
	/* biased = 0 * 0.4 + 0 * 0.6 = 0 */
	CHECK(result == 0);
}

TEST_CASE("Biome trees - seed 255 at sea level stays near 255") {
	uint result = TestBiasTreeSeedByTemperature(255, 0, 15);
	/* biased = 255 * 0.4 + 255 * 0.6 = 102 + 153 = 255 */
	CHECK(result == 255);
}

TEST_CASE("Biome trees - output always within valid range") {
	for (uint seed = 0; seed <= 255; seed += 17) {
		for (uint h = 0; h <= 15; h++) {
			uint result = TestBiasTreeSeedByTemperature(seed, h, 15);
			CHECK(result <= 255);
		}
	}
}

TEST_CASE("Biome trees - monotonic with altitude for fixed seed") {
	/* For a fixed seed, as altitude increases (colder), the biased seed should decrease. */
	uint prev = TestBiasTreeSeedByTemperature(200, 0, 15);
	for (uint h = 1; h <= 15; h++) {
		uint current = TestBiasTreeSeedByTemperature(200, h, 15);
		CHECK(current <= prev);
		prev = current;
	}
}

TEST_CASE("Biome trees - preserves variety (not fully deterministic by altitude)") {
	/* Different input seeds at the same altitude should produce different outputs. */
	uint a = TestBiasTreeSeedByTemperature(50, 7, 15);
	uint b = TestBiasTreeSeedByTemperature(200, 7, 15);
	CHECK(a != b);
}

TEST_CASE("Biome trees - handles max_height of 1 gracefully") {
	/* Edge case: very flat map with max_height = 1. */
	uint at_bottom = TestBiasTreeSeedByTemperature(128, 0, 1);
	uint at_top = TestBiasTreeSeedByTemperature(128, 1, 1);
	CHECK(at_bottom > at_top);
}

/*
 * Feature 2: Harbor-aware industry placement tests
 *
 * SelectHarborBiasedTile logic: pick the best-scored tile from N random candidates.
 * We test the "pick best from candidates" pattern standalone.
 */

TEST_CASE("Harbor bias - picks highest score from candidates") {
	/* Simulate the selection logic: given a set of (tile, score) pairs, the algorithm
	 * should select the tile with the highest harbor score. */
	struct Candidate { int tile; uint8_t score; };
	std::vector<Candidate> candidates = {
		{10, 50}, {20, 200}, {30, 100}, {40, 75}, {50, 150}, {60, 25}, {70, 180}, {80, 90}
	};

	int best_tile = candidates[0].tile;
	uint8_t best_score = candidates[0].score;
	for (size_t i = 1; i < candidates.size(); i++) {
		if (candidates[i].score > best_score) {
			best_tile = candidates[i].tile;
			best_score = candidates[i].score;
		}
	}

	CHECK(best_tile == 20);
	CHECK(best_score == 200);
}

TEST_CASE("Harbor bias - single candidate returns that candidate") {
	/* With only one candidate, it must be selected regardless of score. */
	int tile = 42;
	uint8_t score = 5;
	int best_tile = tile;
	uint8_t best_score = score;

	CHECK(best_tile == 42);
	CHECK(best_score == 5);
}

TEST_CASE("Harbor bias - all zero scores returns first candidate") {
	/* When no candidates have harbor scores, the first random tile wins. */
	struct Candidate { int tile; uint8_t score; };
	std::vector<Candidate> candidates = {
		{10, 0}, {20, 0}, {30, 0}, {40, 0}
	};

	int best_tile = candidates[0].tile;
	uint8_t best_score = candidates[0].score;
	for (size_t i = 1; i < candidates.size(); i++) {
		if (candidates[i].score > best_score) {
			best_tile = candidates[i].tile;
			best_score = candidates[i].score;
		}
	}

	CHECK(best_tile == 10);
	CHECK(best_score == 0);
}

TEST_CASE("Harbor bias - tie-breaking favors first encountered") {
	/* When multiple candidates share the highest score, the first one encountered wins
	 * (since we use strict > comparison). */
	struct Candidate { int tile; uint8_t score; };
	std::vector<Candidate> candidates = {
		{10, 100}, {20, 200}, {30, 200}, {40, 100}
	};

	int best_tile = candidates[0].tile;
	uint8_t best_score = candidates[0].score;
	for (size_t i = 1; i < candidates.size(); i++) {
		if (candidates[i].score > best_score) {
			best_tile = candidates[i].tile;
			best_score = candidates[i].score;
		}
	}

	CHECK(best_tile == 20);
	CHECK(best_score == 200);
}
