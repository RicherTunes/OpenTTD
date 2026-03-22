/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file opengl.h OpenGL video driver support. */

#ifndef VIDEO_OPENGL_H
#define VIDEO_OPENGL_H

#include "../core/geometry_type.hpp"
#include "../gfx_type.h"
#include "../spriteloader/spriteloader.hpp"
#include "../misc/lrucache.hpp"
#include "postprocess.h"

#include <chrono>
#include <vector>

typedef void (*OGLProc)();
typedef OGLProc (*GetOGLProcAddressProc)(const char *proc);

bool IsOpenGLVersionAtLeast(uint8_t major, uint8_t minor);
bool HasStringInExtensionList(std::string_view string, std::string_view substring);

class OpenGLSprite;

using OpenGLSpriteLRUCache = LRUCache<SpriteID, std::unique_ptr<OpenGLSprite>>;

/** Platform-independent back-end class for OpenGL video drivers. */
class OpenGLBackend : public SpriteEncoder {
private:
	static OpenGLBackend *instance; ///< Singleton instance pointer.

	bool persistent_mapping_supported = false; ///< Persistent pixel buffer mapping supported.
	GLsync sync_vid_mapping{}; ///< Sync object for the persistently mapped video buffer.
	GLsync sync_anim_mapping{}; ///< Sync object for the persistently mapped animation buffer.

	void *vid_buffer = nullptr; ///< Pointer to the mapped video buffer.
	GLuint vid_pbo = 0; ///< Pixel buffer object storing the memory used for the video driver to draw to.
	GLuint vid_texture = 0; ///< Texture handle for the video buffer texture.
	GLuint vid_program = 0; ///< Shader program for rendering a RGBA video buffer.
	GLuint pal_program = 0; ///< Shader program for rendering a paletted video buffer.
	GLuint vao_quad = 0; ///< Vertex array object storing the rendering state for the fullscreen quad.
	GLuint vbo_quad = 0; ///< Vertex buffer with a fullscreen quad.
	GLuint pal_texture = 0; ///< Palette lookup texture.

	void *anim_buffer = nullptr; ///< Pointer to the mapped animation buffer.
	GLuint anim_pbo = 0; ///< Pixel buffer object storing the memory used for the animation buffer.
	GLuint anim_texture = 0; ///< Texture handle for the animation buffer texture.

	void *class_buffer = nullptr;       ///< Pointer to the mapped classification buffer.
	GLuint class_pbo = 0;               ///< PBO for classification buffer upload.
	GLuint class_texture = 0;           ///< GL_R8 texture for classification data.
	GLsync sync_class_mapping{};        ///< Sync object for classification buffer mapping.

	GLuint remap_program = 0; ///< Shader program for blending and rendering a RGBA + remap texture.
	GLint  remap_sprite_loc = 0; ///< Uniform location for sprite parameters.
	GLint  remap_screen_loc = 0; ///< Uniform location for screen size.
	GLint  remap_zoom_loc = 0; ///< Uniform location for sprite zoom.
	GLint  remap_rgb_loc = 0; ///< Uniform location for RGB mode flag.

	GLuint sprite_program = 0; ///< Shader program for blending and rendering a sprite to the video buffer.
	GLint  sprite_sprite_loc = 0; ///< Uniform location for sprite parameters.
	GLint  sprite_screen_loc = 0; ///< Uniform location for screen size.
	GLint  sprite_zoom_loc = 0; ///< Uniform location for sprite zoom.
	GLint  sprite_rgb_loc = 0; ///< Uniform location for RGB mode flag.
	GLint  sprite_crash_loc = 0; ///< Uniform location for crash remap mode flag.

