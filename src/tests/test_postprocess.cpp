/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file postprocess.cpp Tests for GPU post-processing pipeline logic. */

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

TEST_CASE("PostProcess - CalculateDimensions clamps above 100")
{
	auto dims = CalculatePostProcessDimensions(1920, 1080, 150);
	CHECK(dims.render.width == 1920);
	CHECK(dims.render.height == 1080);
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
	CHECK(con1[0] == Approx(1.0f / 960.0f));
	CHECK(con1[1] == Approx(1.0f / 540.0f));
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

TEST_CASE("PostProcess - color grading disabled means zero passes from it")
{
	PostProcessConfig config;
	config.color_grading = false;
	config.cg_brightness = 25; /* Non-identity but grading disabled */
	CHECK(PostProcessPassCount(config) == 0);
}
