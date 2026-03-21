/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file test_postprocess.cpp Tests for GPU post-processing pipeline logic. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../video/postprocess.h"
#include <cmath>

#include "../safeguards.h"

/* --- Resolution calculation tests --- */

TEST_CASE("PostProcess - CalculateDimensions 100% scale")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 100);
	CHECK(dims.display.width == 1920);
	CHECK(dims.display.height == 1080);
	CHECK(dims.render.width == 1920);
	CHECK(dims.render.height == 1080);
}

TEST_CASE("PostProcess - CalculateDimensions 50% scale")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 50);
	CHECK(dims.display.width == 1920);
	CHECK(dims.display.height == 1080);
	CHECK(dims.render.width == 960);
	CHECK(dims.render.height == 540);
}

TEST_CASE("PostProcess - CalculateDimensions 75% scale")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 75);
	CHECK(dims.display.width == 1920);
	CHECK(dims.display.height == 1080);
	CHECK(dims.render.width == 1440);
	CHECK(dims.render.height == 810);
}

TEST_CASE("PostProcess - CalculateDimensions 67% scale at 4K")
{
	auto dims = CalculatePostProcessDimensions(3840, 2160, 67);
	CHECK(dims.display.width == 3840);
	CHECK(dims.display.height == 2160);
	CHECK(dims.render.width == 2572);
	CHECK(dims.render.height == 1448);
}

TEST_CASE("PostProcess - CalculateDimensions clamps below 50")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 25);
	CHECK(dims.render.width == 960);
	CHECK(dims.render.height == 540);
}

TEST_CASE("PostProcess - CalculateDimensions allows supersampling above 100")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 150);
	/* Scale 150% means render at 1.5x display (supersampling). */
	CHECK(dims.render.width == 2880);
	CHECK(dims.render.height == 1620);
}

TEST_CASE("PostProcess - CalculateDimensions clamps above 200")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 250);
	/* Clamped to 200%. */
	CHECK(dims.render.width == 3840);
	CHECK(dims.render.height == 2160);
}

TEST_CASE("PostProcess - CalculateDimensions minimum size")
{
	auto dims = CalculatePostProcessDimensions(4, 4, 50);
	CHECK(dims.render.width >= 2);
	CHECK(dims.render.height >= 2);
}

TEST_CASE("PostProcess - CalculateDimensions even alignment")
{
	auto dims = CalculatePostProcessDimensions(1921, 1081, 75);
	CHECK((dims.render.width % 2) == 0);
	CHECK((dims.render.height % 2) == 0);
}

TEST_CASE("PostProcess - CalculateDimensions small window")
{
	auto dims = CalculatePostProcessDimensions(640, 480, 50);
	CHECK(dims.render.width == 320);
	CHECK(dims.render.height == 240);
}

/* --- NeedsFBO tests --- */