	/* Post-processing pipeline resources. */
	GLuint pp_fbo[2] = {};           ///< Ping-pong framebuffer objects (display resolution).
	GLuint pp_tex[2] = {};           ///< Colour texture attachments for ping-pong FBOs.
	GLuint pp_scene_fbo = 0;         ///< Scene FBO at render resolution (for upscaling).
	GLuint pp_scene_tex = 0;         ///< Scene texture at render resolution.
	Dimension pp_render_size = {};   ///< Internal render resolution.
	Dimension pp_display_size = {};  ///< Display/window resolution.
	bool pp_active = false;          ///< Post-processing pipeline is currently active.
	bool pp_fbo_supported = false;   ///< FBO extensions are available.
	bool pp_vid_filter_was_linear = false; ///< Whether vid_texture was set to GL_LINEAR last frame.

	/* Motion vector rasterization resources (GL 4.3+ compute). */
	bool mv_compute_supported = false; ///< GL 4.3+ compute shaders available.
	GLuint mv_compute_program = 0;   ///< Compiled compute shader program.
	GLuint mv_cmd_ssbo = 0;          ///< SSBO for draw command upload.
	GLuint mv_tile_ssbo = 0;         ///< SSBO for tile bin data upload.
	GLuint mv_texture = 0;           ///< RG16F motion vector output texture.
	GLuint mv_depth_texture = 0;     ///< R16F depth output texture.
	GLint mv_screen_size_loc = -1;   ///< Compute uniform: screen_size.
	GLint mv_tile_count_loc = -1;    ///< Compute uniform: tile_count.
	GLint mv_global_motion_loc = -1; ///< Compute uniform: global_motion.
	GLint mv_max_cmds_loc = -1;      ///< Compute uniform: max_cmds_per_tile.

	bool InitMVCompute();
	void DestroyMVResources();
	void DispatchMVRasterization();

	/* Temporal accumulation resources. */
	GLuint pp_temporal_program = 0;  ///< Temporal accumulation shader program.
	GLuint pp_history_fbo = 0;       ///< FBO for rendering into history texture.
	GLuint pp_history_tex = 0;       ///< History texture (previous frame output).
	GLint pp_temporal_texel_loc = -1;  ///< texel_size uniform.
	GLint pp_temporal_jitter_loc = -1; ///< jitter_offset uniform.
	GLint pp_temporal_reset_loc = -1;  ///< reset uniform.
	GLint pp_temporal_history_loc = -1; ///< history_tex sampler uniform.
	GLint pp_temporal_mv_loc = -1;     ///< mv_tex sampler uniform.
	int32_t pp_temporal_prev_scroll_x = 0; ///< Previous frame viewport X for scene cut detection.
	int32_t pp_temporal_prev_scroll_y = 0; ///< Previous frame viewport Y for scene cut detection.
	uint8_t pp_temporal_prev_zoom = 0;     ///< Previous frame zoom level.
	uint32_t pp_temporal_frame_count = 0;  ///< Frames since last reset.

