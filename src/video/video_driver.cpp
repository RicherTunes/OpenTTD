/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file video_driver.cpp Common code between video driver implementations. */

#include "../stdafx.h"
#include "../core/random_func.hpp"
#include "../network/network.h"
#include "../blitter/factory.hpp"
#include "../debug.h"
#include "../driver.h"
#include "../fontcache.h"
#include "../gfx_func.h"
#include "../gfxinit.h"
#include "../progress.h"
#include "../rev.h"
#include "../thread.h"
#include "../window_func.h"
#include "../benchmark.h"
#include "video_driver.hpp"

#include "../safeguards.h"

bool _video_hw_accel; ///< Whether to consider hardware accelerated video drivers on startup.
bool _video_vsync; ///< Whether we should use vsync (only if active video driver supports HW acceleration).
bool _video_post_processing = false; ///< Whether GPU post-processing effects are enabled.
uint8_t _video_render_scale = 100; ///< Internal render resolution as percentage of display (50-200).
uint8_t _video_upscale_mode = 0; ///< Upscale mode: 0=None, 1=Bilinear, 2=FSR1.
uint8_t _video_sharpening = 50; ///< CAS sharpening intensity (0-100).
uint8_t _video_texture_filter = 0; ///< Texture filter: 0=Nearest, 1=Bilinear, 2=Smooth.
bool _video_fxaa = false; ///< Whether FXAA anti-aliasing is enabled.
uint8_t _video_fxaa_quality = 75; ///< FXAA sub-pixel quality (0-100).
uint8_t _video_fxaa_threshold = 8; ///< FXAA edge detection threshold (1-50). Lower = more sensitive to edges.
bool _video_night_mode = false; ///< Whether night mode effect is enabled.
bool _video_crt_filter = false; ///< Whether CRT scanline filter is enabled.
bool _video_vignette = false; ///< Whether vignette edge darkening is enabled.
bool _video_tiltshift = false; ///< Whether tilt-shift miniature effect is enabled.
bool _video_film_grain = false; ///< Whether film grain overlay is enabled.
int8_t _video_brightness = 0; ///< Brightness offset (-50 to 50).
uint8_t _video_contrast = 100; ///< Contrast percentage (50-200).
uint8_t _video_saturation = 100; ///< Saturation percentage (0-200).
int8_t _video_color_temperature = 0; ///< Color temperature shift (-100 to 100).
uint8_t _video_night_intensity = 60; ///< Night mode darkness (20-100).
uint8_t _video_night_blue_shift = 30; ///< Night mode blue tint (0-80).
uint8_t _video_crt_scanlines = 15; ///< CRT scanline intensity (0-50).
uint8_t _video_crt_curvature = 0; ///< CRT screen curvature (0-50).
uint8_t _video_crt_aberration = 5; ///< CRT chromatic aberration (0-30).
uint8_t _video_vignette_intensity = 30; ///< Vignette darkness (0-100).
uint8_t _video_vignette_radius = 85; ///< Vignette inner radius (50-150).
uint8_t _video_vignette_softness = 45; ///< Vignette feather softness (10-80).
uint8_t _video_tiltshift_focus_y = 45; ///< Tilt-shift focus center (0-100).
uint8_t _video_tiltshift_focus_width = 25; ///< Tilt-shift focus band width (5-80).
uint8_t _video_tiltshift_blur = 30; ///< Tilt-shift blur strength (10-60).
uint8_t _video_grain_intensity = 4; ///< Film grain intensity (1-20).
bool _video_dynamic_lighting = false; ///< Whether time-of-day lighting is enabled.
bool _video_bloom = false; ///< Whether bloom glow effect is enabled.
uint8_t _video_bloom_threshold = 70; ///< Bloom luminance threshold (0-100).
uint8_t _video_bloom_intensity = 30; ///< Bloom blend strength (0-100).
uint8_t _video_weather_type = 0; ///< Weather overlay: 0=none, 1=rain, 2=snow.
uint8_t _video_weather_intensity = 30; ///< Weather effect strength (0-100).
bool _video_pixel_smoothing = false; ///< Whether pixel art smoothing is enabled.
uint8_t _video_pixel_smooth_amount = 70; ///< Pixel art smoothing intensity (0-100).
bool _video_auto_supersample = false; ///< Enable automatic supersampling at close zoom.
bool _video_fake_shadows = false; ///< Whether fake directional shadows are enabled.
uint8_t _video_shadow_intensity = 40; ///< Shadow darkness (0-100).
uint16_t _video_shadow_angle = 45; ///< Shadow angle in degrees (0-359).
uint8_t _video_shadow_length = 8; ///< Shadow length in pixels (1-30).
uint8_t _video_shadow_softness = 3; ///< Shadow edge softness (1-10).
bool _video_water_reflections = false; ///< Whether screen-space water reflections are enabled.
uint8_t _video_reflection_intensity = 30; ///< Water reflection strength (0-100).
uint8_t _video_reflection_distortion = 5; ///< Water reflection wave distortion amplitude (0-20).
bool _video_ssao = false; ///< Whether screen-space ambient occlusion is enabled.
uint8_t _video_ssao_radius = 4; ///< SSAO sample radius in pixels (1-15).
uint8_t _video_ssao_intensity = 50; ///< SSAO occlusion darkness (0-100).
uint8_t _video_ssao_samples = 8; ///< SSAO samples per pixel (4-16).
bool _video_terrain_smooth = false; ///< Whether terrain transition smoothing is enabled.
uint8_t _video_terrain_smooth_radius = 2; ///< Terrain smoothing kernel radius (1-5).
uint8_t _video_terrain_smooth_strength = 50; ///< Terrain smoothing strength (0-100).
bool _video_tree_sway = false; ///< Whether animated tree sway is enabled.
uint8_t _video_tree_sway_amount = 3; ///< Tree sway amplitude in pixels (1-10).
uint8_t _video_tree_sway_speed = 50; ///< Tree sway animation speed (10-100).
bool _video_sky_clouds = false; ///< Whether procedural sky with clouds is enabled.
uint8_t _video_cloud_density = 50; ///< Cloud coverage density (0-100).
uint8_t _video_cloud_speed = 30; ///< Cloud drift speed (0-100).
uint8_t _video_sky_brightness = 70; ///< Sky background brightness (0-100).
bool _video_depth_of_field = false; ///< Whether depth-of-field blur is enabled.
uint8_t _video_dof_focus_point = 50; ///< DoF focus distance (0-100).
uint8_t _video_dof_aperture = 30; ///< DoF aperture / blur strength (0-100).
uint8_t _video_dof_range = 40; ///< DoF focus range width (0-100).
bool _video_cpu_viewport_scaling = false; ///< Whether CPU viewport scaling is enabled (render viewport at reduced resolution).

