/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file benchmark.cpp Rendering benchmark harness for GPU performance measurement. */

#include "stdafx.h"

#include "benchmark.h"
#include "console_func.h"
#include "console_type.h"
#include "fileio_func.h"
#include "gfx_func.h"
#include "openttd.h"
#include "rev.h"
#include "video/video_driver.hpp"

#include "3rdparty/fmt/chrono.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

#include "safeguards.h"

BenchmarkHarness _benchmark;

/**
 * Start a benchmark recording session.
 * Pauses the game and disables vsync for clean measurements.
 * @param target Number of frames to capture (0 = until manual stop).
 * @param warmup Number of frames to skip before recording.
 * @param lbl User label for the output filename.
 */
void BenchmarkHarness::Start(uint32_t target, uint32_t warmup, const std::string &lbl)
{
	if (this->active.load(std::memory_order_acquire)) {
		IConsolePrint(CC_ERROR, "Benchmark already running. Use 'benchmark stop' or 'benchmark abort' first.");
		return;
	}

	this->target_frames = target;
	this->warmup_frames = warmup;
	this->frame_counter = 0;
	this->label = lbl;
	this->samples.clear();
	if (target > 0) this->samples.reserve(target);

	/* Save and override vsync. */
	this->saved_vsync = _video_vsync;
	if (_video_vsync) {
		_video_vsync = false;
		VideoDriver::GetInstance()->ToggleVsync(false);
	}

	/* Save and override pause mode — pause the game for reproducible scenes. */
	this->saved_pause = _pause_mode.Any() ? 1 : 0;
	if (!_pause_mode.Any()) {
		_pause_mode.Set(PauseMode::Normal);
	}

	/* Initialize GPU timer queries if OpenGL is available. */
	BenchmarkGPUInit();

	this->active.store(true, std::memory_order_release);

	if (target > 0) {
		IConsolePrint(CC_INFO, "Benchmark started: {} frames ({} warmup). Game paused, vsync disabled.", target, warmup);
	} else {
		IConsolePrint(CC_INFO, "Benchmark started: unlimited frames ({} warmup). Use 'benchmark stop' to finish. Game paused, vsync disabled.", warmup);
	}
}

/**
 * Stop recording, write CSV, print statistics, and restore game state.
 */
void BenchmarkHarness::Stop()
{
	if (!this->active.load(std::memory_order_acquire)) {
		IConsolePrint(CC_ERROR, "No benchmark is running.");
		return;
	}

	this->active.store(false, std::memory_order_release);

	if (this->samples.empty()) {
		IConsolePrint(CC_WARNING, "Benchmark stopped: no frames captured (warmup may not have completed).");
	} else {
		this->WriteCSV();
		this->PrintStats();
	}

	this->RestoreState();
}

/**
 * Discard all data and restore game state without writing output.
 */
void BenchmarkHarness::Abort()
{
	if (!this->active.load(std::memory_order_acquire)) {
		IConsolePrint(CC_ERROR, "No benchmark is running.");
		return;
	}

	this->active.store(false, std::memory_order_release);
	this->samples.clear();
	this->RestoreState();
	IConsolePrint(CC_INFO, "Benchmark aborted. Data discarded.");
}

/**
 * Record one frame of timing data. Called from the draw thread after Paint().
 * Fast-path: returns immediately if not active.
 * @param gpu_postprocess_ns GPU time for post-processing in nanoseconds.
 */
void BenchmarkHarness::RecordFrame(uint64_t gpu_postprocess_ns)
{
	if (!this->active.load(std::memory_order_acquire)) return;

	this->frame_counter++;

	/* Skip warmup frames. */
	if (this->frame_counter <= this->warmup_frames) return;

	PerformanceSnapshot snap = SnapshotPerformanceData();

	using namespace std::chrono;
	uint64_t now_us = static_cast<uint64_t>(
		time_point_cast<microseconds>(high_resolution_clock::now()).time_since_epoch().count());

	BenchmarkSample sample{};
	sample.frame_number = static_cast<uint32_t>(this->samples.size() + 1);
	sample.timestamp_us = now_us;
	sample.cpu_draw_us = snap.drawing_us;
	sample.cpu_paint_us = snap.video_us;
	sample.gpu_postprocess_ns = gpu_postprocess_ns;
	this->samples.push_back(sample);

	/* Auto-stop if target reached. Defer WriteCSV/PrintStats/RestoreState
	 * to the main thread via CheckAutoStop() to avoid cross-thread state mutation. */
	if (this->target_frames > 0 && this->samples.size() >= this->target_frames) {
		this->active.store(false, std::memory_order_release);
		this->auto_stop_pending.store(true, std::memory_order_release);
	}
}