	GLuint pp_blit_program = 0;      ///< Simple blit shader program.
	GLuint pp_cas_program = 0;       ///< CAS sharpening shader program.
	GLuint pp_fsr_easu_program = 0;  ///< FSR 1.0 EASU (upscale) shader program.
	GLuint pp_fsr_rcas_program = 0;  ///< FSR 1.0 RCAS (sharpen) shader program.
	GLuint pp_fxaa_program = 0;      ///< FXAA anti-aliasing shader program.
	GLuint pp_color_program = 0;     ///< Color grading shader program.
	GLuint pp_vignette_program = 0;  ///< Vignette effect shader program.
	GLuint pp_tiltshift_h_program = 0; ///< Tilt-shift horizontal blur shader program.
	GLuint pp_tiltshift_v_program = 0; ///< Tilt-shift vertical blur shader program.
	GLuint pp_night_program = 0;     ///< Night mode shader program.
	GLuint pp_grain_program = 0;     ///< Film grain shader program.
	GLuint pp_bicubic_program = 0;   ///< Bicubic texture filter shader program.
	GLuint pp_pixel_smooth_program = 0; ///< Pixel art smoothing shader program.
	GLint pp_pixel_smooth_texel_loc = -1; ///< Pixel smooth texel_size uniform location.
	GLint pp_pixel_smooth_amount_loc = -1; ///< Pixel smooth smooth_amount uniform location.
	GLuint pp_crt_program = 0;       ///< CRT scanline filter shader program.
	GLuint pp_lighting_program = 0;  ///< Dynamic time-of-day lighting shader program.
	GLuint pp_bloom_threshold_program = 0; ///< Bloom threshold extraction shader program.
	GLuint pp_bloom_blur_h_program = 0;    ///< Bloom horizontal blur shader program.
	GLuint pp_bloom_blur_v_program = 0;    ///< Bloom vertical blur shader program.
	GLuint pp_bloom_composite_program = 0; ///< Bloom composite (additive blend) shader program.
	GLint pp_bloom_composite_orig_loc = -1; ///< Bloom composite bloom_original sampler uniform.
	GLuint pp_weather_program = 0;   ///< Weather particle overlay shader program.
	GLuint pp_shadow_program = 0;        ///< Fake shadow shader program.
	GLuint pp_water_reflect_program = 0;        ///< Water reflection shader program.
	GLuint pp_ssao_program = 0;          ///< Screen-space ambient occlusion shader program.
	GLuint pp_terrain_smooth_program = 0; ///< Terrain transition smoothing shader program.
	GLuint pp_tree_sway_program = 0;     ///< Animated tree sway shader program.
	GLuint pp_sky_program = 0;           ///< Procedural sky with clouds shader program.
	GLuint pp_dof_program = 0;           ///< Depth-of-field blur shader program.
	GLuint pp_toon_program = 0;          ///< Toon/cartoon rendering shader program.
	GLuint pp_heat_haze_program = 0;     ///< Heat haze distortion shader program.
	GLuint pp_water_waves_program = 0;   ///< Animated water waves shader program.
	GLuint pp_seasonal_program = 0;      ///< Seasonal vegetation colour shift shader program.
	GLuint pp_downsample_program = 0;    ///< Downsample shader for supersampling.
	GLint pp_downsample_texel_loc = -1;  ///< Downsample texel_size uniform.
	GLuint pp_debug_class_program = 0;   ///< Shader for classification debug visualization.
	GLint pp_debug_class_class_loc = -1; ///< Debug class class_tex sampler uniform.

	/* CPU viewport scaling scratch buffer resources. */
	GLuint vp_texture = 0;          ///< Texture for the reduced-resolution viewport.
	std::vector<uint32_t> vp_buffer; ///< CPU-side viewport scratch buffer (BGRA pixels).
	int vp_width = 0;               ///< Scratch buffer width.
	int vp_height = 0;              ///< Scratch buffer height.
	int vp_pitch = 0;               ///< Scratch buffer pitch (pixels per row).
	Rect vp_screen_rect = {};       ///< Main viewport rect in display coordinates.
	bool vp_cpu_scaling = false;    ///< CPU viewport scaling is active this frame.