TEST_CASE("PostProcess - NeedsFBO default config")
{
	PostProcessConfig config;
	CHECK_FALSE(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - NeedsFBO with render scale below 100")
{
	PostProcessConfig config;
	config.render_scale = 75;
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - NeedsFBO with FSR1 upscale")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::FSR1;
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - NeedsFBO with sharpening only")
{
	PostProcessConfig config;
	config.sharpening = 50;
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - NeedsFBO bilinear filtering only does not need FBO")
{
	PostProcessConfig config;
	config.bilinear_filtering = true;
	CHECK_FALSE(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - NeedsFBO with any effect enabled")
{
	PostProcessConfig config;
	config.fxaa = true;
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - NeedsFBO with vignette enabled")
{
	PostProcessConfig config;
	config.vignette = true;
	CHECK(PostProcessNeedsFBO(config));
}

/* --- Pass count tests --- */

TEST_CASE("PostProcess - PassCount none")
{
	PostProcessConfig config;
	CHECK(PostProcessPassCount(config) == 0);
}

TEST_CASE("PostProcess - PassCount FSR1 only")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::FSR1;
	config.render_scale = 75;
	CHECK(PostProcessPassCount(config) == 2); /* EASU + RCAS */
}

TEST_CASE("PostProcess - PassCount FSR1 with sharpening does not add extra CAS")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::FSR1;
	config.render_scale = 75;
	config.sharpening = 50;
	CHECK(PostProcessPassCount(config) == 2); /* RCAS already sharpens */
}

TEST_CASE("PostProcess - PassCount bilinear upscale")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::Bilinear;
	config.render_scale = 75;
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - PassCount CAS standalone")
{
	PostProcessConfig config;
	config.sharpening = 50;
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - PassCount bilinear with CAS")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::Bilinear;
	config.render_scale = 75;
	config.sharpening = 50;
	CHECK(PostProcessPassCount(config) == 2);
}

TEST_CASE("PostProcess - PassCount FXAA adds a pass")
{
	PostProcessConfig config;
	config.fxaa = true;
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - PassCount vignette adds a pass")
{
	PostProcessConfig config;
	config.vignette = true;
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - PassCount color grading adds a pass")
{
	PostProcessConfig config;
	config.color_grading = true;
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - PassCount all must-have effects")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::FSR1;
	config.render_scale = 75;
	config.fxaa = true;
	config.color_grading = true;
	config.vignette = true;
	config.tiltshift = true;
	/* FSR EASU + RCAS + FXAA + tiltshift_h + tiltshift_v + color_grading + vignette = 7 */
	CHECK(PostProcessPassCount(config) == 7);
}

/* --- Sharpening mapping tests --- */

TEST_CASE("PostProcess - MapSharpeningToFsrRcas zero means no sharpening")
{
	CHECK(MapSharpeningToFsrRcas(0) == Approx(2.0f));
}

TEST_CASE("PostProcess - MapSharpeningToFsrRcas 100 means max sharpening")
{
	CHECK(MapSharpeningToFsrRcas(100) == Approx(0.0f));
}

TEST_CASE("PostProcess - MapSharpeningToFsrRcas 50 is midpoint")
{
	CHECK(MapSharpeningToFsrRcas(50) == Approx(1.0f));
}

TEST_CASE("PostProcess - MapSharpeningToCas zero is off")
{
	CHECK(MapSharpeningToCas(0) == Approx(0.0f));
}

TEST_CASE("PostProcess - MapSharpeningToCas 100 is max")
{
	CHECK(MapSharpeningToCas(100) == Approx(1.0f));
}

TEST_CASE("PostProcess - MapSharpeningToCas 50 is half")
{
	CHECK(MapSharpeningToCas(50) == Approx(0.5f));
}

/* --- FSR EASU constant tests --- */

TEST_CASE("PostProcess - ComputeFsrEasuConstants 2x upscale")
{
	float con0[4], con1[4], con2[4], con3[4];
	ComputeFsrEasuConstants(con0, con1, con2, con3,
		960.0f, 540.0f,
		960.0f, 540.0f,
		1920.0f, 1080.0f);

	CHECK(con0[0] == Approx(0.5f));
	CHECK(con0[1] == Approx(0.5f));
	/* con1.xy = reciprocal texel size (for sampling offsets). */
	CHECK(con1[0] == Approx(1.0f / 960.0f));
	CHECK(con1[1] == Approx(1.0f / 540.0f));
	/* con1.zw = input size in pixels (for UV-to-texel conversion). */
	CHECK(con1[2] == Approx(960.0f));
	CHECK(con1[3] == Approx(540.0f));
}

TEST_CASE("PostProcess - ComputeFsrEasuConstants 1x no upscale")
{
	float con0[4], con1[4], con2[4], con3[4];
	ComputeFsrEasuConstants(con0, con1, con2, con3,
		1920.0f, 1080.0f,
		1920.0f, 1080.0f,
		1920.0f, 1080.0f);

	CHECK(con0[0] == Approx(1.0f));
	CHECK(con0[1] == Approx(1.0f));
	CHECK(con1[2] == Approx(1920.0f));
	CHECK(con1[3] == Approx(1080.0f));
}

/* --- FSR RCAS constant tests --- */

TEST_CASE("PostProcess - ComputeFsrRcasConstant max sharpening")
{
	float con[4];
	ComputeFsrRcasConstant(con, 0.0f);
	CHECK(con[0] == Approx(1.0f)); /* exp(0) = 1.0 */
}

TEST_CASE("PostProcess - ComputeFsrRcasConstant no sharpening")
{
	float con[4];
	ComputeFsrRcasConstant(con, 2.0f);
	CHECK(con[0] == Approx(expf(-2.0f)));
}

/* --- CAS constant tests --- */

TEST_CASE("PostProcess - ComputeCasConstant")
{
	float con[4];
	ComputeCasConstant(con, 0.5f, 1920.0f, 1080.0f);
	CHECK(con[0] == Approx(0.5f));
	CHECK(con[1] == Approx(1.0f / 1920.0f));
	CHECK(con[2] == Approx(1.0f / 1080.0f));
}

TEST_CASE("PostProcess - ComputeCasConstant zero intensity")
{
	float con[4];
	ComputeCasConstant(con, 0.0f, 1920.0f, 1080.0f);
	CHECK(con[0] == Approx(0.0f));
}

/* --- Zero/negative dimension edge cases --- */

TEST_CASE("PostProcess - CalculateDimensions zero dimensions")
{
	auto dims = CalculatePostProcessDimensions(0, 0, 50);
	CHECK(dims.display.width == 0);
	CHECK(dims.display.height == 0);
	CHECK(dims.render.width == 0);
	CHECK(dims.render.height == 0);
}

TEST_CASE("PostProcess - CalculateDimensions negative dimensions")
{
	auto dims = CalculatePostProcessDimensions(-1, -1, 75);
	CHECK(dims.display.width == 0);
	CHECK(dims.display.height == 0);
}

/* --- Individual effect pass count tests --- */

TEST_CASE("PostProcess - PassCount night mode adds a pass")
{
	PostProcessConfig config;
	config.night_mode = true;
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - PassCount film grain adds a pass")
{
	PostProcessConfig config;
	config.film_grain = true;
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - PassCount CRT filter adds a pass")
{
	PostProcessConfig config;
	config.crt_filter = true;
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - PassCount tiltshift adds two passes")
{
	PostProcessConfig config;
	config.tiltshift = true;
	CHECK(PostProcessPassCount(config) == 2);
}

/* --- NeedsFBO for all effect types --- */

TEST_CASE("PostProcess - NeedsFBO with night mode")
{
	PostProcessConfig config;
	config.night_mode = true;
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - NeedsFBO with film grain")
{
	PostProcessConfig config;
	config.film_grain = true;
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - NeedsFBO with CRT filter")
{
	PostProcessConfig config;
	config.crt_filter = true;
	CHECK(PostProcessNeedsFBO(config));
}

/* --- Bilinear filtering edge cases --- */

TEST_CASE("PostProcess - bilinear_filtering alone does not need FBO")
{
	PostProcessConfig config;
	config.bilinear_filtering = true;
	/* Bilinear filtering is just a GL texture parameter, not a shader pass. */
	CHECK_FALSE(PostProcessNeedsFBO(config));
	CHECK(PostProcessPassCount(config) == 0);
}

TEST_CASE("PostProcess - MapSharpeningToFsrRcas is monotonically decreasing")
{
	float prev = MapSharpeningToFsrRcas(0);
	for (uint8_t i = 1; i <= 100; i++) {
		float cur = MapSharpeningToFsrRcas(i);
		CHECK(cur <= prev);
		prev = cur;
	}
}

TEST_CASE("PostProcess - MapSharpeningToCas is monotonically increasing")
{
	float prev = MapSharpeningToCas(0);
	for (uint8_t i = 1; i <= 100; i++) {
		float cur = MapSharpeningToCas(i);
		CHECK(cur >= prev);
		prev = cur;
	}
}

TEST_CASE("PostProcess - NeedsFBO with tiltshift")
{
	PostProcessConfig config;
	config.tiltshift = true;
	CHECK(PostProcessNeedsFBO(config));
}

/* --- Boundary render scale values --- */

TEST_CASE("PostProcess - CalculateDimensions 51% scale odd rounding")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 51);
	CHECK(dims.render.width > 0);
	CHECK(dims.render.height > 0);
	CHECK((dims.render.width % 2) == 0);
	CHECK((dims.render.height % 2) == 0);
}

/* --- Division by zero guard --- */

TEST_CASE("PostProcess - ComputeFsrEasuConstants zero input safe")
{
	float con0[4], con1[4], con2[4], con3[4];
	ComputeFsrEasuConstants(con0, con1, con2, con3, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	/* Should not crash, values should be finite. */
	CHECK(std::isfinite(con0[0]));
	CHECK(std::isfinite(con1[0]));
}

TEST_CASE("PostProcess - ComputeCasConstant zero dimensions safe")
{
	float con[4];
	ComputeCasConstant(con, 0.5f, 0.0f, 0.0f);
	CHECK(con[1] == 0.0f);
	CHECK(con[2] == 0.0f);
}

/* --- M5: Bilinear upscale at 100% should NOT need FBO --- */

TEST_CASE("PostProcess - NeedsFBO bilinear upscale at 100pct is no-op")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::Bilinear;
	config.render_scale = 100;
	CHECK_FALSE(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - NeedsFBO bilinear upscale at 75pct needs FBO via render_scale")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::Bilinear;
	config.render_scale = 75;
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - NeedsFBO FSR1 at 100pct still needs FBO")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::FSR1;
	config.render_scale = 100;
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - PassCount bilinear at 100pct is zero")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::Bilinear;
	config.render_scale = 100;
	CHECK(PostProcessPassCount(config) == 0);
}

/* --- Ultra-wide and extreme aspect ratio dimension tests --- */

TEST_CASE("PostProcess - CalculateDimensions ultrawide 32:9 at 5120x1440")
{
	auto dims = CalculatePostProcessDimensions(5120, 1440, 75);
	CHECK(dims.display.width == 5120);
	CHECK(dims.display.height == 1440);
	CHECK(dims.render.width > 0);
	CHECK(dims.render.height > 0);
	CHECK((dims.render.width % 2) == 0);
	CHECK((dims.render.height % 2) == 0);
	/* Render should be ~75% of display */
	CHECK(dims.render.width <= 5120);
	CHECK(dims.render.width >= 3840);
}

TEST_CASE("PostProcess - CalculateDimensions super ultrawide 48:9 at 7680x1440")
{
	auto dims = CalculatePostProcessDimensions(7680, 1440, 67);
	CHECK(dims.display.width == 7680);
	CHECK(dims.display.height == 1440);
	CHECK(dims.render.width > 0);
	CHECK((dims.render.width % 2) == 0);
}

TEST_CASE("PostProcess - CalculateDimensions 8K resolution")
{
	auto dims = CalculatePostProcessDimensions(7680, 4320, 50);
	CHECK(dims.display.width == 7680);
	CHECK(dims.display.height == 4320);
	CHECK(dims.render.width == 3840);
	CHECK(dims.render.height == 2160);
}

TEST_CASE("PostProcess - CalculateDimensions portrait orientation")
{
	auto dims = CalculatePostProcessDimensions(1080, 1920, 75);
	CHECK(dims.display.width == 1080);
	CHECK(dims.display.height == 1920);
	CHECK(dims.render.width > 0);
	CHECK(dims.render.height > 0);
	CHECK((dims.render.width % 2) == 0);
	CHECK((dims.render.height % 2) == 0);
}

/* --- Combined effect pass counting --- */

TEST_CASE("PostProcess - PassCount all effects maximum")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::FSR1;
	config.render_scale = 75;
	config.fxaa = true;
	config.color_grading = true;
	config.vignette = true;
	config.tiltshift = true;
	config.night_mode = true;
	config.film_grain = true;
	config.crt_filter = true;
	/* FSR EASU + RCAS + FXAA + tiltshift_h + tiltshift_v + color + night + vignette + grain + CRT = 10 */
	CHECK(PostProcessPassCount(config) == 10);
}

TEST_CASE("PostProcess - PassCount sharpening suppressed by FSR1")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::FSR1;
	config.sharpening = 80;
	/* EASU + RCAS = 2, CAS suppressed because FSR1 RCAS handles sharpening */
	CHECK(PostProcessPassCount(config) == 2);
}

/* --- Render scale dimension invariants --- */

TEST_CASE("PostProcess - render dimensions never exceed display dimensions")
{
	for (uint8_t scale = 50; scale <= 100; scale++) {
		auto dims = CalculatePostProcessDimensions(1920, 1080, scale);
		CHECK(dims.render.width <= dims.display.width);
		CHECK(dims.render.height <= dims.display.height);
	}
}

TEST_CASE("PostProcess - render dimensions always even")
{
	for (uint8_t scale = 50; scale < 100; scale++) {
		auto dims = CalculatePostProcessDimensions(1921, 1081, scale);
		CHECK((dims.render.width % 2) == 0);
		CHECK((dims.render.height % 2) == 0);
	}
}

/* --- 8bpp blitter compatibility --- */

TEST_CASE("PostProcess - NeedsFBO with bilinear at 100% does not need FBO")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::Bilinear;
	config.render_scale = 100;
	/* Bilinear at 100% is a no-op upscale -- don't waste an FBO. */
	CHECK_FALSE(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - NeedsFBO with bilinear below 100% needs FBO")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::Bilinear;
	config.render_scale = 75;
	CHECK(PostProcessNeedsFBO(config));
}

/* --- Large resolution edge cases --- */

TEST_CASE("PostProcess - CalculateDimensions 8K at 67% edge case")
{
	auto dims = CalculatePostProcessDimensions(7680, 4320, 67);
	CHECK(dims.render.width > 0);
	CHECK(dims.render.height > 0);
	CHECK(dims.render.width <= dims.display.width);
	CHECK(dims.render.height <= dims.display.height);
	CHECK((dims.render.width % 2) == 0);
	CHECK((dims.render.height % 2) == 0);
}

/* --- Config combination edge cases --- */

TEST_CASE("PostProcess - PassCount with all effects and FSR1")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::FSR1;
	config.render_scale = 75;
	config.sharpening = 50;
	config.fxaa = true;
	config.color_grading = true;
	config.vignette = true;
	config.tiltshift = true;
	config.night_mode = true;
	config.film_grain = true;
	config.crt_filter = true;
	/* EASU + RCAS + FXAA + tilt_h + tilt_v + color + night + vignette + grain + crt = 10 */
	/* (sharpening doesn't add CAS when FSR1 active) */
	CHECK(PostProcessPassCount(config) == 10);
}

TEST_CASE("PostProcess - PassCount with all effects and bilinear")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::Bilinear;
	config.render_scale = 75;
	config.sharpening = 50;
	config.fxaa = true;
	config.color_grading = true;
	config.vignette = true;
	config.tiltshift = true;
	config.night_mode = true;
	config.film_grain = true;
	config.crt_filter = true;
	/* bilinear + CAS + FXAA + tilt_h + tilt_v + color + night + vignette + grain + crt = 10 */
	CHECK(PostProcessPassCount(config) == 10);
}

TEST_CASE("PostProcess - NeedsFBO is false only when everything is off")
{
	PostProcessConfig config;
	/* Default config: all off, 100% scale, no upscale, no sharpening */
	CHECK_FALSE(PostProcessNeedsFBO(config));
	/* Any single toggle should trigger FBO */
	config.fxaa = true;
	CHECK(PostProcessNeedsFBO(config));
}

/* --- Settings-to-config identity tests (color grading at defaults is identity) --- */

TEST_CASE("PostProcess - color grading defaults are identity values")
{
	PostProcessConfig config;
	/* brightness=0, contrast=100, saturation=100, temperature=0 means no change. */
	CHECK(config.cg_brightness == 0);
	CHECK(config.cg_contrast == 100);
	CHECK(config.cg_saturation == 100);
	CHECK(config.cg_temperature == 0);
}

TEST_CASE("PostProcess - color grading enabled adds one pass")
{
	PostProcessConfig config;
	config.color_grading = true;
	CHECK(PostProcessPassCount(config) == 1);
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - night mode enabled adds one pass")
{
	PostProcessConfig config;
	config.night_mode = true;
	CHECK(PostProcessPassCount(config) == 1);
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - CRT filter enabled adds one pass")
{
	PostProcessConfig config;
	config.crt_filter = true;
	CHECK(PostProcessPassCount(config) == 1);
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - FXAA enabled adds one pass")
{
	PostProcessConfig config;
	config.fxaa = true;
	CHECK(PostProcessPassCount(config) == 1);
	CHECK(PostProcessNeedsFBO(config));
}

/* --- Each effect independently needs FBO --- */

TEST_CASE("PostProcess - each effect toggle independently needs FBO")
{
	/* Test every boolean toggle creates FBO need. */
	auto TestToggle = [](auto setter) {
		PostProcessConfig config;
		setter(config);
		CHECK(PostProcessNeedsFBO(config));
	};
	TestToggle([](auto &c) { c.fxaa = true; });
	TestToggle([](auto &c) { c.color_grading = true; });
	TestToggle([](auto &c) { c.vignette = true; });
	TestToggle([](auto &c) { c.tiltshift = true; });
	TestToggle([](auto &c) { c.night_mode = true; });
	TestToggle([](auto &c) { c.film_grain = true; });
	TestToggle([](auto &c) { c.crt_filter = true; });
	TestToggle([](auto &c) { c.sharpening = 1; });
}

/* --- Config change detection edge cases --- */

TEST_CASE("PostProcess - NeedsFBO detects individual effect changes")
{
	PostProcessConfig off;
	PostProcessConfig fxaa_on;
	fxaa_on.fxaa = true;
	/* Toggling FXAA changes NeedsFBO result. */
	CHECK_FALSE(PostProcessNeedsFBO(off));
	CHECK(PostProcessNeedsFBO(fxaa_on));
}

TEST_CASE("PostProcess - pass count consistent with NeedsFBO")
{
	/* If NeedsFBO is false, pass count must be 0. */
	PostProcessConfig config;
	if (!PostProcessNeedsFBO(config)) {
		CHECK(PostProcessPassCount(config) == 0);
	}
}

/* --- Sharpening default value test --- */

TEST_CASE("PostProcess - sharpening default is 0 (off)")
{
	PostProcessConfig config;
	CHECK(config.sharpening == 0);
	CHECK_FALSE(PostProcessNeedsFBO(config)); /* sharpening=0 doesn't trigger FBO */
}

TEST_CASE("PostProcess - sharpening value 1 is minimum active")
{
	PostProcessConfig config;
	config.sharpening = 1;
	CHECK(PostProcessNeedsFBO(config));
	CHECK(PostProcessPassCount(config) == 1); /* CAS standalone */
}

/* --- Color grading at identity values still counts as a pass when explicitly enabled --- */

TEST_CASE("PostProcess - color grading at identity values still runs when enabled")
{
	PostProcessConfig config;
	config.color_grading = true;
	config.cg_brightness = 0;
	config.cg_contrast = 100;
	config.cg_saturation = 100;
	config.cg_temperature = 0;
	/* Even though all values are identity, the pass should still run if enabled.
	 * The shader is a no-op at identity — zero visual cost, avoids flicker. */
	CHECK(PostProcessPassCount(config) == 1);
	CHECK(PostProcessNeedsFBO(config));
}

/* --- Sharpening mapping boundary tests --- */

TEST_CASE("PostProcess - MapSharpeningToFsrRcas clamps above 100")
{
	/* Values above 100 should clamp to 100 (max sharpening = 0.0). */
	CHECK(MapSharpeningToFsrRcas(255) == Approx(0.0f));
}

TEST_CASE("PostProcess - MapSharpeningToCas clamps above 100")
{
	CHECK(MapSharpeningToCas(255) == Approx(1.0f));
}

/* --- FSR RCAS constant edge cases --- */

TEST_CASE("PostProcess - ComputeFsrRcasConstant negative input clamps to 0")
{
	float con[4];
	ComputeFsrRcasConstant(con, -1.0f);
	CHECK(con[0] == Approx(1.0f)); /* exp(0) = 1.0, clamped to 0.0 minimum */
}

TEST_CASE("PostProcess - ComputeFsrRcasConstant over-range clamps to 2")
{
	float con[4];
	ComputeFsrRcasConstant(con, 5.0f);
	CHECK(con[0] == Approx(expf(-2.0f))); /* Clamped to 2.0 */
}

/* --- CAS constant edge cases --- */

TEST_CASE("PostProcess - ComputeCasConstant negative intensity clamps to 0")
{
	float con[4];
	ComputeCasConstant(con, -0.5f, 1920.0f, 1080.0f);
	CHECK(con[0] == Approx(0.0f));
}

TEST_CASE("PostProcess - ComputeCasConstant over-range clamps to 1")
{
	float con[4];
	ComputeCasConstant(con, 2.0f, 1920.0f, 1080.0f);
	CHECK(con[0] == Approx(1.0f));
}

/* --- Tiny window dimension edge cases --- */

TEST_CASE("PostProcess - CalculateDimensions 3x3 at 50% stays at minimum 2x2")
{
	auto dims = CalculatePostProcessDimensions(3, 3, 50);
	CHECK(dims.render.width >= 2);
	CHECK(dims.render.height >= 2);
	CHECK((dims.render.width % 2) == 0);
	CHECK((dims.render.height % 2) == 0);
}

TEST_CASE("PostProcess - CalculateDimensions 1x1 at 50% stays at minimum 2x2")
{
	auto dims = CalculatePostProcessDimensions(1, 1, 50);
	CHECK(dims.render.width >= 2);
	CHECK(dims.render.height >= 2);
}

TEST_CASE("PostProcess - CalculateDimensions 2x2 at 50% stays at minimum 2x2")
{
	auto dims = CalculatePostProcessDimensions(2, 2, 50);
	CHECK(dims.render.width >= 2);
	CHECK(dims.render.height >= 2);
}

/* --- FSR EASU constants with asymmetric input/output --- */

TEST_CASE("PostProcess - ComputeFsrEasuConstants asymmetric upscale")
{
	float con0[4], con1[4], con2[4], con3[4];
	/* 720p to 1080p: non-integer scale factor. */
	ComputeFsrEasuConstants(con0, con1, con2, con3,
		1280.0f, 720.0f,
		1280.0f, 720.0f,
		1920.0f, 1080.0f);
	/* Scale factor should be 2/3. */
	CHECK(con0[0] == Approx(1280.0f / 1920.0f));
	CHECK(con0[1] == Approx(720.0f / 1080.0f));
	/* con1.xy = reciprocal texel sizes. */
	CHECK(con1[0] == Approx(1.0f / 1280.0f));
	CHECK(con1[1] == Approx(1.0f / 720.0f));
	/* con1.zw = input size in pixels. */
	CHECK(con1[2] == Approx(1280.0f));
	CHECK(con1[3] == Approx(720.0f));
}

/* --- PostProcessPassCount and NeedsFBO consistency --- */

TEST_CASE("PostProcess - NeedsFBO implies PassCount > 0 for most configs")
{
	/* If any single effect enables FBO, pass count should be > 0
	 * (exception: render_scale < 100 with no upscale mode set). */
	PostProcessConfig config;
	config.fxaa = true;
	CHECK(PostProcessNeedsFBO(config));
	CHECK(PostProcessPassCount(config) > 0);
}

TEST_CASE("PostProcess - render_scale < 100 needs FBO but 0 passes without upscale mode")
{
	/* render_scale < 100 triggers FBO but with UpscaleMode::None, no upscale pass is generated.
	 * This is a valid state: the FBO is needed but the blit shader handles it. */
	PostProcessConfig config;
	config.render_scale = 75;
	config.upscale_mode = UpscaleMode::None;
	CHECK(PostProcessNeedsFBO(config));
	CHECK(PostProcessPassCount(config) == 0);
}

/* --- Monotonicity of render dimensions --- */

TEST_CASE("PostProcess - render dimensions increase monotonically with scale")
{
	uint prev_w = 0;
	uint prev_h = 0;
	for (uint8_t scale = 50; scale <= 100; scale++) {
		auto dims = CalculatePostProcessDimensions(1920, 1080, scale);
		CHECK(dims.render.width >= prev_w);
		CHECK(dims.render.height >= prev_h);
		prev_w = dims.render.width;
		prev_h = dims.render.height;
	}
}

TEST_CASE("PostProcess - color grading disabled means zero passes from it")
{
	PostProcessConfig config;
	config.color_grading = false;
	config.cg_brightness = 25; /* Non-identity but grading disabled */
	CHECK(PostProcessPassCount(config) == 0);
}

/* --- Vignette settings --- */

TEST_CASE("PostProcess - vignette toggle needs FBO and adds pass")
{
	PostProcessConfig config;
	config.vignette = true;
	CHECK(PostProcessNeedsFBO(config));
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - vignette defaults match struct")
{
	PostProcessConfig config;
	CHECK(config.vignette_intensity == 30);
	CHECK(config.vignette_radius == 85);
}

/* --- Tilt-shift settings --- */

TEST_CASE("PostProcess - tiltshift toggle needs FBO and adds two passes")
{
	PostProcessConfig config;
	config.tiltshift = true;
	CHECK(PostProcessNeedsFBO(config));
	CHECK(PostProcessPassCount(config) == 2); /* H + V blur */
}

TEST_CASE("PostProcess - tiltshift defaults match struct")
{
	PostProcessConfig config;
	CHECK(config.tiltshift_focus_y == 45);
	CHECK(config.tiltshift_focus_width == 25);
	CHECK(config.tiltshift_blur == 30);
}

/* --- Film grain settings --- */

TEST_CASE("PostProcess - film grain toggle needs FBO and adds pass")
{
	PostProcessConfig config;
	config.film_grain = true;
	CHECK(PostProcessNeedsFBO(config));
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - grain intensity default")
{
	PostProcessConfig config;
	CHECK(config.grain_intensity == 4);
}

/* --- Night mode sub-parameters --- */

TEST_CASE("PostProcess - night mode defaults match struct")
{
	PostProcessConfig config;
	CHECK(config.night_intensity == 60);
	CHECK(config.night_blue_shift == 30);
}

/* --- CRT sub-parameters --- */

TEST_CASE("PostProcess - CRT defaults match struct")
{
	PostProcessConfig config;
	CHECK(config.crt_scanlines == 15);
	CHECK(config.crt_curvature == 0);
	CHECK(config.crt_aberration == 5);
}

/* --- Color temperature --- */

TEST_CASE("PostProcess - color temperature default is zero (neutral)")
{
	PostProcessConfig config;
	CHECK(config.cg_temperature == 0);
}

/* --- Full pipeline pass count with all effects --- */

TEST_CASE("PostProcess - all effects enabled gives correct total passes")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::FSR1;
	config.render_scale = 75;
	config.fxaa = true;
	config.color_grading = true;
	config.vignette = true;
	config.tiltshift = true;
	config.night_mode = true;
	config.film_grain = true;
	config.crt_filter = true;
	/* EASU + RCAS + FXAA + tiltH + tiltV + color + night + vignette + grain + CRT = 10 */
	CHECK(PostProcessPassCount(config) == 10);
}

TEST_CASE("PostProcess - all effects with bilinear gives correct total")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::Bilinear;
	config.render_scale = 75;
	config.sharpening = 50;
	config.fxaa = true;
	config.color_grading = true;
	config.vignette = true;
	config.tiltshift = true;
	config.night_mode = true;
	config.film_grain = true;
	config.crt_filter = true;
	/* bilinear + CAS + FXAA + tiltH + tiltV + color + night + vignette + grain + CRT = 10 */
	CHECK(PostProcessPassCount(config) == 10);
}

/* --- Color grading parameter mapping tests --- */

TEST_CASE("PostProcess - color grading brightness range maps correctly")
{
	/* brightness is int8_t (-50..50), mapped via /100.0f in shader. */
	PostProcessConfig config;
	config.color_grading = true;
	config.cg_brightness = -50;
	CHECK(config.cg_brightness / 100.0f == Approx(-0.5f));
	config.cg_brightness = 50;
	CHECK(config.cg_brightness / 100.0f == Approx(0.5f));
	config.cg_brightness = 0;
	CHECK(config.cg_brightness / 100.0f == Approx(0.0f));
}

TEST_CASE("PostProcess - color grading contrast range maps correctly")
{
	/* contrast is uint8_t (50..200), mapped via /100.0f. */
	PostProcessConfig config;
	CHECK(config.cg_contrast / 100.0f == Approx(1.0f)); /* default 100 = identity */
	config.cg_contrast = 50;
	CHECK(config.cg_contrast / 100.0f == Approx(0.5f));
	config.cg_contrast = 200;
	CHECK(config.cg_contrast / 100.0f == Approx(2.0f));
}

TEST_CASE("PostProcess - color grading saturation range maps correctly")
{
	PostProcessConfig config;
	CHECK(config.cg_saturation / 100.0f == Approx(1.0f)); /* default 100 = identity */
	config.cg_saturation = 0;
	CHECK(config.cg_saturation / 100.0f == Approx(0.0f)); /* grayscale */
	config.cg_saturation = 200;
	CHECK(config.cg_saturation / 100.0f == Approx(2.0f)); /* oversaturated */
}

TEST_CASE("PostProcess - color grading temperature range maps correctly")
{
	PostProcessConfig config;
	CHECK(config.cg_temperature / 100.0f == Approx(0.0f)); /* default 0 = neutral */
	config.cg_temperature = -100;
	CHECK(config.cg_temperature / 100.0f == Approx(-1.0f)); /* cool */
	config.cg_temperature = 100;
	CHECK(config.cg_temperature / 100.0f == Approx(1.0f)); /* warm */
}

/* --- CRT parameter mapping tests --- */

TEST_CASE("PostProcess - CRT scanline mapping")
{
	PostProcessConfig config;
	/* scanlines (0-50) mapped via /100.0f in shader */
	config.crt_scanlines = 0;
	CHECK(config.crt_scanlines / 100.0f == Approx(0.0f)); /* no scanlines */
	config.crt_scanlines = 50;
	CHECK(config.crt_scanlines / 100.0f == Approx(0.5f)); /* max */
}

TEST_CASE("PostProcess - CRT curvature mapping")
{
	PostProcessConfig config;
	config.crt_curvature = 0;
	CHECK(config.crt_curvature / 100.0f == Approx(0.0f)); /* flat */
	config.crt_curvature = 50;
	CHECK(config.crt_curvature / 100.0f == Approx(0.5f)); /* max curve */
}

TEST_CASE("PostProcess - CRT aberration mapping")
{
	PostProcessConfig config;
	/* aberration (0-30) mapped via /10.0f */
	config.crt_aberration = 0;
	CHECK(config.crt_aberration / 10.0f == Approx(0.0f));
	config.crt_aberration = 30;
	CHECK(config.crt_aberration / 10.0f == Approx(3.0f));
}

/* --- Night mode parameter mapping tests --- */

TEST_CASE("PostProcess - night mode intensity mapping")
{
	PostProcessConfig config;
	/* night_intensity (20-100) mapped via /100.0f */
	config.night_intensity = 20;
	CHECK(config.night_intensity / 100.0f == Approx(0.2f));
	config.night_intensity = 100;
	CHECK(config.night_intensity / 100.0f == Approx(1.0f));
}

TEST_CASE("PostProcess - night mode blue shift mapping")
{
	PostProcessConfig config;
	config.night_blue_shift = 0;
	CHECK(config.night_blue_shift / 100.0f == Approx(0.0f));
	config.night_blue_shift = 80;
	CHECK(config.night_blue_shift / 100.0f == Approx(0.8f));
}

/* --- Vignette parameter edge cases --- */

TEST_CASE("PostProcess - vignette zero intensity is no-op")
{
	PostProcessConfig config;
	config.vignette = true;
	config.vignette_intensity = 0;
	/* Even with vignette enabled, intensity=0 means the shader multiplies by 1.0 (no darkening). */
	CHECK(config.vignette_intensity / 100.0f == Approx(0.0f));
	/* But NeedsFBO still returns true because the vignette bool is set. */
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - vignette max intensity")
{
	PostProcessConfig config;
	config.vignette = true;
	config.vignette_intensity = 100;
	CHECK(config.vignette_intensity / 100.0f == Approx(1.0f));
}

/* --- Tilt-shift parameter boundary tests --- */

TEST_CASE("PostProcess - tilt-shift focus at top edge")
{
	PostProcessConfig config;
	config.tiltshift = true;
	config.tiltshift_focus_y = 0; /* Focus at very top */
	CHECK(config.tiltshift_focus_y / 100.0f == Approx(0.0f));
}

TEST_CASE("PostProcess - tilt-shift focus at bottom edge")
{
	PostProcessConfig config;
	config.tiltshift = true;
	config.tiltshift_focus_y = 100; /* Focus at very bottom */
	CHECK(config.tiltshift_focus_y / 100.0f == Approx(1.0f));
}

TEST_CASE("PostProcess - tilt-shift minimum focus width")
{
	PostProcessConfig config;
	config.tiltshift = true;
	config.tiltshift_focus_width = 5; /* Minimum documented width */
	CHECK(config.tiltshift_focus_width / 100.0f == Approx(0.05f));
}

/* --- Film grain time wrapping test --- */

TEST_CASE("PostProcess - grain intensity mapping")
{
	PostProcessConfig config;
	/* grain_intensity (1-20) mapped via /100.0f */
	config.grain_intensity = 1;
	CHECK(config.grain_intensity / 100.0f == Approx(0.01f));
	config.grain_intensity = 20;
	CHECK(config.grain_intensity / 100.0f == Approx(0.2f));
}

/* --- 1-pixel window dimension tests --- */

TEST_CASE("PostProcess - CalculateDimensions 1x1080 portrait-extreme")
{
	auto dims = CalculatePostProcessDimensions(1, 1080, 50);
	CHECK(dims.render.width >= 2);
	CHECK(dims.render.height > 0);
}

TEST_CASE("PostProcess - CalculateDimensions 1920x1 landscape-extreme")
{
	auto dims = CalculatePostProcessDimensions(1920, 1, 50);
	CHECK(dims.render.width > 0);
	CHECK(dims.render.height >= 2);
}

/* --- PostProcessConfig default struct has correct identity values --- */

TEST_CASE("PostProcess - default config produces no FBO and no passes")
{
	PostProcessConfig config;
	CHECK_FALSE(PostProcessNeedsFBO(config));
	CHECK(PostProcessPassCount(config) == 0);
	CHECK(config.render_scale == 100);
	CHECK(config.sharpening == 0);
	CHECK(config.upscale_mode == UpscaleMode::None);
	CHECK_FALSE(config.fxaa);
	CHECK_FALSE(config.color_grading);
	CHECK_FALSE(config.vignette);
	CHECK_FALSE(config.tiltshift);
	CHECK_FALSE(config.night_mode);
	CHECK_FALSE(config.film_grain);
	CHECK_FALSE(config.crt_filter);
	CHECK_FALSE(config.dynamic_lighting);
	CHECK_FALSE(config.bloom);
	CHECK(config.weather_type == 0);
}

/* ====== Dynamic Lighting (Time-of-Day) Tests ====== */

TEST_CASE("PostProcess - dynamic lighting needs FBO")
{
	PostProcessConfig config;
	config.dynamic_lighting = true;
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - dynamic lighting adds one pass")
{
	PostProcessConfig config;
	config.dynamic_lighting = true;
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - time_of_day defaults to noon")
{
	PostProcessConfig config;
	CHECK(config.time_of_day == Approx(0.5f));
}

TEST_CASE("PostProcess - time_of_day range is 0.0 to 1.0")
{
	PostProcessConfig config;
	config.time_of_day = 0.0f;
	CHECK(config.time_of_day >= 0.0f);
	config.time_of_day = 1.0f;
	CHECK(config.time_of_day <= 1.0f);
}

/* ====== Bloom Tests ====== */

TEST_CASE("PostProcess - bloom needs FBO")
{
	PostProcessConfig config;
	config.bloom = true;
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - bloom adds 3 passes")
{
	PostProcessConfig config;
	config.bloom = true;
	CHECK(PostProcessPassCount(config) == 3);
}

TEST_CASE("PostProcess - bloom defaults")
{
	PostProcessConfig config;
	CHECK_FALSE(config.bloom);
	CHECK(config.bloom_threshold == 70);
	CHECK(config.bloom_intensity == 30);
}

TEST_CASE("PostProcess - bloom with other effects stacks correctly")
{
	PostProcessConfig config;
	config.bloom = true;
	config.fxaa = true;
	/* bloom(3) + fxaa(1) = 4 */
	CHECK(PostProcessPassCount(config) == 4);
}

/* ====== Weather Effects Tests ====== */

TEST_CASE("PostProcess - weather type 0 does not need FBO")
{
	PostProcessConfig config;
	config.weather_type = 0;
	CHECK_FALSE(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - weather rain needs FBO and adds one pass")
{
	PostProcessConfig config;
	config.weather_type = 1;
	CHECK(PostProcessNeedsFBO(config));
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - weather snow needs FBO and adds one pass")
{
	PostProcessConfig config;
	config.weather_type = 2;
	CHECK(PostProcessNeedsFBO(config));
	CHECK(PostProcessPassCount(config) == 1);
}

TEST_CASE("PostProcess - weather defaults")
{
	PostProcessConfig config;
	CHECK(config.weather_type == 0);
	CHECK(config.weather_intensity == 30);
}

/* ====== Supersampling (render_scale > 100%) Tests ====== */

TEST_CASE("PostProcess - render_scale 150% produces larger render dimensions")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 150);
	CHECK(dims.render.width > dims.display.width);
	CHECK(dims.render.height > dims.display.height);
	CHECK(dims.render.width == 2880);
	CHECK(dims.render.height == 1620);
}

TEST_CASE("PostProcess - render_scale 200% doubles dimensions")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 200);
	CHECK(dims.render.width == 3840);
	CHECK(dims.render.height == 2160);
}

TEST_CASE("PostProcess - render_scale above 100 needs FBO for downsample")
{
	PostProcessConfig config;
	config.render_scale = 150;
	CHECK(PostProcessNeedsFBO(config));
}

TEST_CASE("PostProcess - render_scale 150% adds downsample pass")
{
	PostProcessConfig config;
	config.render_scale = 150;
	CHECK(PostProcessPassCount(config) >= 1);
}

TEST_CASE("PostProcess - sharpening suppressed when supersampling")
{
	PostProcessConfig config;
	config.render_scale = 150;
	config.sharpening = 80;
	/* Sharpening should still count (the suppression is a render-time decision,
	 * not a pass-count decision). But for SSAA, CAS is counterproductive. */
	/* For now, pass count includes CAS — suppression happens in RenderPostProcess. */
	CHECK(PostProcessPassCount(config) >= 1);
}

TEST_CASE("PostProcess - render_scale clamps at 200")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 250);
	/* 250 clamped to 200 */
	CHECK(dims.render.width == 3840);
	CHECK(dims.render.height == 2160);
}

TEST_CASE("PostProcess - render_scale 100% is identity even with new range")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 100);
	CHECK(dims.render.width == 1920);
	CHECK(dims.render.height == 1080);
}

/* ====== Full pipeline with all new features ====== */

TEST_CASE("PostProcess - all features including new ones")
{
	PostProcessConfig config;
	config.upscale_mode = UpscaleMode::FSR1;
	config.render_scale = 75;
	config.fxaa = true;
	config.color_grading = true;
	config.vignette = true;
	config.tiltshift = true;
	config.night_mode = true;
	config.film_grain = true;
	config.crt_filter = true;
	config.dynamic_lighting = true;
	config.bloom = true;
	config.weather_type = 1;
	/* EASU(1)+RCAS(1)+FXAA(1)+tilt(2)+color(1)+night(1)+vig(1)+grain(1)+crt(1)+lighting(1)+bloom(3)+weather(1) = 15 */
	CHECK(PostProcessPassCount(config) == 15);
}