void VideoDriver::GameLoop()
{
	this->next_game_tick += this->GetGameInterval();

	/* Avoid next_game_tick getting behind more and more if it cannot keep up. */
	auto now = std::chrono::steady_clock::now();
	if (this->next_game_tick < now - ALLOWED_DRIFT * this->GetGameInterval()) this->next_game_tick = now;

	{
		std::lock_guard<std::mutex> lock(this->game_state_mutex);

		::GameLoop();
	}
}

void VideoDriver::GameThread()
{
	while (!_exit_game) {
		this->GameLoop();

		auto now = std::chrono::steady_clock::now();
		if (this->next_game_tick > now) {
			std::this_thread::sleep_for(this->next_game_tick - now);
		} else {
			/* Ensure we yield this thread if drawings wants to take a lock on
			 * the game state. This is mainly because most OSes have an
			 * optimization that if you unlock/lock a mutex in the same thread
			 * quickly, it will never context switch even if there is another
			 * thread waiting to take the lock on the same mutex. */
			std::lock_guard<std::mutex> lock(this->game_thread_wait_mutex);
		}
	}
}

/**
 * Pause the game-loop for a bit, releasing the game-state lock. This allows,
 * if the draw-tick requested this, the drawing to happen.
 */
void VideoDriver::GameLoopPause()
{
	/* If we are not called from the game-thread, ignore this request. */
	if (std::this_thread::get_id() != this->game_thread.get_id()) return;

	this->game_state_mutex.unlock();

	{
		/* See GameThread() for more details on this lock. */
		std::lock_guard<std::mutex> lock(this->game_thread_wait_mutex);
	}

	this->game_state_mutex.lock();
}