	/* Cached uniform locations for all post-processing shaders. */
	GLint pp_cas_sharp_loc = -1;     ///< CAS sharpness uniform location.
	GLint pp_cas_texel_loc = -1;     ///< CAS texel_size uniform location.
	GLint pp_easu_con0_loc = -1;     ///< FSR EASU constant 0 uniform location.
	GLint pp_easu_con1_loc = -1;     ///< FSR EASU constant 1 uniform location.
	GLint pp_easu_con2_loc = -1;     ///< FSR EASU constant 2 uniform location.
	GLint pp_easu_con3_loc = -1;     ///< FSR EASU constant 3 uniform location.
	GLint pp_rcas_con_loc = -1;      ///< FSR RCAS strength uniform location.
	GLint pp_rcas_texel_loc = -1;    ///< FSR RCAS texel_size uniform location.
	GLint pp_fxaa_texel_loc = -1;    ///< FXAA texel_size uniform location.
	GLint pp_fxaa_subpix_loc = -1;   ///< FXAA subpix_quality uniform location.
	GLint pp_fxaa_edge_loc = -1;     ///< FXAA edge_threshold uniform location.
	GLint pp_ts_texel_loc[2] = {-1, -1}; ///< Tilt-shift texel_size locations [h, v].
	GLint pp_ts_focus_loc[2] = {-1, -1}; ///< Tilt-shift focus_position locations.
	GLint pp_ts_width_loc[2] = {-1, -1}; ///< Tilt-shift focus_width locations.
	GLint pp_ts_blur_loc[2] = {-1, -1};  ///< Tilt-shift blur_strength locations.
	GLint pp_ts_viewport_loc[2] = {-1, -1}; ///< Tilt-shift viewport_uv locations [h, v].
	GLint pp_cg_brightness_loc = -1; ///< Color grading brightness uniform location.
	GLint pp_cg_contrast_loc = -1;   ///< Color grading contrast uniform location.
	GLint pp_cg_saturation_loc = -1; ///< Color grading saturation uniform location.
	GLint pp_cg_temperature_loc = -1;///< Color grading temperature uniform location.
	GLint pp_vig_intensity_loc = -1; ///< Vignette intensity uniform location.
	GLint pp_vig_radius_loc = -1;    ///< Vignette radius uniform location.
	GLint pp_vig_softness_loc = -1;  ///< Vignette softness uniform location.
	GLint pp_vig_screen_loc = -1;    ///< Vignette screen_size uniform location.
	GLint pp_night_int_loc = -1;     ///< Night mode intensity uniform location.
	GLint pp_night_blue_loc = -1;    ///< Night mode blue shift uniform location.
	GLint pp_night_viewport_loc = -1; ///< Night mode viewport_uv uniform location.
	GLint pp_grain_int_loc = -1;     ///< Film grain intensity uniform location.
	GLint pp_grain_time_loc = -1;    ///< Film grain time uniform location.
	GLint pp_bicubic_texel_loc = -1; ///< Bicubic texel_size uniform location.
	GLint pp_crt_texel_loc = -1;     ///< CRT texel_size uniform location.
	GLint pp_crt_res_loc = -1;       ///< CRT resolution uniform location.
	GLint pp_crt_scanline_loc = -1;  ///< CRT scanline_intensity uniform location.
	GLint pp_crt_curve_loc = -1;     ///< CRT curvature uniform location.
	GLint pp_crt_aberr_loc = -1;     ///< CRT chromatic_aberr uniform location.
	GLint pp_lighting_tod_loc = -1;  ///< Dynamic lighting time_of_day uniform location.
	GLint pp_bloom_thresh_loc = -1;  ///< Bloom threshold uniform location.
	GLint pp_bloom_int_loc = -1;     ///< Bloom intensity uniform location.
	GLint pp_bloom_blur_h_texel_loc = -1; ///< Bloom blur H texel_size uniform location.
	GLint pp_bloom_blur_v_texel_loc = -1; ///< Bloom blur V texel_size uniform location.
	GLint pp_weather_time_loc = -1;  ///< Weather time uniform location.
	GLint pp_weather_int_loc = -1;   ///< Weather intensity uniform location.
	GLint pp_weather_type_loc = -1;  ///< Weather type uniform location.
	GLint pp_shadow_intensity_loc = -1;  ///< Shadow intensity uniform location.
	GLint pp_shadow_dir_loc = -1;        ///< Shadow direction uniform location.
	GLint pp_shadow_length_loc = -1;     ///< Shadow length uniform location.
	GLint pp_shadow_samples_loc = -1;    ///< Shadow samples uniform location.
	GLint pp_shadow_texel_loc = -1;      ///< Shadow texel_size uniform location.
	GLint pp_water_reflect_intensity_loc = -1;   ///< Reflection intensity uniform.
	GLint pp_water_reflect_distortion_loc = -1;  ///< Wave distortion uniform.
	GLint pp_water_reflect_time_loc = -1;        ///< Animation time uniform.
	GLint pp_water_reflect_texel_loc = -1;       ///< Texel size uniform.
	GLint pp_water_reflect_viewport_loc = -1;    ///< Water reflection viewport_uv uniform.
	GLint pp_water_reflect_class_loc = -1;       ///< Water reflect class_tex sampler location.

