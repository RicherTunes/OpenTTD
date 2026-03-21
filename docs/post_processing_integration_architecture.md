# Post-Processing Integration Architecture

**Date:** 2026-03-21
**Purpose:** Technical architecture for adding shader-based post-processing to OpenTTD's OpenGL backend -- the recommended alternative to DLSS/FSR for visual enhancement.

## 1. Current Rendering Flow

```
CPU Blitter -> PBO (CPU-mapped) -> vid_texture (GPU) -> Fullscreen Quad -> Default FBO -> Screen
```

The `OpenGLBackend::Paint()` method (src/video/opengl.cpp:1065):
1. Clears the screen
2. Binds `vid_texture` (the CPU-rendered framebuffer)
3. Selects the appropriate shader (direct RGBA, palette lookup, or remap)
4. Draws a fullscreen quad (`vao_quad`) with 4 vertices
5. The SDL2/Win32 driver then swaps buffers

## 2. Proposed Rendering Flow

```
CPU Blitter -> PBO -> vid_texture -> Fullscreen Quad -> Scene FBO (off-screen)
    -> Post-Process Pass 1 -> PP FBO 1
    -> Post-Process Pass 2 -> PP FBO 2
    -> ...
    -> Final Pass -> Default FBO -> Screen
```

### Key Changes:
1. **Scene FBO:** Instead of rendering the framebuffer quad to the default framebuffer, render it to an off-screen FBO
2. **Post-process chain:** Apply shader passes sequentially, ping-ponging between two FBOs
3. **Final output:** The last pass renders to the default framebuffer (screen)

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

## 8. Files to Modify

| File | Changes |
|------|---------|
| `src/video/opengl.h` | Add PP member variables and methods |
| `src/video/opengl.cpp` | Add PP init, resize, apply, destroy; modify Paint() |
| `src/table/opengl_shader.h` | Add post-processing shader source strings |
| `src/video/sdl2_opengl_v.cpp` | Potentially adjust AllocateBackingStore for render scaling |
| `src/video/win32_v.cpp` | Same for Win32 OpenGL driver |
| `src/settings_type.h` | Add PP setting fields |
| `src/table/settings/gui_settings.ini` | Add PP setting definitions |
| `src/widgets/game_settings_widget.h` | Add PP settings UI widgets |

## 9. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| FBO not supported on old GPU | Very Low | Medium | OpenGL 3.0+ already required; FBOs are core |
| Performance regression from extra pass | Low | Low | Effects are optional; trivial full-screen quad cost |
| Visual artifacts from upscaling | Medium | Low | Provide disable option; test with multiple zoom levels |
| Maintenance burden | Low | Low | Self-contained in OpenGL backend; no blitter changes |
| Breaks palette animation | Medium | Medium | Ensure PP operates after palette resolve in remap shader |
