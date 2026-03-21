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
	GLuint pp_fbo[2] = {};           ///< Ping-pong framebuffer objects.
	GLuint pp_tex[2] = {};           ///< Colour texture attachments for ping-pong FBOs.
	Dimension pp_render_size = {};   ///< Internal render resolution.
	Dimension pp_display_size = {};  ///< Display/window resolution.
	bool pp_active = false;          ///< Post-processing pipeline is currently active.
	bool pp_fbo_supported = false;   ///< FBO extensions are available.

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
	GLuint pp_crt_program = 0;       ///< CRT scanline filter shader program.

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
	GLint pp_cg_brightness_loc = -1; ///< Color grading brightness uniform location.
	GLint pp_cg_contrast_loc = -1;   ///< Color grading contrast uniform location.
	GLint pp_cg_saturation_loc = -1; ///< Color grading saturation uniform location.
	GLint pp_cg_temperature_loc = -1;///< Color grading temperature uniform location.
	GLint pp_vig_intensity_loc = -1; ///< Vignette intensity uniform location.
	GLint pp_vig_radius_loc = -1;    ///< Vignette radius uniform location.
	GLint pp_vig_softness_loc = -1;  ///< Vignette softness uniform location.
	GLint pp_night_int_loc = -1;     ///< Night mode intensity uniform location.
	GLint pp_night_blue_loc = -1;    ///< Night mode blue shift uniform location.
	GLint pp_grain_int_loc = -1;     ///< Film grain intensity uniform location.
	GLint pp_grain_time_loc = -1;    ///< Film grain time uniform location.
	GLint pp_crt_texel_loc = -1;     ///< CRT texel_size uniform location.
	GLint pp_crt_res_loc = -1;       ///< CRT resolution uniform location.
	GLint pp_crt_scanline_loc = -1;  ///< CRT scanline_intensity uniform location.
	GLint pp_crt_curve_loc = -1;     ///< CRT curvature uniform location.
	GLint pp_crt_aberr_loc = -1;     ///< CRT chromatic_aberr uniform location.

	PostProcessConfig pp_config;     ///< Current post-processing configuration.

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

	void InitBenchmarkQueries();
	void DestroyBenchmarkQueries();
	void BeginBenchmarkQuery();
	void EndBenchmarkQuery();
	uint64_t ReadBackBenchmarkGPUTime();

	void *GetVideoBuffer();
	uint8_t *GetAnimBuffer();
	void ReleaseVideoBuffer(const Rect &update_rect);
	void ReleaseAnimBuffer(const Rect &update_rect);

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