	/* Viewport UV mask locations for all viewport-masked shaders. */
	GLint pp_cas_viewport_loc = -1;              ///< CAS viewport_uv uniform location.
	GLint pp_fxaa_viewport_loc = -1;             ///< FXAA viewport_uv uniform location.
	GLint pp_cg_viewport_loc = -1;               ///< Color grading viewport_uv uniform location.
	GLint pp_vig_viewport_loc = -1;              ///< Vignette viewport_uv uniform location.
	GLint pp_grain_viewport_loc = -1;            ///< Film grain viewport_uv uniform location.
	GLint pp_pixel_smooth_viewport_loc = -1;     ///< Pixel smooth viewport_uv uniform location.
	GLint pp_crt_viewport_loc = -1;              ///< CRT viewport_uv uniform location.
	GLint pp_lighting_viewport_loc = -1;         ///< Dynamic lighting viewport_uv uniform location.
	GLint pp_bloom_thresh_viewport_loc = -1;     ///< Bloom threshold viewport_uv uniform location.
	GLint pp_bloom_blur_h_viewport_loc = -1;     ///< Bloom blur H viewport_uv uniform location.
	GLint pp_bloom_blur_v_viewport_loc = -1;     ///< Bloom blur V viewport_uv uniform location.
	GLint pp_bloom_composite_viewport_loc = -1;  ///< Bloom composite viewport_uv uniform location.
	GLint pp_weather_viewport_loc = -1;          ///< Weather viewport_uv uniform location.
	GLint pp_shadow_viewport_loc = -1;           ///< Shadow viewport_uv uniform location.
	GLint pp_ssao_viewport_loc = -1;             ///< SSAO viewport_uv uniform location.
	GLint pp_terrain_smooth_viewport_loc = -1;   ///< Terrain smooth viewport_uv uniform location.
	GLint pp_tree_sway_viewport_loc = -1;        ///< Tree sway viewport_uv uniform location.
	GLint pp_sky_viewport_loc = -1;              ///< Sky viewport_uv uniform location.
	GLint pp_dof_viewport_loc = -1;              ///< DoF viewport_uv uniform location.
	GLint pp_toon_viewport_loc = -1;             ///< Toon viewport_uv uniform location.
	GLint pp_haze_viewport_loc = -1;             ///< Heat haze viewport_uv uniform location.
	GLint pp_ssao_radius_loc = -1;       ///< SSAO radius uniform location.
	GLint pp_ssao_intensity_loc = -1;    ///< SSAO intensity uniform location.
	GLint pp_ssao_samples_loc = -1;      ///< SSAO samples uniform location.
	GLint pp_ssao_texel_loc = -1;        ///< SSAO texel_size uniform location.
	GLint pp_terrain_smooth_radius_loc = -1;   ///< Terrain smooth radius uniform location.
	GLint pp_terrain_smooth_strength_loc = -1; ///< Terrain smooth strength uniform location.
	GLint pp_terrain_smooth_texel_loc = -1;    ///< Terrain smooth texel_size uniform location.
	GLint pp_tree_sway_amount_loc = -1;  ///< Tree sway amount uniform location.
	GLint pp_tree_sway_speed_loc = -1;   ///< Tree sway speed uniform location.
	GLint pp_tree_sway_time_loc = -1;    ///< Tree sway time uniform location.
	GLint pp_tree_sway_texel_loc = -1;   ///< Tree sway texel_size uniform location.
	GLint pp_sky_density_loc = -1;       ///< Sky cloud density uniform location.
	GLint pp_sky_speed_loc = -1;         ///< Sky cloud speed uniform location.
	GLint pp_sky_brightness_loc = -1;    ///< Sky brightness uniform location.
	GLint pp_sky_time_loc = -1;          ///< Sky time uniform location.
	GLint pp_dof_focus_loc = -1;         ///< DoF focus point uniform location.
	GLint pp_dof_aperture_loc = -1;      ///< DoF aperture uniform location.
	GLint pp_dof_range_loc = -1;         ///< DoF focus range uniform location.
	GLint pp_dof_texel_loc = -1;         ///< DoF texel_size uniform location.
	GLint pp_toon_texel_loc = -1;        ///< Toon texel_size uniform location.
	GLint pp_toon_edge_loc = -1;         ///< Toon edge_threshold uniform location.
	GLint pp_toon_levels_loc = -1;       ///< Toon color_levels uniform location.
	GLint pp_haze_texel_loc = -1;        ///< Heat haze texel_size uniform location.
	GLint pp_haze_intensity_loc = -1;    ///< Heat haze intensity uniform location.
	GLint pp_haze_distortion_loc = -1;   ///< Heat haze distortion uniform location.
	GLint pp_haze_time_loc = -1;         ///< Heat haze time uniform location.
	GLint pp_waves_texel_loc = -1;       ///< Water waves texel_size uniform location.
	GLint pp_waves_amplitude_loc = -1;   ///< Water waves wave_amplitude uniform location.
	GLint pp_waves_speed_loc = -1;       ///< Water waves wave_speed uniform location.
	GLint pp_waves_time_loc = -1;        ///< Water waves time uniform location.
	GLint pp_seasonal_season_loc = -1;   ///< Seasonal vegetation season uniform location.
	GLint pp_seasonal_intensity_loc = -1;///< Seasonal vegetation intensity uniform location.