/**
 * Check if the draw thread requested an auto-stop and finalize the benchmark.
 * Must be called from the main thread (e.g., from VideoDriver::Tick after RecordFrame).
 */
void BenchmarkHarness::CheckAutoStop()
{
	if (!this->auto_stop_pending.exchange(false)) return;

	IConsolePrint(CC_INFO, "Benchmark auto-completing after {} frames.", this->target_frames);
	if (!this->samples.empty()) {
		this->WriteCSV();
		this->PrintStats();
	}
	this->RestoreState();
}

/** Restore vsync and pause state to what they were before the benchmark. */
void BenchmarkHarness::RestoreState()
{
	/* Destroy GPU timer queries. */
	BenchmarkGPUDestroy();

	/* Restore vsync. */
	if (this->saved_vsync) {
		_video_vsync = true;
		VideoDriver::GetInstance()->ToggleVsync(true);
	}

	/* Restore pause state. */
	if (this->saved_pause == 0) {
		_pause_mode.Reset(PauseMode::Normal);
	}
}

/**
 * Get the output filename for the CSV.
 * @return Full path to the output file.
 */
std::string BenchmarkHarness::GetOutputFilename() const
{
	auto ts = fmt::localtime(time(nullptr));
	if (this->label.empty()) {
		return fmt::format("{}benchmark-{:%Y%m%d-%H%M%S}.csv",
			FiosGetScreenshotDir(), ts);
	}
	/* Sanitize label: strip path separators and special characters. */
	std::string safe_label;
	for (char c : this->label) {
		if (c != '/' && c != '\\' && c != ':' && c != '*' && c != '?' && c != '<' && c != '>' && c != '|' && c != '.') {
			safe_label += c;
		}
	}
	if (safe_label.empty()) safe_label = "unnamed";
	return fmt::format("{}benchmark-{:%Y%m%d-%H%M%S}-{}.csv",
		FiosGetScreenshotDir(), ts, safe_label);
}

