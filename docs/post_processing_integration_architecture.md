# Post-Processing Integration Architecture

**Date:** 2026-03-21 (originally); updated 2026-03-22
**Purpose:** Technical architecture for shader-based post-processing in OpenTTD's OpenGL backend.

> **Status (2026-03-22):** This architecture has been fully implemented. The "proposed" flow below is now the **actual** flow. 29 shader programs, FBO ping-pong pipeline, CPU viewport scaling, 66+ configurable globals, 14 presets. See CLAUDE.md for the complete implementation reference.

## 1. Rendering Flow (Implemented)

```
Without PP (vanilla):
  CPU Blitter -> PBO -> vid_texture -> VBO Quad (GL_TRIANGLE_STRIP, 4 verts) -> Screen

With PP enabled:
  CPU Blitter -> PBO -> vid_texture -> VBO Quad -> FBO[0] (render resolution)
      -> [Optional: CPU VP scaling composites vp_texture over viewport area]
      -> Upscale pass (FSR EASU/bilinear/temporal/plugin) -> FBO ping-pong
      -> Effect passes (up to 29 shaders) -> FBO ping-pong (GL_TRIANGLES, 3 verts via gl_VertexID)
      -> Final pass -> Default FBO (display resolution) -> Screen
      -> [Cursor drawn at display resolution after all PP]
```

### Architecture:
1. **FBO[0]:** Scene is rendered to FBO[0] at render resolution (25-200% of display)
2. **Ping-pong chain:** Effect passes alternate between FBO[0] and FBO[1], each reading from source and writing to destination
3. **Final output:** The last pass renders to the default framebuffer (screen) at display resolution
4. **CPU viewport scaling:** Optional -- viewport rendered to half-size scratch buffer at zoom+1, GPU-upscaled and composited before PP chain

## 3. Required OpenGL Resources

### 3.1 New Resources in OpenGLBackend

```cpp
/* Post-processing resources */
GLuint pp_fbo[2] = {};        /* Ping-pong framebuffer objects */
GLuint pp_texture[2] = {};    /* Colour attachments for FBOs */
Dimension pp_size = {};       /* Current post-processing buffer size */

/* Shader programs for each effect */
GLuint pp_sharpen_program = 0;
GLuint pp_upscale_program = 0;
/* ... additional effect programs */
```

### 3.2 OpenGL Extensions Needed

The existing OpenGL backend already requires OpenGL 3.0+ on Windows (3.2+ enforced). FBOs are core in OpenGL 3.0, so **no additional extension requirements**.

Required functions (already available in OpenGL 3.0+):
- `glGenFramebuffers` / `glDeleteFramebuffers`
- `glBindFramebuffer`
- `glFramebufferTexture2D`
- `glCheckFramebufferStatus`
- `glDrawBuffer` / `glReadBuffer`

## 4. Integration Points

### 4.1 OpenGLBackend Modifications

**`opengl.h`** -- Add post-processing state:
```cpp
class OpenGLBackend : public SpriteEncoder {
private:
    /* ... existing members ... */

    /* Post-processing */
    GLuint pp_fbo[2] = {};
    GLuint pp_texture[2] = {};
    Dimension pp_size = {};
    bool pp_enabled = false;

    /* Effect shader programs */
    struct PostProcessEffect {
        GLuint program;
        std::string_view name;
        bool enabled;
    };
    std::vector<PostProcessEffect> pp_effects;

    bool InitPostProcessing();
    void DestroyPostProcessing();
    void ResizePostProcessing(int w, int h);
    void ApplyPostProcessing();
};
```