	PostProcessConfig pp_config;     ///< Current post-processing configuration.

	std::chrono::steady_clock::time_point pp_anim_start_time{};  ///< Global animation time base for all animated effects.
	std::chrono::steady_clock::time_point pp_last_frame_time{};    ///< Last frame timestamp for delta_time computation.

	/* Benchmark GPU timer query state. */
	GLuint benchmark_query[2] = {};      ///< Double-buffered GL_TIME_ELAPSED query objects.
	int benchmark_query_idx = 0;         ///< Current ping-pong index for timer queries.
	bool benchmark_query_active = false; ///< A timer query is currently in flight.
	bool benchmark_query_pending = false;///< Previous frame's query result is available to read.
	uint64_t benchmark_gpu_ns = 0;       ///< Last completed GPU post-process time in nanoseconds.

	bool SetupPostProcessFBOs(int display_w, int display_h);
	void DestroyPostProcessFBOs();
	bool InitPostProcessShaders();
	void RenderPostProcess();

	bool SetupViewportScratchBuffer(int vp_w, int vp_h, uint8_t render_scale);
	void DestroyViewportScratchBuffer();

	OpenGLSpriteLRUCache cursor_cache; ///< Cache of encoded cursor sprites.
	PaletteID last_sprite_pal = (PaletteID)-1; ///< Last uploaded remap palette.
	bool clear_cursor_cache = false; ///< A clear of the cursor cache is pending.

	Point cursor_pos{}; ///< Cursor position
	bool cursor_in_window = false; ///< Cursor inside this window
	std::vector<CursorSprite> cursor_sprites{}; ///< Sprites comprising cursor

	OpenGLBackend();
	~OpenGLBackend() override;

	std::optional<std::string_view> Init(const Dimension &screen_res);
	bool InitShaders();

	void InternalClearCursorCache();

	void RenderOglSprite(const OpenGLSprite *gl_sprite, PaletteID pal, int x, int y, ZoomLevel zoom);

public:
	/**
	 * Get singleton instance of this class.
	 * @return Our instance.
	 */
	static inline OpenGLBackend *Get()
	{
		return OpenGLBackend::instance;
	}