/** Write all captured samples to a CSV file with metadata header. */
void BenchmarkHarness::WriteCSV() const
{
	std::string filename = this->GetOutputFilename();
	auto f = FioFOpenFile(filename, "wt", Subdirectory::NO_DIRECTORY);

	if (!f.has_value()) {
		IConsolePrint(CC_ERROR, "Failed to open '{}' for writing.", filename);
		return;
	}

	/* Metadata header. */
	fmt::print(*f, "# OpenTTD Benchmark - {:%Y-%m-%d %H:%M:%S}\n", fmt::localtime(time(nullptr)));
	fmt::print(*f, "# Revision: {}\n", _openttd_revision);

	/* Video driver info. */
	auto *vd = VideoDriver::GetInstance();
	fmt::print(*f, "# Video Driver: {}\n", vd != nullptr ? vd->GetInfoString() : "unknown");
	fmt::print(*f, "# Resolution: {}x{}\n", _screen.width, _screen.height);

	/* Post-processing config. */
	fmt::print(*f, "# Post-Processing: {}\n", _video_post_processing ? "on" : "off");
	fmt::print(*f, "# Render Scale: {}%\n", _video_render_scale);

	const char *upscale_names[] = {"None", "Bilinear", "FSR1", "Temporal", "Plugin"};
	uint8_t mode = _video_upscale_mode;
	fmt::print(*f, "# Upscale Mode: {}\n", mode <= 4 ? upscale_names[mode] : "Unknown");
	fmt::print(*f, "# Sharpening: {}\n", _video_sharpening);

	/* Effect toggles. */
	fmt::print(*f, "# Texture Filter: {}\n", _video_texture_filter == 0 ? "Nearest" : _video_texture_filter == 1 ? "Bilinear" : "Bicubic");
	fmt::print(*f, "# FXAA: {}\n", _video_fxaa ? "on" : "off");
	fmt::print(*f, "# Night Mode: {}\n", _video_night_mode ? "on" : "off");
	fmt::print(*f, "# CRT Filter: {}\n", _video_crt_filter ? "on" : "off");
	fmt::print(*f, "# Vignette: {}\n", _video_vignette ? "on" : "off");
	fmt::print(*f, "# Tilt-Shift: {}\n", _video_tiltshift ? "on" : "off");
	fmt::print(*f, "# Film Grain: {}\n", _video_film_grain ? "on" : "off");
	fmt::print(*f, "# Dynamic Lighting: {}\n", _video_dynamic_lighting ? "on" : "off");
	fmt::print(*f, "# Bloom: {}\n", _video_bloom ? "on" : "off");

	/* Color grading. */
	fmt::print(*f, "# Brightness: {}\n", _video_brightness);
	fmt::print(*f, "# Contrast: {}\n", _video_contrast);
	fmt::print(*f, "# Saturation: {}\n", _video_saturation);
	fmt::print(*f, "# Color Temperature: {}\n", _video_color_temperature);

	/* Effect parameters. */
	fmt::print(*f, "# Night Intensity: {}\n", _video_night_intensity);
	fmt::print(*f, "# Night Blue Shift: {}\n", _video_night_blue_shift);
	fmt::print(*f, "# CRT Scanlines: {}\n", _video_crt_scanlines);
	fmt::print(*f, "# CRT Curvature: {}\n", _video_crt_curvature);
	fmt::print(*f, "# CRT Aberration: {}\n", _video_crt_aberration);
	fmt::print(*f, "# Vignette Intensity: {}\n", _video_vignette_intensity);
	fmt::print(*f, "# Vignette Radius: {}\n", _video_vignette_radius);
	fmt::print(*f, "# Tilt-Shift Focus Y: {}\n", _video_tiltshift_focus_y);
	fmt::print(*f, "# Tilt-Shift Focus Width: {}\n", _video_tiltshift_focus_width);
	fmt::print(*f, "# Tilt-Shift Blur: {}\n", _video_tiltshift_blur);
	fmt::print(*f, "# Grain Intensity: {}\n", _video_grain_intensity);
	fmt::print(*f, "# Bloom Threshold: {}\n", _video_bloom_threshold);
	fmt::print(*f, "# Bloom Intensity: {}\n", _video_bloom_intensity);

	/* Weather. */
	const char *weather_names[] = {"None", "Rain", "Snow"};
	uint8_t wtype = _video_weather_type;
	fmt::print(*f, "# Weather Type: {}\n", wtype <= 2 ? weather_names[wtype] : "Unknown");
	fmt::print(*f, "# Weather Intensity: {}\n", _video_weather_intensity);

	/* New effects. */
	fmt::print(*f, "# Pixel Smoothing: {}\n", _video_pixel_smoothing ? "on" : "off");
	fmt::print(*f, "# Auto Supersample: {}\n", _video_auto_supersample ? "on" : "off");
	fmt::print(*f, "# Fake Shadows: {}\n", _video_fake_shadows ? "on" : "off");
	fmt::print(*f, "# Water Reflections: {}\n", _video_water_reflections ? "on" : "off");
	fmt::print(*f, "# SSAO: {}\n", _video_ssao ? "on" : "off");
	fmt::print(*f, "# Terrain Smooth: {}\n", _video_terrain_smooth ? "on" : "off");
	fmt::print(*f, "# Tree Sway: {}\n", _video_tree_sway ? "on" : "off");
	fmt::print(*f, "# Sky Clouds: {}\n", _video_sky_clouds ? "on" : "off");
	fmt::print(*f, "# Depth of Field: {}\n", _video_depth_of_field ? "on" : "off");

	fmt::print(*f, "# Vsync: forced off\n");
	fmt::print(*f, "# Warmup: {} frames\n", this->warmup_frames);
	if (!this->label.empty()) fmt::print(*f, "# Label: {}\n", this->label);

	/* CSV header and data. */
	fmt::print(*f, "frame,timestamp_us,cpu_draw_us,cpu_paint_us,gpu_postprocess_ns\n");
	for (const auto &s : this->samples) {
		fmt::print(*f, "{},{},{},{},{}\n",
			s.frame_number, s.timestamp_us, s.cpu_draw_us, s.cpu_paint_us, s.gpu_postprocess_ns);
	}

	IConsolePrint(CC_INFO, "Benchmark data written to '{}'.", filename);
}

