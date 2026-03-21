/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file test_temporal_upscale.cpp Tests for temporal upscaling support. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../video/temporal_upscale.h"
#include "../zoom_type.h"
#include <cmath>
#include <set>

#include "../safeguards.h"

/* --- Halton sequence tests --- */

TEST_CASE("TemporalUpscale - Halton base 2 first values")
{
	CHECK(JitterSequence::Halton(0, 2) == Approx(0.5f));
	CHECK(JitterSequence::Halton(1, 2) == Approx(0.25f));
	CHECK(JitterSequence::Halton(2, 2) == Approx(0.75f));
	CHECK(JitterSequence::Halton(3, 2) == Approx(0.125f));
}

TEST_CASE("TemporalUpscale - Halton base 3 first values")
{
	CHECK(JitterSequence::Halton(0, 3) == Approx(1.0f / 3.0f));
	CHECK(JitterSequence::Halton(1, 3) == Approx(2.0f / 3.0f));
	CHECK(JitterSequence::Halton(2, 3) == Approx(1.0f / 9.0f));
}

TEST_CASE("TemporalUpscale - Halton values are in [0, 1) range")
{
	for (uint32_t i = 0; i < 100; i++) {
		float h2 = JitterSequence::Halton(i, 2);
		float h3 = JitterSequence::Halton(i, 3);
		CHECK(h2 >= 0.0f);
		CHECK(h2 < 1.0f);
		CHECK(h3 >= 0.0f);
		CHECK(h3 < 1.0f);
	}
}

TEST_CASE("TemporalUpscale - Halton sequence is quasi-random (unique values)")
{
	std::set<int> seen;
	for (uint32_t i = 0; i < 64; i++) {
		int quantized = static_cast<int>(JitterSequence::Halton(i, 2) * 1000);
		seen.insert(quantized);
	}
	/* All 64 values should be unique when quantized to 3 decimal places. */
	CHECK(seen.size() == 64);
}

/* --- JitterSequence tests --- */

TEST_CASE("TemporalUpscale - NextFrame produces jitter in [-0.5, 0.5]")
{
	JitterSequence seq;
	for (int i = 0; i < 100; i++) {
		float jx, jy;
		seq.NextFrame(jx, jy);
		CHECK(jx >= -0.5f);
		CHECK(jx <= 0.5f);
		CHECK(jy >= -0.5f);
		CHECK(jy <= 0.5f);
	}
}

TEST_CASE("TemporalUpscale - NextFrame cycles after 64 frames")
{
	JitterSequence seq;
	float first_x, first_y;
	seq.NextFrame(first_x, first_y);
	/* Advance 63 more frames. */
	for (int i = 0; i < 63; i++) {
		float jx, jy;
		seq.NextFrame(jx, jy);
	}
	/* Frame 64 should be same as frame 0. */
	float cycle_x, cycle_y;
	seq.NextFrame(cycle_x, cycle_y);
	CHECK(cycle_x == Approx(first_x));
	CHECK(cycle_y == Approx(first_y));
}

TEST_CASE("TemporalUpscale - Reset resets frame index")
{
	JitterSequence seq;
	float first_x, first_y;
	seq.NextFrame(first_x, first_y);
	/* Advance some frames. */
	for (int i = 0; i < 10; i++) {
		float jx, jy;
		seq.NextFrame(jx, jy);
	}
	/* Reset and verify first frame matches. */
	seq.Reset();
	float reset_x, reset_y;
	seq.NextFrame(reset_x, reset_y);
	CHECK(reset_x == Approx(first_x));
	CHECK(reset_y == Approx(first_y));
}

/* --- ShouldApplyJitter tests --- */

TEST_CASE("TemporalUpscale - No jitter at 100% render scale")
{
	CHECK_FALSE(ShouldApplyJitter(100, to_underlying(ZoomLevel::Out2x)));
	CHECK_FALSE(ShouldApplyJitter(100, to_underlying(ZoomLevel::Out8x)));
}

TEST_CASE("TemporalUpscale - No jitter at Normal or zoomed-in levels")
{
	CHECK_FALSE(ShouldApplyJitter(75, to_underlying(ZoomLevel::Normal)));
	CHECK_FALSE(ShouldApplyJitter(75, to_underlying(ZoomLevel::In2x)));
	CHECK_FALSE(ShouldApplyJitter(75, to_underlying(ZoomLevel::In4x)));
}

TEST_CASE("TemporalUpscale - Jitter enabled at sub-100% scale and zoomed out")
{
	CHECK(ShouldApplyJitter(75, to_underlying(ZoomLevel::Out2x)));
	CHECK(ShouldApplyJitter(50, to_underlying(ZoomLevel::Out4x)));
	CHECK(ShouldApplyJitter(67, to_underlying(ZoomLevel::Out8x)));
}

TEST_CASE("TemporalUpscale - Jitter boundary at Out2x zoom")
{
	/* Out2x is the first zoom level where jitter is allowed. */
	CHECK(ShouldApplyJitter(75, to_underlying(ZoomLevel::Out2x)));
	CHECK_FALSE(ShouldApplyJitter(75, to_underlying(ZoomLevel::Normal)));
}

/* --- TemporalUpscaleParams defaults --- */

TEST_CASE("TemporalUpscale - Params default to sane values")
{
	TemporalUpscaleParams params;
	CHECK(params.jitter_x == 0.0f);
	CHECK(params.jitter_y == 0.0f);
	CHECK(params.delta_time == Approx(0.016f));
	CHECK(params.reset == false);
	CHECK(params.color_texture == 0);
}
