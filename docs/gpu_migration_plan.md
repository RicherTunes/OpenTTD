# OpenTTD GPU Migration Plan: Enabling DLSS & FSR

**Date:** 2026-03-21
**Version:** 1.0 (Pre-adversarial review)
**Goal:** Migrate OpenTTD's rendering pipeline to enable DLSS and FSR integration (version-agnostic), improving visual quality while maintaining the game's broad hardware compatibility.

---

## 0. Revised Technology Assessment

### DLSS 4.5 (Available Now)

DLSS 4.5 is the current shipping version (CES 2026). Key properties:

| Property | Detail |
|----------|--------|
| **Super Resolution** | 2nd gen transformer model, 5x compute of DLSS 4.0 |
| **Frame Generation** | Dynamic Multi Frame Gen (auto 2x-6x), RTX 40/50 only |
| **GPU Support** | All GeForce RTX GPUs (20/30/40/50 series) for Super Resolution |
| **API Requirement** | DirectX 11, DirectX 12, or Vulkan |
| **SDK** | NVIDIA Streamline Framework |
| **Inputs Required** | Color buffer, motion vectors, depth buffer, jittered projection |
| **OpenGL Support** | None |

### FSR 2.x / 3.x (Open Source, Cross-Vendor)

| Property | FSR 2.x | FSR 3.x |
|----------|---------|---------|
| **GPU Support** | Any vendor, any GPU with compute | Any vendor, any GPU with compute |
| **API** | DX12, Vulkan, **OpenGL (community port)** | DX12, Vulkan |
| **Open Source** | Yes (MIT license) | Yes |
| **Inputs** | Color, motion vectors, depth, jitter | Same + frame gen needs swap chain |
| **OpenGL Port** | Exists, proven (~1200 LOC backend) | No OpenGL port |

### Critical Finding: FSR 2 OpenGL Port