/** Compute and print summary statistics to the console. */
void BenchmarkHarness::PrintStats() const
{
	if (this->samples.empty()) return;

	size_t n = this->samples.size();

	auto compute_stats = [n](std::vector<double> &vals) {
		std::sort(vals.begin(), vals.end());
		double sum = std::accumulate(vals.begin(), vals.end(), 0.0);
		double avg = sum / static_cast<double>(n);

		double sq_sum = 0.0;
		for (double v : vals) sq_sum += (v - avg) * (v - avg);
		double stdev = std::sqrt(sq_sum / static_cast<double>(n));

		/* Median: average of two middle elements for even n, middle element for odd n. */
		double median = (n % 2 == 0) ? (vals[n / 2 - 1] + vals[n / 2]) * 0.5 : vals[n / 2];

		struct Stats { double min, max, avg, median, p95, p99, stdev; };
		return Stats{
			vals.front(),
			vals.back(),
			avg,
			median,
			vals[std::min(n - 1, static_cast<size_t>(n * 95 / 100))],
			vals[std::min(n - 1, static_cast<size_t>(n * 99 / 100))],
			stdev
		};
	};

	/* GPU post-process stats (convert ns to ms). */
	std::vector<double> gpu_ms(n);
	for (size_t i = 0; i < n; i++) gpu_ms[i] = static_cast<double>(this->samples[i].gpu_postprocess_ns) / 1e6;
	auto gpu = compute_stats(gpu_ms);

	/* CPU paint stats (convert us to ms). */
	std::vector<double> paint_ms(n);
	for (size_t i = 0; i < n; i++) paint_ms[i] = static_cast<double>(this->samples[i].cpu_paint_us) / 1e3;
	auto paint = compute_stats(paint_ms);

	/* CPU draw stats (convert us to ms). */
	std::vector<double> draw_ms(n);
	for (size_t i = 0; i < n; i++) draw_ms[i] = static_cast<double>(this->samples[i].cpu_draw_us) / 1e3;
	auto draw = compute_stats(draw_ms);

	IConsolePrint(CC_INFO, "Benchmark complete: {} frames captured.", n);
	IConsolePrint(CC_INFO, "  GPU Post-Process:");
	IConsolePrint(CC_INFO, "    min: {:.2f}ms  max: {:.2f}ms  avg: {:.2f}ms  median: {:.2f}ms", gpu.min, gpu.max, gpu.avg, gpu.median);
	IConsolePrint(CC_INFO, "    p95: {:.2f}ms  p99: {:.2f}ms  stdev: {:.2f}ms", gpu.p95, gpu.p99, gpu.stdev);
	IConsolePrint(CC_INFO, "  CPU Paint (PFE_VIDEO):");
	IConsolePrint(CC_INFO, "    min: {:.2f}ms  max: {:.2f}ms  avg: {:.2f}ms  median: {:.2f}ms", paint.min, paint.max, paint.avg, paint.median);
	IConsolePrint(CC_INFO, "    p95: {:.2f}ms  p99: {:.2f}ms  stdev: {:.2f}ms", paint.p95, paint.p99, paint.stdev);
	IConsolePrint(CC_INFO, "  CPU Draw (PFE_DRAWING):");
	IConsolePrint(CC_INFO, "    min: {:.2f}ms  max: {:.2f}ms  avg: {:.2f}ms  median: {:.2f}ms", draw.min, draw.max, draw.avg, draw.median);
	IConsolePrint(CC_INFO, "    p95: {:.2f}ms  p99: {:.2f}ms  stdev: {:.2f}ms", draw.p95, draw.p99, draw.stdev);

	/* Effective FPS from paint times. */
	if (paint.avg > 0.0) {
		IConsolePrint(CC_INFO, "  Effective FPS: avg {:.1f}  min {:.1f}  max {:.1f}",
			1000.0 / paint.avg, 1000.0 / paint.max, 1000.0 / paint.min);
	}
}

/**
 * Hook called from VideoDriver::Tick() after Paint().
 * Reads GPU time from the OpenGL backend and records the frame.
 */
void BenchmarkRecordFrame()
{
	if (!_benchmark.IsActive()) return;

	uint64_t gpu_ns = BenchmarkGPUReadBack();
	_benchmark.RecordFrame(gpu_ns);
}