**`opengl.cpp`** -- Modify `Paint()`:
```cpp
void OpenGLBackend::Paint()
{
    if (this->pp_enabled && !this->pp_effects.empty()) {
        /* Render scene to off-screen FBO */
        _glBindFramebuffer(GL_FRAMEBUFFER, this->pp_fbo[0]);
        _glViewport(0, 0, this->pp_size.width, this->pp_size.height);
    }

    /* Existing rendering code (clear, bind textures, draw quad) */
    _glClear(GL_COLOR_BUFFER_BIT);
    _glDisable(GL_BLEND);
    /* ... existing texture binding and shader selection ... */
    _glBindVertexArray(this->vao_quad);
    _glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    _glEnable(GL_BLEND);

    if (this->pp_enabled && !this->pp_effects.empty()) {
        this->ApplyPostProcessing();
    }
}

void OpenGLBackend::ApplyPostProcessing()
{
    int src = 0; /* Source FBO index (ping-pong) */

    for (size_t i = 0; i < this->pp_effects.size(); ++i) {
        if (!this->pp_effects[i].enabled) continue;

        bool is_last = true;
        for (size_t j = i + 1; j < this->pp_effects.size(); ++j) {
            if (this->pp_effects[j].enabled) { is_last = false; break; }
        }

        if (is_last) {
            /* Final pass: render to default framebuffer */
            _glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else {
            /* Intermediate pass: render to other ping-pong FBO */
            _glBindFramebuffer(GL_FRAMEBUFFER, this->pp_fbo[1 - src]);
        }

        _glUseProgram(this->pp_effects[i].program);
        _glActiveTexture(GL_TEXTURE0);
        _glBindTexture(GL_TEXTURE_2D, this->pp_texture[src]);

        /* Set uniforms: texture size, effect-specific parameters */
        /* ... */

        _glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        if (!is_last) src = 1 - src;
    }
}
```

### 4.2 Settings Integration

Add post-processing options to game settings:
- `gui.pp_sharpening` -- Enable/disable CAS sharpening
- `gui.pp_upscale_mode` -- None / FSR1 / xBR / HQ4x
- `gui.pp_render_scale` -- Internal render scale (50%-100% for upscaling use)
- `gui.pp_art_filter` -- None / CRT / Tilt-Shift / Watercolor

## 5. Example Shader: Contrast Adaptive Sharpening (CAS)

This AMD CAS shader is a good first post-processing effect. It is:
- Simple to implement (~30 lines GLSL)
- Visually effective for pixel art (sharpens edges without artifacts)
- Very fast (single texture sample + 4 neighbours)
- Open-source (MIT license from AMD)

```glsl
#version 150

uniform sampler2D scene_tex;
uniform vec2 texel_size;   /* 1.0 / texture_size */
uniform float sharpness;   /* 0.0 to 1.0 */

in vec2 colour_tex_uv;
out vec4 colour;

void main() {
    vec3 c = texture(scene_tex, colour_tex_uv).rgb;
    vec3 t = texture(scene_tex, colour_tex_uv + vec2(0, -texel_size.y)).rgb;
    vec3 b = texture(scene_tex, colour_tex_uv + vec2(0,  texel_size.y)).rgb;
    vec3 l = texture(scene_tex, colour_tex_uv + vec2(-texel_size.x, 0)).rgb;
    vec3 r = texture(scene_tex, colour_tex_uv + vec2( texel_size.x, 0)).rgb;

    vec3 mn = min(c, min(min(t, b), min(l, r)));
    vec3 mx = max(c, max(max(t, b), max(l, r)));

    vec3 amp = clamp(min(mn, 1.0 - mx) / mx, 0.0, 1.0);
    amp = sqrt(amp);

    float peak = -3.0 * sharpness + 8.0;
    vec3 w = amp / peak;

    vec3 result = (c + (t + b + l + r) * w) / (1.0 + 4.0 * w);
    colour = vec4(result, 1.0);
}
```

## 6. Example Shader: Tilt-Shift Miniature Effect

Creates a diorama/miniature look by blurring the top and bottom of the screen, simulating shallow depth of field. This is particularly effective for OpenTTD's isometric view.

```glsl
#version 150

uniform sampler2D scene_tex;
uniform vec2 texel_size;
uniform float focus_center;  /* 0.0-1.0, vertical focus point */
uniform float focus_width;   /* 0.0-1.0, width of sharp band */

in vec2 colour_tex_uv;
out vec4 colour;

void main() {
    float dist = abs(colour_tex_uv.y - focus_center);
    float blur_amount = smoothstep(focus_width * 0.5, focus_width, dist);

    vec3 result = vec3(0.0);
    float total_weight = 0.0;

    /* Variable-radius box blur based on distance from focus */
    int radius = int(blur_amount * 4.0);
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            vec2 offset = vec2(float(x), float(y)) * texel_size;
            result += texture(scene_tex, colour_tex_uv + offset).rgb;
            total_weight += 1.0;
        }
    }

    /* Boost saturation slightly for miniature effect */
    result /= total_weight;
    float lum = dot(result, vec3(0.299, 0.587, 0.114));
    result = mix(vec3(lum), result, 1.0 + 0.3 * blur_amount);

    colour = vec4(result, 1.0);
}
```

