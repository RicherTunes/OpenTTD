/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file benchmark.h Rendering benchmark harness for GPU performance measurement. */

#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "framerate_type.h"

#include <string>
#include <vector>

/** One frame of benchmark timing data. */
struct BenchmarkSample {
	uint32_t frame_number;        ///< Frame index (after warmup).
	uint64_t timestamp_us;        ///< Wall-clock timestamp in microseconds.
	uint64_t cpu_draw_us;         ///< PFE_DRAWING duration (CPU-side draw).
	uint64_t cpu_paint_us;        ///< PFE_VIDEO duration (paint + swap).
	uint64_t gpu_postprocess_ns;  ///< GL_TIME_ELAPSED for post-processing pass.
};

/** Rendering benchmark harness that captures per-frame timing to CSV. */
class BenchmarkHarness {
	bool active = false;           ///< Currently recording.
	uint32_t target_frames = 0;    ///< Auto-stop after this many frames (0 = manual stop).
	uint32_t warmup_frames = 300;  ///< Frames to skip before recording.
	uint32_t frame_counter = 0;    ///< Total frames seen since Start().
	std::string label;             ///< User label for output filename.
	bool saved_vsync = false;      ///< Original vsync state to restore.
	uint8_t saved_pause = 0;       ///< Original pause mode to restore.

	std::vector<BenchmarkSample> samples; ///< Recorded frame data.

	std::string GetOutputFilename() const;
	void WriteCSV() const;
	void PrintStats() const;
	void RestoreState();

public:
	void Start(uint32_t target, uint32_t warmup, const std::string &lbl);
	void Stop();
	void Abort();
	void RecordFrame(uint64_t gpu_postprocess_ns);
	bool IsActive() const { return this->active; }
	uint32_t GetFramesCaptured() const { return static_cast<uint32_t>(this->samples.size()); }
	uint32_t GetTargetFrames() const { return this->target_frames; }
	uint32_t GetWarmupFrames() const { return this->warmup_frames; }
	uint32_t GetFrameCounter() const { return this->frame_counter; }
};

extern BenchmarkHarness _benchmark;

void BenchmarkRecordFrame();

/* GPU benchmark query bridge functions (implemented in opengl.cpp). */
void BenchmarkGPUInit();
void BenchmarkGPUDestroy();
uint64_t BenchmarkGPUReadBack();

#endif /* BENCHMARK_H */