A complete FSR 2 OpenGL backend exists ([GitHub](https://github.com/JuanDiegoMontoya/FidelityFX-FSR2-OpenGL)). It requires:
- OpenGL 4.3+ (compute shaders)
- SPIR-V shader support
- 8 image units minimum
- ~1,200 lines of backend code

OpenTTD's Win32 OpenGL driver already requests OpenGL 4.5 context. This is a viable path.

---

## 1. Strategy: Three-Phase GPU Migration

### Phase Overview

```
Phase 1: Post-Processing Foundation + FSR 1         [4-6 weeks]
    Adds FBO pipeline, spatial upscaling, sharpening
    No motion vectors needed. Works TODAY on existing OpenGL.

Phase 2: Motion Vector Generation + FSR 2 on OpenGL [8-12 weeks]
    Generates per-pixel motion vectors from game state
    Integrates FSR 2 temporal upscaling via OpenGL compute
    Cross-vendor, works on any GPU with OpenGL 4.3+

Phase 3: Vulkan/DX11 Backend + DLSS Integration     [12-20 weeks]
    New rendering backend for Vulkan (primary) and DX11 (Windows)
    Enables DLSS Super Resolution and Frame Generation
    Enables FSR 3+ features
```

### Why This Order

1. **Phase 1 delivers value immediately** with zero architectural risk
2. **Phase 2 solves the hardest novel problem** (motion vectors for 2D) while staying on proven OpenGL
3. **Phase 3 is the most effort** but by then motion vectors and post-processing are solved -- the new backend "just" needs to present them to DLSS/FSR SDKs

---

## 2. Phase 1: Post-Processing Foundation + FSR 1

**Duration:** 4-6 weeks
**Risk:** Low
**Prerequisite:** None

### 2.1 Objectives

- Add FBO-based off-screen rendering to OpenGL backend
- Implement render scaling (render at lower resolution, display at window resolution)
- Add FSR 1 spatial upscaling as GLSL compute/fragment shader
- Add CAS (Contrast Adaptive Sharpening) post-process
- Add user-facing settings for render scale and sharpening

### 2.2 Technical Changes

#### New OpenGL Resources

```
OpenGLBackend additions:
  - scene_fbo: Framebuffer Object for off-screen rendering
  - scene_texture: Color attachment at render resolution
  - pp_fbo[2]: Ping-pong FBOs for post-processing chain
  - pp_texture[2]: Color attachments for ping-pong
  - upscale_program: FSR 1 / bilinear upscale shader
  - sharpen_program: CAS sharpening shader
```

#### Modified Render Flow

```
Before:
  CPU blitter -> PBO -> vid_texture -> fullscreen quad -> screen

After:
  CPU blitter -> PBO -> vid_texture (at render_scale resolution)
    -> fullscreen quad -> scene_fbo (render resolution)
    -> upscale pass -> pp_fbo[0] (display resolution)
    -> sharpen pass -> screen
```

#### Render Scaling Architecture

```cpp
/* New settings */
int render_scale = 100;  /* 50-100, percentage of display resolution */

/* In OpenGLBackend::Resize() */
int render_w = display_w * render_scale / 100;
int render_h = display_h * render_scale / 100;
/* PBO and vid_texture sized to render_w x render_h */
/* pp_fbo sized to display_w x display_h */
```

#### Files Modified

| File | Change |
|------|--------|
| `src/video/opengl.h` | Add FBO/texture members, post-process methods |
| `src/video/opengl.cpp` | FBO init/resize/destroy, modified Paint(), post-process chain |
| `src/table/opengl_shader.h` | Add FSR 1 and CAS shader source |
| `src/settings_type.h` | Add `render_scale`, `sharpening`, `upscale_mode` fields |
| `src/table/settings/gui_settings.ini` | Setting definitions |

#### Estimated LOC

- FBO infrastructure: ~200 lines
- FSR 1 shader (GLSL port): ~150 lines
- CAS shader: ~40 lines
- Render scaling plumbing: ~100 lines
- Settings integration: ~50 lines
- **Total: ~540 lines**

### 2.3 Deliverables

- [ ] Render scaling at 50-100% of display resolution
- [ ] FSR 1 spatial upscaling
- [ ] CAS sharpening (adjustable intensity)
- [ ] User settings in GUI
- [ ] Performance benchmarks showing no regression at 100% scale

---

## 3. Phase 2: Motion Vector Generation + FSR 2 on OpenGL

**Duration:** 8-12 weeks
**Risk:** Medium-High (novel problem: motion vectors for 2D sprite engine)
**Prerequisite:** Phase 1 complete

### 3.1 Objectives

- Generate per-pixel motion vectors from OpenTTD's game state
- Generate a synthetic depth buffer from isometric tile coordinates
- Implement projection jitter for temporal super resolution
- Integrate FSR 2 via OpenGL compute shaders
- Achieve temporal upscaling quality superior to FSR 1

### 3.2 Motion Vector Generation

This is the core technical challenge. OpenTTD has no 3D projection matrix, so motion vectors must be synthesized from game state.

#### Motion Vector Sources

| Source | Motion Type | Difficulty | Coverage |
|--------|------------|------------|----------|
| Viewport scroll | Uniform translation | Easy | ~70% of pixels |
| Vehicle movement | Per-object translation | Medium | ~5-10% of pixels |
| Sprite animation | Discrete frame changes | Hard | ~5% of pixels |
| Palette animation | Color-only change | N/A (no motion) | ~2% of pixels |
| Static tiles | Zero motion | Easy | ~15% of pixels |

#### Architecture: Motion Vector Buffer

Add a **motion vector buffer** alongside the video buffer, written by the blitter during compositing.

```cpp
/* New buffer: 2x float16 per pixel (screen-space motion in pixels) */
struct MotionVector {
    int16_t dx;  /* Horizontal motion in 1/8 pixel units (fixed-point) */
    int16_t dy;  /* Vertical motion in 1/8 pixel units (fixed-point) */
};

/* In OpenGLBackend */
GLuint mv_pbo = 0;        /* PBO for motion vector data */
GLuint mv_texture = 0;    /* Texture for motion vectors */
MotionVector *mv_buffer;   /* CPU-mapped buffer */
```

#### Motion Vector Generation Strategy

**Layer 1: Viewport Motion (global)**
```cpp
/* Computed once per frame from viewport scroll delta */
int16_t global_dx = (prev_viewport_x - curr_viewport_x) * 8;  /* 1/8 px */
int16_t global_dy = (prev_viewport_y - curr_viewport_y) * 8;
/* Fill entire MV buffer with this value (memset-like) */
```

**Layer 2: Vehicle Motion (per-object)**
```cpp
/* For each vehicle drawn this frame:
   1. Get vehicle's previous screen position (cached from last frame)
   2. Get current screen position
   3. Compute delta
   4. Write to MV buffer for all pixels covered by vehicle sprite */

/* Requires: blitter modification to accept MV buffer and write during Draw() */
```

**Layer 3: Zero-motion override for static elements**
```cpp
/* Tiles that haven't changed between frames get (0,0) motion.
   This prevents the global viewport MV from being applied to
   areas where static tiles scrolled into view from off-screen. */
```

#### Blitter Interface Extension

```cpp
class Blitter {
    /* Existing */
    virtual void Draw(BlitterParams *bp, BlitterMode mode, ZoomLevel zoom) = 0;

    /* New: Draw with motion vector output */
    virtual void DrawWithMotion(BlitterParams *bp, BlitterMode mode, ZoomLevel zoom,
                                 MotionVector *mv_dst, int mv_pitch,
                                 int16_t obj_dx, int16_t obj_dy) {
        /* Default: delegate to Draw(), write uniform motion */
        this->Draw(bp, mode, zoom);
        /* Write obj_dx/obj_dy to all pixels in the drawn region */
    }
};
```

### 3.3 Synthetic Depth Buffer

OpenTTD's isometric view has implicit depth based on tile position and height.

```cpp
/* Depth = f(tile_x, tile_y, tile_z) mapped to [0, 1] */
float depth = (tile_x + tile_y + tile_z * 2) / max_world_diagonal;
```

Written during tile/sprite drawing, similar to motion vectors. Stored as a single-channel float16 texture.

### 3.4 Projection Jitter

FSR 2 requires sub-pixel jitter applied to the rendered image each frame for temporal accumulation.

Since OpenTTD has no projection matrix, jitter is applied as a **sub-pixel offset to sprite drawing coordinates**:

```cpp
/* Per-frame jitter from Halton sequence (standard for TAA) */
float jitter_x = halton_sequence(frame_number, 2) - 0.5f;  /* [-0.5, 0.5] */
float jitter_y = halton_sequence(frame_number, 3) - 0.5f;

/* Applied in GfxBlitter as sub-pixel offset to sprite positions */
/* OR applied as texture coordinate offset in the OpenGL upload shader */
```

**Important:** Sub-pixel jitter is problematic for pixel art. At 1:1 zoom, a half-pixel offset would cause visible shimmer. This should only be applied when render_scale < 100% (upscaling active) and at zoom levels where sub-pixel precision matters.

### 3.5 FSR 2 OpenGL Integration

Based on the proven [FSR 2 OpenGL port](https://github.com/JuanDiegoMontoya/FidelityFX-FSR2-OpenGL):

```
Inputs to FSR 2:
  - Color buffer (render resolution) -> scene_texture
  - Motion vectors (render resolution) -> mv_texture
  - Depth buffer (render resolution) -> depth_texture
  - Jitter offsets -> per-frame uniforms

Output from FSR 2:
  - Upscaled color buffer (display resolution) -> pp_texture

Integration:
  - FSR 2 context created at startup
  - Each frame: dispatch FSR 2 compute shaders between scene render and display
  - Replaces FSR 1 upscale pass from Phase 1
```

#### OpenGL Requirements

| Requirement | OpenTTD Status |
|-------------|---------------|
| OpenGL 4.3+ (compute shaders) | Win32 requests 4.5, falls back to 3.2 |
| SPIR-V support (GL_ARB_gl_spirv) | Available in GL 4.6, extension in 4.5 |
| 8+ image units | Standard on modern GPUs |
| Shader storage buffers | Core in GL 4.3 |

**Fallback:** If GPU doesn't support GL 4.3+, fall back to FSR 1 (Phase 1). This preserves OpenTTD's broad hardware compatibility.

### 3.6 Files Modified/Created

| File | Change |
|------|--------|
| `src/blitter/base.hpp` | Add `DrawWithMotion()` virtual method |
| `src/blitter/32bpp_optimized.hpp/cpp` | Implement motion vector writing |
| `src/blitter/40bpp_anim.hpp/cpp` | Same |
| `src/video/opengl.h/cpp` | Add MV buffer, depth buffer, FSR 2 context, jitter |
| `src/video/motion_vectors.h` | MotionVector struct, Halton sequence |
| `src/viewport.cpp` | Pass vehicle motion data to drawing functions |
| `src/vehicle_base.h` | Cache previous frame screen position |
| `3rdparty/fsr2/` | FSR 2 SDK + OpenGL backend (~1200 LOC) |

#### Estimated LOC

- Motion vector buffer infrastructure: ~300 lines
- Blitter MV extension: ~200 lines per blitter variant
- Depth buffer generation: ~150 lines
- Jitter system: ~80 lines
- FSR 2 integration glue: ~400 lines
- FSR 2 OpenGL backend (vendored): ~1,200 lines
- **Total new code: ~2,330 lines** (excluding vendored FSR 2 SDK)

### 3.7 Deliverables

- [ ] Per-pixel motion vectors (viewport + vehicle motion)
- [ ] Synthetic depth buffer
- [ ] FSR 2 temporal upscaling via OpenGL compute
- [ ] Graceful fallback to FSR 1 on older GPUs
- [ ] Quality comparison: FSR 2 vs FSR 1 vs native
- [ ] Performance benchmarks at various render scales

---

## 4. Phase 3: Vulkan/DX11 Backend + DLSS

**Duration:** 12-20 weeks
**Risk:** High (new rendering backend, significant complexity)
**Prerequisite:** Phase 2 complete (motion vectors + depth working)

### 4.1 Objectives

- Implement a Vulkan rendering backend (primary)
- Implement a DirectX 11 rendering backend (Windows, for DLSS compatibility)
- Integrate DLSS Super Resolution via Streamline SDK
- Integrate FSR 3.x via FidelityFX SDK (Vulkan path)
- Maintain OpenGL backend as fallback

### 4.2 Why Both Vulkan and DX11?

| Backend | Reason |
|---------|--------|
| **Vulkan** | Cross-platform (Linux/Windows/macOS via MoltenVK), FSR 3 support, modern API |
| **DX11** | DLSS has mature DX11 support, lower complexity than DX12, wide Windows compatibility |

DX12 is intentionally excluded -- it adds massive complexity over DX11 with no benefit for OpenTTD's simple rendering model (single fullscreen quad + post-processing).

### 4.3 Rendering Backend Abstraction

Before building new backends, abstract the rendering interface away from OpenGL specifics:

```cpp
/* New abstraction layer */
class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    /* Lifecycle */
    virtual bool Init(const Dimension &screen_res) = 0;
    virtual void Shutdown() = 0;
    virtual bool Resize(int w, int h) = 0;

    /* Buffer access (for CPU blitter) */
    virtual void *MapVideoBuffer() = 0;
    virtual void UnmapVideoBuffer(const Rect &dirty) = 0;
    virtual MotionVector *MapMotionVectorBuffer() = 0;
    virtual void UnmapMotionVectorBuffer() = 0;

    /* Rendering */
    virtual void BeginFrame() = 0;
    virtual void Paint() = 0;
    virtual void EndFrame() = 0;

    /* Upscaling */
    virtual void SetUpscaleMode(UpscaleMode mode) = 0;
    virtual void SetRenderScale(int percent) = 0;

    /* Palette */
    virtual void UpdatePalette(const Colour *pal, uint first, uint length) = 0;

    /* Info */
    virtual std::string GetDriverName() const = 0;
    virtual bool SupportsCompute() const = 0;
};
```

### 4.4 Vulkan Backend Architecture

The Vulkan backend mirrors the OpenGL backend's "CPU blitter + GPU presentation" model. It does NOT render sprites on the GPU -- it presents the CPU-rendered framebuffer and applies post-processing.

```
Vulkan resources:
  - VkInstance, VkDevice, VkQueue
  - VkSwapchainKHR (presentation)
  - VkBuffer (staging buffer for CPU framebuffer upload, replaces PBO)
  - VkImage (video texture, MV texture, depth texture)
  - VkPipeline (fullscreen quad rendering)
  - VkComputePipeline (FSR 3 / custom post-processing)
  - VkDescriptorSet (resource bindings)

Render flow:
  1. CPU blitter writes to mapped VkBuffer (staging)
  2. Copy staging buffer to VkImage (video texture)
  3. Execute upscaling compute dispatch (FSR 3)
  4. Render fullscreen quad with upscaled texture
  5. Apply post-processing (sharpening, effects)
  6. Present swapchain image
```

### 4.5 DX11 Backend Architecture

Simpler than Vulkan (no explicit synchronization, no command buffers):

```
DX11 resources:
  - ID3D11Device, ID3D11DeviceContext
  - IDXGISwapChain
  - ID3D11Texture2D (video, MV, depth textures)
  - ID3D11Buffer (staging buffer for CPU upload)
  - ID3D11VertexShader / ID3D11PixelShader (fullscreen quad)
  - ID3D11ComputeShader (post-processing)

DLSS integration via Streamline:
  - sl::dlssSetOptions() to configure DLSS mode
  - Tag resources: sl::ResourceTag for color, MV, depth
  - sl::evaluateFeature() each frame to run DLSS inference
```

### 4.6 DLSS Integration via Streamline

```cpp
/* Initialization */
sl::Preferences prefs;
prefs.renderAPI = sl::RenderAPI::eD3D11;  /* or eVulkan */
slInit(prefs);
slSetFeatureLoaded(sl::kFeatureDLSS, true);

/* Per-frame */
sl::DLSSOptions options;
options.mode = sl::DLSSMode::eBalanced;
options.outputWidth = display_width;
options.outputHeight = display_height;
slDLSSSetOptions(viewport_id, options);

/* Tag resources */
sl::Resource colorIn = {sl::ResourceType::eTex2d, color_texture, ...};
sl::Resource motionVectors = {sl::ResourceType::eTex2d, mv_texture, ...};
sl::Resource depth = {sl::ResourceType::eTex2d, depth_texture, ...};
slSetTag(viewport_id, &colorIn, &motionVectors, &depth, ...);

/* Evaluate */
sl::ViewportHandle viewport(viewport_id);
slEvaluateFeature(sl::kFeatureDLSS, frame_token, &viewport, 1, ...);
```

### 4.7 Video Driver Integration

New video driver classes:

```
VideoDriver_Win32Vulkan     (Windows, Vulkan + FSR 3 + optional DLSS via DX11 interop)
VideoDriver_Win32DX11       (Windows, DX11 + DLSS via Streamline)
VideoDriver_SDL_Vulkan      (Linux/Windows, Vulkan + FSR 3)
```

Factory priorities (higher = preferred):
```
win32-dx11-dlss:  12  (Windows, NVIDIA GPU detected)
win32-vulkan:     11  (Windows, Vulkan available)
win32-opengl:     10  (Windows, existing)
sdl-vulkan:        9  (Linux, Vulkan available)
sdl-opengl:        8  (Linux/Windows, existing)
win32:             9  (Windows GDI, existing)
sdl:               5  (Software, existing)
```

### 4.8 Files Created

| File | Purpose |
|------|--------|
| `src/video/render_backend.h` | Abstract RenderBackend interface |
| `src/video/opengl_backend.cpp` | OpenGLBackend refactored to implement RenderBackend |
| `src/video/vulkan_backend.h/cpp` | Vulkan RenderBackend implementation |
| `src/video/dx11_backend.h/cpp` | DirectX 11 RenderBackend implementation |
| `src/video/vulkan_v.h/cpp` | Vulkan video driver (SDL + Vulkan) |
| `src/video/win32_dx11_v.h/cpp` | Win32 DX11 video driver |
| `src/video/dlss_integration.h/cpp` | DLSS Streamline wrapper |
| `src/video/fsr3_integration.h/cpp` | FSR 3 FidelityFX wrapper |
| `3rdparty/streamline/` | Vendored Streamline SDK headers/libs |
| `3rdparty/fsr3/` | Vendored FSR 3 SDK |

#### Estimated LOC

- RenderBackend abstraction: ~150 lines
- Vulkan backend: ~2,500 lines
- DX11 backend: ~1,500 lines
- DLSS integration: ~400 lines
- FSR 3 integration: ~400 lines
- Video driver wrappers: ~600 lines
- CMake/build changes: ~200 lines
- **Total: ~5,750 lines**

### 4.9 Deliverables

- [ ] Vulkan rendering backend (Linux + Windows)
- [ ] DX11 rendering backend (Windows)
- [ ] DLSS Super Resolution integration
- [ ] FSR 3 temporal upscaling integration
- [ ] Automatic backend selection based on GPU/driver
- [ ] Fallback chain: DLSS -> FSR 3 -> FSR 2 (OpenGL) -> FSR 1 -> None
- [ ] Performance and quality comparison across all modes

---

## 5. Hardware Compatibility Matrix

| GPU | Phase 1 | Phase 2 | Phase 3 |
|-----|---------|---------|---------|
| Any GPU (software) | No (no OpenGL) | No | No |
| Intel/AMD GL 3.2+ | FSR 1 + CAS | FSR 1 only (no compute) | N/A |
| Any GPU GL 4.3+ | FSR 1 + CAS | FSR 2 | N/A |
| AMD RDNA+ (Vulkan) | FSR 1 + CAS | FSR 2 | FSR 3 |
| NVIDIA GTX (Vulkan) | FSR 1 + CAS | FSR 2 | FSR 3 |
| NVIDIA RTX 20/30 | FSR 1 + CAS | FSR 2 | DLSS SR + FSR 3 |
| NVIDIA RTX 40/50 | FSR 1 + CAS | FSR 2 | DLSS SR + Frame Gen + FSR 3 |
| AMD RX 9000 | FSR 1 + CAS | FSR 2 | FSR 4 (driver-based) |

**Key principle: Every phase is additive. No existing hardware support is removed.**

---

## 6. Fallback Chain

The system auto-selects the best available upscaling technology:

```
1. DLSS Super Resolution (RTX GPU + DX11/Vulkan backend)
2. FSR 3 Upscaling (Vulkan backend, any GPU)
3. FSR 2 Upscaling (OpenGL 4.3+ compute, any GPU)
4. FSR 1 Spatial (OpenGL 3.2+ fragment shader, any GPU)
5. CAS Sharpening only (any OpenGL GPU)
6. No upscaling (native resolution, any backend)
```

User can override auto-selection. Each tier gracefully degrades to the next if hardware doesn't support it.

---

## 7. Risk Register

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|------------|--------|------------|
| R1 | Motion vectors inaccurate for 2D sprites | High | Medium | Start with viewport-only MV; iterate. FSR 2 is tolerant of imperfect MVs. |
| R2 | FSR 2 on OpenGL has perf issues on NVIDIA | Medium | Medium | Benchmark early; provide FSR 1 fallback. Phase 3 (Vulkan) resolves. |
| R3 | Sub-pixel jitter causes shimmer on pixel art | High | Medium | Only jitter when render_scale < 100%. Use conservative jitter amplitude. |
| R4 | Vulkan backend maintenance burden | Medium | High | Keep it minimal (presentation only). Use vk-bootstrap or similar. |
| R5 | Streamline SDK licensing conflicts with GPLv2 | Medium | High | Investigate early. May need to ship as optional DLL/plugin. |
| R6 | DLSS version churn breaks integration | Low | Medium | Streamline abstracts version differences. Pin to stable SDK version. |
| R7 | OpenTTD community rejects GPU requirement | Medium | Medium | All phases are opt-in. Software rendering always works. |
| R8 | 40bpp blitter + MV buffer = CPU perf regression | Medium | Medium | MV writing is cheap (memcpy-level). Benchmark and optimize. |

---

## 8. Success Metrics

| Metric | Target |
|--------|--------|
| FSR 1 at 75% render scale | Visually acceptable, <1ms GPU overhead |
| FSR 2 at 50% render scale | Comparable to native, <3ms total overhead |
| DLSS at 50% render scale | Better than native quality |
| No regression at 100% scale | <0.5ms additional overhead |
| Fallback chain latency | <100ms to detect and select appropriate mode |
| Hardware compatibility | No existing hardware loses functionality |

---

## 9. Open Questions

1. **GPLv2 + Streamline SDK licensing:** Streamline is MIT-licensed, but ships NVIDIA proprietary DLLs. How does this interact with OpenTTD's GPLv2? May need the DLSS integration as an optional, separately-distributed plugin.

2. **FSR 2 jitter in pixel art context:** Does temporal accumulation meaningfully help when the source content is low-frequency pixel art? Need prototyping.

3. **Motion vector precision for discrete sprite motion:** When a vehicle sprite "jumps" between positions (as opposed to smooth sub-pixel motion), do temporal upscalers handle this correctly?

4. **Community appetite:** Is there demand for this among OpenTTD players? Should be gauged before Phase 3.

Sources:
- [NVIDIA DLSS 4.5 Technical Blog](https://developer.nvidia.com/blog/nvidia-dlss-4-5-delivers-super-resolution-upgrades-and-new-dynamic-multi-frame-generation/)
- [DLSS 4.5 Announcement](https://www.nvidia.com/en-us/geforce/news/dlss-4-5-dynamic-multi-frame-gen-6x-2nd-gen-transformer-super-res/)
- [DLSS 4.5 on all RTX GPUs](https://www.nvidia.com/en-us/geforce/news/dlss-4-5-super-resolution-available-now/)
- [Streamline SDK GitHub](https://github.com/NVIDIA-RTX/Streamline)
- [Streamline DLSS Programming Guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md)
- [FSR 2 OpenGL Port Blog](https://juandiegomontoya.github.io/porting_fsr2.html)
- [FSR 2 OpenGL GitHub](https://github.com/JuanDiegoMontoya/FidelityFX-FSR2-OpenGL)
- [FSR 2 GPUOpen](https://gpuopen.com/fidelityfx-superresolution-2/)
- [FSR 3 GPUOpen](https://gpuopen.com/fidelityfx-super-resolution-3/)
- [AMD FidelityFX SDK](https://gpuopen.com/amd-fsr-sdk/)