## 7. Upscaling Architecture

For spatial upscaling (FSR 1, xBR, etc.), the architecture changes slightly:

```
CPU Blitter (at reduced resolution) -> PBO -> vid_texture (small)
    -> Scene FBO (small) -> Upscale Pass -> Upscaled FBO (display resolution)
    -> Optional post-process -> Screen
```

### Key difference:
- The game's internal render resolution (`_screen_width`, `_screen_height`) is set lower than the window size
- The upscaling shader maps the small texture to the full window size
- Additional post-processing (sharpening) is applied at display resolution

### Implementation:
- Add a `render_scale` setting (e.g., 75% = render at 75% of window size)
- The OpenGL `Resize()` method allocates the PBO/vid_texture at scaled size
- The upscale shader is the first post-processing pass, expanding to full resolution
- Subsequent passes operate at full resolution

## 8. Files Modified (Implementation Status)

All files below have been modified. The actual implementation differs from the original proposal in some details (e.g., shader source is in `postprocess_shader.h` not `opengl_shader.h`, settings use `misc_settings.ini` not `gui_settings.ini`).

| File | Status | What was done |
|------|--------|---------------|
| `src/video/opengl.h` | Done | 29 shader programs, FBO members, benchmark queries, CPU VP scaling members |
| `src/video/opengl.cpp` | Done | Paint() with FBO pipeline, RenderPostProcess(), CPU VP compositing, 2000+ lines added |
| `src/video/postprocess.h/.cpp` | New | PostProcessConfig (40+ fields), dimension math, FSR/CAS constants |
| `src/table/postprocess_shader.h` | New | 29 fragment shaders + vertex shader + compute shader as GLSL source arrays |
| `src/video/viewport_cpu_scale.h` | New | GL-free interface for CPU viewport scaling |
| `src/video/motion_vector.h/.cpp` | New | Draw-command recording, tile-based spatial binning |
| `src/video/temporal_upscale.h/.cpp` | New | Halton jitter sequence, temporal upscale interface |
| `src/video/pp_screenshot.h/.cpp` | New | Post-processed framebuffer capture to BMP |
| `src/video/upscale_plugin.h/.cpp` | New | DLSS/FSR plugin C ABI + runtime DLL/SO loader |
| `src/video/render_backend.h` | New | Abstract backend interface for future Vulkan/DX11 |
| `src/video/video_driver.cpp/.hpp` | Done | 66+ global variables for all PP settings |
| `src/table/settings/misc_settings.ini` | Done | INI entries for all PP settings |
| `src/settings_gui.cpp` | Done | GUI sliders/toggles for PP settings |
| `src/console_cmds.cpp` | Done | pp status/info/on/off/enable/disable/set/reset/preset commands |
| `src/benchmark.h/.cpp` | New | GPU benchmark harness with CSV export |
| `src/viewport.cpp` | Done | DrawViewportCPUScaled(), CPU VP scaling interception |
| `src/window_gui.h` | Done | DrawViewportCPUScaled() declaration |
| `src/tests/test_postprocess.cpp` | New | 271 tests, 558 assertions |
| `src/tests/test_motion_vector.cpp` | New | 38 tests |
| `src/tests/test_temporal_upscale.cpp` | New | 14 tests |
| `src/tests/test_upscale_plugin.cpp` | New | 14 tests |

## 9. Risk Assessment (Retrospective)

| Risk | Occurred? | Resolution |
|------|-----------|------------|
| FBO not supported on old GPU | No | OpenGL 3.2+ required; FBOs are core since 3.0 |
| Performance regression from extra pass | No | All effects default OFF; zero overhead when disabled |
| Visual artifacts from upscaling | Minor | FSR EASU outlier taps fixed; bicubic/RCAS clamped to [0,1] |
| Maintenance burden | Low | Self-contained in video/ directory; 400+ unit tests |
| Breaks palette animation | Managed | PP operates after palette resolve; `_screen_disable_anim` guards CPU VP scaling |