/* static */ void VideoDriver::GameThreadThunk(VideoDriver *drv)
{
	drv->GameThread();
}

void VideoDriver::StartGameThread()
{
	if (this->is_game_threaded) {
		this->is_game_threaded = StartNewThread(&this->game_thread, "ottd:game", &VideoDriver::GameThreadThunk, this);
	}

	Debug(driver, 1, "using {}thread for game-loop", this->is_game_threaded ? "" : "no ");
}

void VideoDriver::StopGameThread()
{
	if (!this->is_game_threaded) return;

	this->game_thread.join();
}

void VideoDriver::Tick()
{
	if (!this->is_game_threaded && std::chrono::steady_clock::now() >= this->next_game_tick) {
		this->GameLoop();

		/* For things like dedicated server, don't run a separate draw-tick. */
		if (!this->HasGUI()) {
			::InputLoop();
			::UpdateWindows();
			this->next_draw_tick = this->next_game_tick;
		}
	}

	auto now = std::chrono::steady_clock::now();
	if (this->HasGUI() && now >= this->next_draw_tick) {
		this->next_draw_tick += this->GetDrawInterval();
		/* Avoid next_draw_tick getting behind more and more if it cannot keep up. */
		if (this->next_draw_tick < now - ALLOWED_DRIFT * this->GetDrawInterval()) this->next_draw_tick = now;

		/* Locking video buffer can block (especially with vsync enabled), do it before taking game state lock. */
		this->LockVideoBuffer();

		{
			/* Tell the game-thread to stop so we can have a go. */
			std::lock_guard<std::mutex> lock_wait(this->game_thread_wait_mutex);
			std::lock_guard<std::mutex> lock_state(this->game_state_mutex);

			/* Keep the interactive randomizer a bit more random by requesting
			 * new values when-ever we can. */
			InteractiveRandom();

			this->DrainCommandQueue();

			while (this->PollEvent()) {}
			this->InputLoop();

			/* Check if the fast-forward button is still pressed. */
			if (fast_forward_key_pressed && !_networking && _game_mode != GM_MENU) {
				ChangeGameSpeed(true);
				this->fast_forward_via_key = true;
			} else if (this->fast_forward_via_key) {
				ChangeGameSpeed(false);
				this->fast_forward_via_key = false;
			}

			::InputLoop();

			/* Prevent drawing when switching mode, as windows can be removed when they should still appear. */
			if (_game_mode == GM_BOOTSTRAP || _switch_mode == SM_NONE || HasModalProgress()) {
				::UpdateWindows();
			}

			this->PopulateSystemSprites();
		}

		this->CheckPaletteAnim();
		this->Paint();
		BenchmarkRecordFrame();
		_benchmark.CheckAutoStop();

		this->UnlockVideoBuffer();

		/* Wait till the first successful drawing tick before marking the driver as operational. */
		static bool first_draw_tick = true;
		if (first_draw_tick) {
			first_draw_tick = false;
			DriverFactoryBase::MarkVideoDriverOperational();
		}
	}
}

void VideoDriver::SleepTillNextTick()
{
	auto next_tick = this->next_draw_tick;
	auto now = std::chrono::steady_clock::now();

	if (!this->is_game_threaded) {
		next_tick = min(next_tick, this->next_game_tick);
	}

	if (next_tick > now) {
		std::this_thread::sleep_for(next_tick - now);
	}
}

/**
 * Get the caption to use for the game's title bar.
 * @return The caption.
 */
/* static */ std::string VideoDriver::GetCaption()
{
	return fmt::format("OpenTTD {}", _openttd_revision);
}