	static std::optional<std::string_view> Create(GetOGLProcAddressProc get_proc, const Dimension &screen_res);
	static void Destroy();

	void PrepareContext();

	std::string GetDriverName();

	void UpdatePalette(const Colour *pal, uint first, uint length);
	bool Resize(int w, int h, bool force = false);
	void Paint();

	void DrawMouseCursor();
	void PopulateCursorCache();
	void ClearCursorCache();

	void SetPostProcessConfig(const PostProcessConfig &config);
	bool IsPostProcessSupported() const { return this->pp_fbo_supported; }
	const PostProcessConfig &GetPostProcessConfig() const { return this->pp_config; }

	bool IsViewportCPUScaling() const { return this->vp_cpu_scaling; }
	void *GetViewportScratchBuffer() { return this->vp_buffer.empty() ? nullptr : this->vp_buffer.data(); }
	int GetViewportScratchWidth() const { return this->vp_width; }
	int GetViewportScratchHeight() const { return this->vp_height; }
	int GetViewportScratchPitch() const { return this->vp_pitch; }
	Rect GetViewportScreenRect() const { return this->vp_screen_rect; }

	void InitBenchmarkQueries();
	void DestroyBenchmarkQueries();
	void BeginBenchmarkQuery();
	void EndBenchmarkQuery();
	uint64_t ReadBackBenchmarkGPUTime();

	void *GetVideoBuffer();
	uint8_t *GetAnimBuffer();
	uint8_t *GetClassBuffer();
	void ReleaseVideoBuffer(const Rect &update_rect);
	void ReleaseAnimBuffer(const Rect &update_rect);
	void ReleaseClassBuffer(const Rect &update_rect);

	/* SpriteEncoder */

	bool Is32BppSupported() override { return true; }
	uint GetSpriteAlignment() override { return 1u << to_underlying(ZoomLevel::Max); }
	Sprite *Encode(SpriteType sprite_type, const SpriteLoader::SpriteCollection &sprite, SpriteAllocator &allocator) override;
};


/** Class that encapsulates a RGBA texture together with a paletted remap texture. */
class OpenGLSprite {
private:
	/** Enum of all used OpenGL texture objects. */
	enum Texture : uint8_t {
		TEX_RGBA,    ///< RGBA texture part.
		TEX_REMAP,   ///< Remap texture part.
		NUM_TEX
	};

	Dimension dim{};
	std::array<GLuint, NUM_TEX> tex{}; ///< The texture objects.
	int16_t x_offs = 0;  ///< Number of pixels to shift the sprite to the right.
	int16_t y_offs = 0;  ///< Number of pixels to shift the sprite downwards.

	static std::array<GLuint, NUM_TEX> dummy_tex; ///< 1x1 dummy textures to substitute for unused sprite components.

	static GLuint pal_identity; ///< Identity texture mapping.
	static GLuint pal_tex;      ///< Texture for palette remap.
	static GLuint pal_pbo;      ///< Pixel buffer object for remap upload.

	static bool Create();
	static void Destroy();

	bool BindTextures() const;

public:
	OpenGLSprite(SpriteType sprite_type, const SpriteLoader::SpriteCollection &sprite);

	/* No support for moving/copying the textures is implemented. */
	OpenGLSprite(const OpenGLSprite&) = delete;
	OpenGLSprite(OpenGLSprite&&) = delete;
	OpenGLSprite& operator=(const OpenGLSprite&) = delete;
	OpenGLSprite& operator=(OpenGLSprite&&) = delete;
	~OpenGLSprite();

	void Update(uint width, uint height, uint level, const SpriteLoader::CommonPixel *data);
	Dimension GetSize(ZoomLevel level) const;

	friend class OpenGLBackend;
};

#endif /* VIDEO_OPENGL_H */
